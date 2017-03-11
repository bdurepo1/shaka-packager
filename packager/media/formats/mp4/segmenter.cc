// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/formats/mp4/segmenter.h"

#include <algorithm>

#include "packager/base/logging.h"
#include "packager/media/base/buffer_writer.h"
#include "packager/media/base/media_sample.h"
#include "packager/media/base/muxer_options.h"
#include "packager/media/base/muxer_util.h"
#include "packager/media/base/stream_info.h"
#include "packager/media/chunking/chunking_handler.h"
#include "packager/media/event/progress_listener.h"
#include "packager/media/formats/mp4/box_definitions.h"
#include "packager/media/formats/mp4/fragmenter.h"
#include "packager/version/version.h"

namespace shaka {
namespace media {
namespace mp4 {

namespace {

uint64_t Rescale(uint64_t time_in_old_scale,
                 uint32_t old_scale,
                 uint32_t new_scale) {
  return static_cast<double>(time_in_old_scale) / old_scale * new_scale;
}

}  // namespace

Segmenter::Segmenter(const MuxerOptions& options,
                     std::unique_ptr<FileType> ftyp,
                     std::unique_ptr<Movie> moov)
    : options_(options),
      ftyp_(std::move(ftyp)),
      moov_(std::move(moov)),
      moof_(new MovieFragment()),
      fragment_buffer_(new BufferWriter()),
      sidx_(new SegmentIndex()),
      progress_listener_(NULL),
      progress_target_(0),
      accumulated_progress_(0),
      sample_duration_(0u) {}

Segmenter::~Segmenter() {}

Status Segmenter::Initialize(
    const std::vector<std::shared_ptr<StreamInfo>>& streams,
    MuxerListener* muxer_listener,
    ProgressListener* progress_listener) {
  DCHECK_LT(0u, streams.size());
  muxer_listener_ = muxer_listener;
  progress_listener_ = progress_listener;
  moof_->header.sequence_number = 0;

  moof_->tracks.resize(streams.size());
  fragmenters_.resize(streams.size());

  for (uint32_t i = 0; i < streams.size(); ++i) {
    moof_->tracks[i].header.track_id = i + 1;
    if (streams[i]->stream_type() == kStreamVideo) {
      // Use the first video stream as the reference stream (which is 1-based).
      if (sidx_->reference_id == 0)
        sidx_->reference_id = i + 1;
    }
    fragmenters_[i].reset(new Fragmenter(streams[i], &moof_->tracks[i]));
  }

  if (options_.mp4_use_decoding_timestamp_in_timeline) {
    for (uint32_t i = 0; i < streams.size(); ++i)
      fragmenters_[i]->set_use_decoding_timestamp_in_timeline(true);
  }

  // Choose the first stream if there is no VIDEO.
  if (sidx_->reference_id == 0)
    sidx_->reference_id = 1;
  sidx_->timescale = streams[GetReferenceStreamId()]->time_scale();

  // Use media duration as progress target.
  progress_target_ = streams[GetReferenceStreamId()]->duration();

  // Use the reference stream's time scale as movie time scale.
  moov_->header.timescale = sidx_->timescale;
  moof_->header.sequence_number = 1;

  // Fill in version information.
  const std::string version = GetPackagerVersion();
  if (!version.empty()) {
    moov_->metadata.handler.handler_type = FOURCC_ID32;
    moov_->metadata.id3v2.language.code = "eng";
    moov_->metadata.id3v2.private_frame.owner = GetPackagerProjectUrl();
    moov_->metadata.id3v2.private_frame.value = version;
  }
  return DoInitialize();
}

Status Segmenter::Finalize() {
  // Set tracks and moov durations.
  // Note that the updated moov box will be written to output file for VOD case
  // only.
  for (std::vector<Track>::iterator track = moov_->tracks.begin();
       track != moov_->tracks.end();
       ++track) {
    track->header.duration = Rescale(track->media.header.duration,
                                     track->media.header.timescale,
                                     moov_->header.timescale);
    if (track->header.duration > moov_->header.duration)
      moov_->header.duration = track->header.duration;
  }
  moov_->extends.header.fragment_duration = moov_->header.duration;

  return DoFinalize();
}

Status Segmenter::AddSample(size_t stream_id,
                            std::shared_ptr<MediaSample> sample) {
  // Set default sample duration if it has not been set yet.
  if (moov_->extends.tracks[stream_id].default_sample_duration == 0) {
    moov_->extends.tracks[stream_id].default_sample_duration =
        sample->duration();
  }

  DCHECK_LT(stream_id, fragmenters_.size());
  Fragmenter* fragmenter = fragmenters_[stream_id].get();
  if (fragmenter->fragment_finalized()) {
    return Status(error::FRAGMENT_FINALIZED,
                  "Current fragment is finalized already.");
  }

  Status status = fragmenter->AddSample(sample);
  if (!status.ok())
    return status;

  if (sample_duration_ == 0)
    sample_duration_ = sample->duration();
  moov_->tracks[stream_id].media.header.duration += sample->duration();
  return Status::OK;
}

Status Segmenter::FinalizeSegment(size_t stream_id,
                                  std::shared_ptr<SegmentInfo> segment_info) {
  if (segment_info->key_rotation_encryption_config) {
    FinalizeFragmentForKeyRotation(
        stream_id, segment_info->is_encrypted,
        *segment_info->key_rotation_encryption_config);
  }

  DCHECK_LT(stream_id, fragmenters_.size());
  Fragmenter* fragmenter = fragmenters_[stream_id].get();
  DCHECK(fragmenter);
  Status status = fragmenter->FinalizeFragment();
  if (!status.ok())
    return status;

  // Check if all tracks are ready for fragmentation.
  for (const std::unique_ptr<Fragmenter>& fragmenter : fragmenters_) {
    if (!fragmenter->fragment_finalized())
      return Status::OK;
  }

  MediaData mdat;
  // Data offset relative to 'moof': moof size + mdat header size.
  // The code will also update box sizes for moof_ and its child boxes.
  uint64_t data_offset = moof_->ComputeSize() + mdat.HeaderSize();
  // 'traf' should follow 'mfhd' moof header box.
  uint64_t next_traf_position = moof_->HeaderSize() + moof_->header.box_size();
  for (size_t i = 0; i < moof_->tracks.size(); ++i) {
    TrackFragment& traf = moof_->tracks[i];
    if (traf.auxiliary_offset.offsets.size() > 0) {
      DCHECK_EQ(traf.auxiliary_offset.offsets.size(), 1u);
      DCHECK(!traf.sample_encryption.sample_encryption_entries.empty());

      next_traf_position += traf.box_size();
      // SampleEncryption 'senc' box should be the last box in 'traf'.
      // |auxiliary_offset| should point to the data of SampleEncryption.
      traf.auxiliary_offset.offsets[0] =
          next_traf_position - traf.sample_encryption.box_size() +
          traf.sample_encryption.HeaderSize() +
          sizeof(uint32_t);  // for sample count field in 'senc'
    }
    traf.runs[0].data_offset = data_offset + mdat.data_size;
    mdat.data_size += static_cast<uint32_t>(fragmenters_[i]->data()->Size());
  }

  // Generate segment reference.
  sidx_->references.resize(sidx_->references.size() + 1);
  fragmenters_[GetReferenceStreamId()]->GenerateSegmentReference(
      &sidx_->references[sidx_->references.size() - 1]);
  sidx_->references[sidx_->references.size() - 1].referenced_size =
      data_offset + mdat.data_size;

  // Write the fragment to buffer.
  moof_->Write(fragment_buffer_.get());
  mdat.WriteHeader(fragment_buffer_.get());
  for (const std::unique_ptr<Fragmenter>& fragmenter : fragmenters_)
    fragment_buffer_->AppendBuffer(*fragmenter->data());

  // Increase sequence_number for next fragment.
  ++moof_->header.sequence_number;

  for (std::unique_ptr<Fragmenter>& fragmenter : fragmenters_)
    fragmenter->ClearFragmentFinalized();
  if (!segment_info->is_subsegment) {
    Status status = DoFinalizeSegment();
    // Reset segment information to initial state.
    sidx_->references.clear();
    return status;
  }
  return Status::OK;
}

uint32_t Segmenter::GetReferenceTimeScale() const {
  return moov_->header.timescale;
}

double Segmenter::GetDuration() const {
  if (moov_->header.timescale == 0) {
    // Handling the case where this is not properly initialized.
    return 0.0;
  }

  return static_cast<double>(moov_->header.duration) / moov_->header.timescale;
}

void Segmenter::UpdateProgress(uint64_t progress) {
  accumulated_progress_ += progress;

  if (!progress_listener_) return;
  if (progress_target_ == 0) return;
  // It might happen that accumulated progress exceeds progress_target due to
  // computation errors, e.g. rounding error. Cap it so it never reports > 100%
  // progress.
  if (accumulated_progress_ >= progress_target_) {
    progress_listener_->OnProgress(1.0);
  } else {
    progress_listener_->OnProgress(static_cast<double>(accumulated_progress_) /
                                   progress_target_);
  }
}

void Segmenter::SetComplete() {
  if (!progress_listener_) return;
  progress_listener_->OnProgress(1.0);
}

uint32_t Segmenter::GetReferenceStreamId() {
  DCHECK(sidx_);
  return sidx_->reference_id - 1;
}

void Segmenter::FinalizeFragmentForKeyRotation(
    size_t stream_id,
    bool fragment_encrypted,
    const EncryptionConfig& encryption_config) {
  const std::vector<ProtectionSystemSpecificInfo>& system_info =
      encryption_config.key_system_info;
  moof_->pssh.resize(system_info.size());
  for (size_t i = 0; i < system_info.size(); i++)
    moof_->pssh[i].raw_box = system_info[i].CreateBox();

  // Skip the following steps if the current fragment is not going to be
  // encrypted. 'pssh' box needs to be included in the fragment, which is
  // performed above, regardless of whether the fragment is encrypted. This is
  // necessary for two reasons: 1) Requesting keys before reaching encrypted
  // content avoids playback delay due to license requests; 2) In Chrome, CDM
  // must be initialized before starting the playback and CDM can only be
  // initialized with a valid 'pssh'.
  if (!fragment_encrypted)
    return;

  DCHECK_LT(stream_id, moof_->tracks.size());
  TrackFragment& traf = moof_->tracks[stream_id];
  traf.sample_group_descriptions.resize(traf.sample_group_descriptions.size() +
                                        1);
  SampleGroupDescription& sample_group_description =
      traf.sample_group_descriptions.back();
  sample_group_description.grouping_type = FOURCC_seig;

  sample_group_description.cenc_sample_encryption_info_entries.resize(1);
  CencSampleEncryptionInfoEntry& sample_group_entry =
      sample_group_description.cenc_sample_encryption_info_entries.back();
  sample_group_entry.is_protected = 1;
  sample_group_entry.per_sample_iv_size = encryption_config.per_sample_iv_size;
  sample_group_entry.constant_iv = encryption_config.constant_iv;
  sample_group_entry.crypt_byte_block = encryption_config.crypt_byte_block;
  sample_group_entry.skip_byte_block = encryption_config.skip_byte_block;
  sample_group_entry.key_id = encryption_config.key_id;
}

}  // namespace mp4
}  // namespace media
}  // namespace shaka
