// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "packager/file/file_test_util.h"
#include "packager/hls/base/media_playlist.h"
#include "packager/version/version.h"

namespace shaka {
namespace hls {

using ::testing::_;
using ::testing::ReturnArg;

namespace {

const char kDefaultPlaylistFileName[] = "default_playlist.m3u8";
const double kTimeShiftBufferDepth = 20;
const uint64_t kTimeScale = 90000;
const uint64_t kMBytes = 1000000;
const uint64_t kZeroByteOffset = 0;

MATCHER_P(MatchesString, expected_string, "") {
  const std::string arg_string(static_cast<const char*>(arg));
  *result_listener << "which is " << arg_string.size()
                   << " long and the content is " << arg_string;
  return expected_string == std::string(static_cast<const char*>(arg));
}

}  // namespace

class MediaPlaylistTest : public ::testing::Test {
 protected:
  MediaPlaylistTest() : MediaPlaylistTest(HlsPlaylistType::kVod) {}

  MediaPlaylistTest(HlsPlaylistType type)
      : default_file_name_(kDefaultPlaylistFileName),
        default_name_("default_name"),
        default_group_id_("default_group_id"),
        media_playlist_(type,
                        kTimeShiftBufferDepth,
                        default_file_name_,
                        default_name_,
                        default_group_id_) {}

  void SetUp() override {
    SetPackagerVersionForTesting("test");

    MediaInfo::VideoInfo* video_info =
        valid_video_media_info_.mutable_video_info();
    video_info->set_codec("avc1");
    video_info->set_time_scale(kTimeScale);
    video_info->set_frame_duration(3000);
    video_info->set_width(1280);
    video_info->set_height(720);
    video_info->set_pixel_width(1);
    video_info->set_pixel_height(1);

    valid_video_media_info_.set_reference_time_scale(kTimeScale);
  }

  const std::string default_file_name_;
  const std::string default_name_;
  const std::string default_group_id_;
  MediaPlaylist media_playlist_;

  MediaInfo valid_video_media_info_;
};

class MediaPlaylistMultiSegmentTest : public MediaPlaylistTest {
 protected:
  MediaPlaylistMultiSegmentTest() : MediaPlaylistTest() {}
  // This constructor is for Live and Event playlist tests.
  MediaPlaylistMultiSegmentTest(HlsPlaylistType type)
      : MediaPlaylistTest(type) {}

  void SetUp() override {
    MediaPlaylistTest::SetUp();
    // This is just set to be consistent with the multisegment format and used
    // as a switch in MediaPlaylist.
    // The template string doesn't really matter.
    valid_video_media_info_.set_segment_template("file$Number$.ts");
  }
};

class MediaPlaylistSingleSegmentTest : public MediaPlaylistTest {
 protected:
  MediaPlaylistSingleSegmentTest() : MediaPlaylistTest() {}
};

// Verify that SetMediaInfo() fails if timescale is not present.
TEST_F(MediaPlaylistMultiSegmentTest, NoTimeScale) {
  MediaInfo media_info;
  EXPECT_FALSE(media_playlist_.SetMediaInfo(media_info));
}

// The current implementation only handles video and audio.
TEST_F(MediaPlaylistMultiSegmentTest, NoAudioOrVideo) {
  MediaInfo media_info;
  media_info.set_reference_time_scale(kTimeScale);
  MediaInfo::TextInfo* text_info = media_info.mutable_text_info();
  text_info->set_format("vtt");
  EXPECT_FALSE(media_playlist_.SetMediaInfo(media_info));
}

TEST_F(MediaPlaylistMultiSegmentTest, SetMediaInfo) {
  MediaInfo media_info;
  media_info.set_reference_time_scale(kTimeScale);
  MediaInfo::VideoInfo* video_info = media_info.mutable_video_info();
  video_info->set_width(1280);
  video_info->set_height(720);
  EXPECT_TRUE(media_playlist_.SetMediaInfo(media_info));
}

// Verify that AddSegment works (not crash).
TEST_F(MediaPlaylistMultiSegmentTest, AddSegment) {
  ASSERT_TRUE(media_playlist_.SetMediaInfo(valid_video_media_info_));
  media_playlist_.AddSegment("file1.ts", 900000, 0, kZeroByteOffset, 1000000);
}

// Verify that it returns the display resolution.
TEST_F(MediaPlaylistMultiSegmentTest, GetDisplayResolution) {
  // A real case using sintel video.
  MediaInfo media_info;
  media_info.set_reference_time_scale(kTimeScale);
  MediaInfo::VideoInfo* video_info = media_info.mutable_video_info();
  video_info->set_width(1920);
  video_info->set_height(818);
  video_info->set_pixel_width(1636);
  video_info->set_pixel_height(1635);
  ASSERT_TRUE(media_playlist_.SetMediaInfo(media_info));
  uint32_t width = 0;
  uint32_t height = 0;
  EXPECT_TRUE(media_playlist_.GetDisplayResolution(&width, &height));
  EXPECT_EQ(1921u, width);
  EXPECT_EQ(818u, height);
}

TEST_F(MediaPlaylistSingleSegmentTest, InitRange) {
  const std::string kExpectedOutput =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:0\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXT-X-MAP:URI=\"file.mp4\",BYTERANGE=\"501@0\"\n"
      "#EXT-X-ENDLIST\n";
  valid_video_media_info_.set_media_file_name("file.mp4");
  valid_video_media_info_.mutable_init_range()->set_begin(0);
  valid_video_media_info_.mutable_init_range()->set_end(500);

  ASSERT_TRUE(media_playlist_.SetMediaInfo(valid_video_media_info_));
  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_.WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

TEST_F(MediaPlaylistSingleSegmentTest, InitRangeWithOffset) {
  const std::string kExpectedOutput =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:0\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXT-X-MAP:URI=\"file.mp4\",BYTERANGE=\"485@16\"\n"
      "#EXT-X-ENDLIST\n";
  valid_video_media_info_.set_media_file_name("file.mp4");
  valid_video_media_info_.mutable_init_range()->set_begin(16);
  valid_video_media_info_.mutable_init_range()->set_end(500);

  ASSERT_TRUE(media_playlist_.SetMediaInfo(valid_video_media_info_));
  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_.WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

// Closest to the normal use case where there is an init range and then
// subsegment ranges. There is index range between the subsegment and init range.
TEST_F(MediaPlaylistSingleSegmentTest, AddSegmentByteRange) {
  const std::string kExpectedOutput =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:10\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXT-X-MAP:URI=\"file.mp4\",BYTERANGE=\"501@0\"\n"
      "#EXTINF:10.000,\n"
      "#EXT-X-BYTERANGE:1000000@1000\n"
      "file.mp4\n"
      "#EXTINF:10.000,\n"
      "#EXT-X-BYTERANGE:2000000\n"
      "file.mp4\n"
      "#EXT-X-ENDLIST\n";
  valid_video_media_info_.set_media_file_name("file.mp4");
  valid_video_media_info_.mutable_init_range()->set_begin(0);
  valid_video_media_info_.mutable_init_range()->set_end(500);

  ASSERT_TRUE(media_playlist_.SetMediaInfo(valid_video_media_info_));
  media_playlist_.AddSegment("file.mp4", 0, 10 * kTimeScale, 1000,
                             1 * kMBytes);
  media_playlist_.AddSegment("file.mp4", 10 * kTimeScale, 10 * kTimeScale,
                             1001000, 2 * kMBytes);

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_.WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

// Verify that AddEncryptionInfo works (not crash).
TEST_F(MediaPlaylistMultiSegmentTest, AddEncryptionInfo) {
  ASSERT_TRUE(media_playlist_.SetMediaInfo(valid_video_media_info_));
  media_playlist_.AddEncryptionInfo(MediaPlaylist::EncryptionMethod::kSampleAes,
                                    "http://example.com", "", "0xabcedf", "",
                                    "");
}

TEST_F(MediaPlaylistMultiSegmentTest, WriteToFile) {
  ASSERT_TRUE(media_playlist_.SetMediaInfo(valid_video_media_info_));
  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:0\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXT-X-ENDLIST\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_.WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

// If bitrate (bandwidth) is not set in the MediaInfo, use it.
TEST_F(MediaPlaylistMultiSegmentTest, UseBitrateInMediaInfo) {
  valid_video_media_info_.set_bandwidth(8191);
  ASSERT_TRUE(media_playlist_.SetMediaInfo(valid_video_media_info_));
  EXPECT_EQ(8191u, media_playlist_.Bitrate());
}

// If bitrate (bandwidth) is not set in the MediaInfo, then calculate from the
// segments.
TEST_F(MediaPlaylistMultiSegmentTest, GetBitrateFromSegments) {
  valid_video_media_info_.clear_bandwidth();
  ASSERT_TRUE(media_playlist_.SetMediaInfo(valid_video_media_info_));

  media_playlist_.AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                             kMBytes);
  media_playlist_.AddSegment("file2.ts", 10 * kTimeScale, 20 * kTimeScale,
                             kZeroByteOffset, 5 * kMBytes);

  // Max bitrate is 2000Kb/s.
  EXPECT_EQ(2000000u, media_playlist_.Bitrate());
}

TEST_F(MediaPlaylistMultiSegmentTest, GetLongestSegmentDuration) {
  ASSERT_TRUE(media_playlist_.SetMediaInfo(valid_video_media_info_));

  media_playlist_.AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                             kMBytes);
  media_playlist_.AddSegment("file2.ts", 10 * kTimeScale, 30 * kTimeScale,
                             kZeroByteOffset, 5 * kMBytes);
  media_playlist_.AddSegment("file3.ts", 40 * kTimeScale, 14 * kTimeScale,
                             kZeroByteOffset, 3 * kMBytes);

  EXPECT_NEAR(30.0, media_playlist_.GetLongestSegmentDuration(), 0.01);
}

TEST_F(MediaPlaylistMultiSegmentTest, SetTargetDuration) {
  ASSERT_TRUE(media_playlist_.SetMediaInfo(valid_video_media_info_));
  media_playlist_.SetTargetDuration(20);
  const std::string kExpectedOutput =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:20\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXT-X-ENDLIST\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_.WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

TEST_F(MediaPlaylistMultiSegmentTest, WriteToFileWithSegments) {
  valid_video_media_info_.set_reference_time_scale(90000);
  ASSERT_TRUE(media_playlist_.SetMediaInfo(valid_video_media_info_));

  media_playlist_.AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                             kMBytes);
  media_playlist_.AddSegment("file2.ts", 10 * kTimeScale, 30 * kTimeScale,
                             kZeroByteOffset, 5 * kMBytes);
  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:30\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXTINF:10.000,\n"
      "file1.ts\n"
      "#EXTINF:30.000,\n"
      "file2.ts\n"
      "#EXT-X-ENDLIST\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_.WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

TEST_F(MediaPlaylistMultiSegmentTest, WriteToFileWithEncryptionInfo) {
  valid_video_media_info_.set_reference_time_scale(90000);
  ASSERT_TRUE(media_playlist_.SetMediaInfo(valid_video_media_info_));

  media_playlist_.AddEncryptionInfo(MediaPlaylist::EncryptionMethod::kSampleAes,
                                    "http://example.com", "", "0x12345678",
                                    "com.widevine", "1/2/4");
  media_playlist_.AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                             kMBytes);
  media_playlist_.AddSegment("file2.ts", 10 * kTimeScale, 30 * kTimeScale,
                             kZeroByteOffset, 5 * kMBytes);
  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:30\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXT-X-KEY:METHOD=SAMPLE-AES,"
      "URI=\"http://example.com\",IV=0x12345678,KEYFORMATVERSIONS=\"1/2/4\","
      "KEYFORMAT=\"com.widevine\"\n"
      "#EXTINF:10.000,\n"
      "file1.ts\n"
      "#EXTINF:30.000,\n"
      "file2.ts\n"
      "#EXT-X-ENDLIST\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_.WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

TEST_F(MediaPlaylistMultiSegmentTest, WriteToFileWithEncryptionInfoEmptyIv) {
  valid_video_media_info_.set_reference_time_scale(90000);
  ASSERT_TRUE(media_playlist_.SetMediaInfo(valid_video_media_info_));

  media_playlist_.AddEncryptionInfo(MediaPlaylist::EncryptionMethod::kSampleAes,
                                    "http://example.com", "", "", "com.widevine",
                                    "");
  media_playlist_.AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                             kMBytes);
  media_playlist_.AddSegment("file2.ts", 10 * kTimeScale, 30 * kTimeScale,
                             kZeroByteOffset, 5 * kMBytes);
  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:30\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXT-X-KEY:METHOD=SAMPLE-AES,"
      "URI=\"http://example.com\",KEYFORMAT=\"com.widevine\"\n"
      "#EXTINF:10.000,\n"
      "file1.ts\n"
      "#EXTINF:30.000,\n"
      "file2.ts\n"
      "#EXT-X-ENDLIST\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_.WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

// Verify that EXT-X-DISCONTINUITY is inserted before EXT-X-KEY.
TEST_F(MediaPlaylistMultiSegmentTest, WriteToFileWithClearLead) {
  valid_video_media_info_.set_reference_time_scale(90000);
  ASSERT_TRUE(media_playlist_.SetMediaInfo(valid_video_media_info_));

  media_playlist_.AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                             kMBytes);

  media_playlist_.AddEncryptionInfo(MediaPlaylist::EncryptionMethod::kSampleAes,
                                    "http://example.com", "", "0x12345678",
                                    "com.widevine", "1/2/4");
  media_playlist_.AddSegment("file2.ts", 10 * kTimeScale, 30 * kTimeScale,
                             kZeroByteOffset, 5 * kMBytes);
  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:30\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXTINF:10.000,\n"
      "file1.ts\n"
      "#EXT-X-DISCONTINUITY\n"
      "#EXT-X-KEY:METHOD=SAMPLE-AES,"
      "URI=\"http://example.com\",IV=0x12345678,KEYFORMATVERSIONS=\"1/2/4\","
      "KEYFORMAT=\"com.widevine\"\n"
      "#EXTINF:30.000,\n"
      "file2.ts\n"
      "#EXT-X-ENDLIST\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_.WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

TEST_F(MediaPlaylistMultiSegmentTest, GetLanguage) {
  MediaInfo media_info;
  media_info.set_reference_time_scale(kTimeScale);

  // Check conversions from long to short form.
  media_info.mutable_audio_info()->set_language("eng");
  ASSERT_TRUE(media_playlist_.SetMediaInfo(media_info));
  EXPECT_EQ("en", media_playlist_.GetLanguage());  // short form

  media_info.mutable_audio_info()->set_language("eng-US");
  ASSERT_TRUE(media_playlist_.SetMediaInfo(media_info));
  EXPECT_EQ("en-US", media_playlist_.GetLanguage());  // region preserved

  media_info.mutable_audio_info()->set_language("apa");
  ASSERT_TRUE(media_playlist_.SetMediaInfo(media_info));
  EXPECT_EQ("apa", media_playlist_.GetLanguage());  // no short form exists
}

TEST_F(MediaPlaylistMultiSegmentTest, GetNumChannels) {
  MediaInfo media_info;
  media_info.set_reference_time_scale(kTimeScale);

  // Returns 0 by default if not audio.
  EXPECT_EQ(0, media_playlist_.GetNumChannels());

  media_info.mutable_audio_info()->set_num_channels(2);
  ASSERT_TRUE(media_playlist_.SetMediaInfo(media_info));
  EXPECT_EQ(2, media_playlist_.GetNumChannels());

  media_info.mutable_audio_info()->set_num_channels(8);
  ASSERT_TRUE(media_playlist_.SetMediaInfo(media_info));
  EXPECT_EQ(8, media_playlist_.GetNumChannels());
}

TEST_F(MediaPlaylistMultiSegmentTest, InitSegment) {
  valid_video_media_info_.set_reference_time_scale(90000);
  valid_video_media_info_.set_init_segment_name("init_segment.mp4");
  ASSERT_TRUE(media_playlist_.SetMediaInfo(valid_video_media_info_));

  media_playlist_.AddSegment("file1.mp4", 0, 10 * kTimeScale, kZeroByteOffset,
                             kMBytes);
  media_playlist_.AddSegment("file2.mp4", 10 * kTimeScale, 30 * kTimeScale,
                             kZeroByteOffset, 5 * kMBytes);

  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version test\n"
      "#EXT-X-TARGETDURATION:30\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXT-X-MAP:URI=\"init_segment.mp4\"\n"
      "#EXTINF:10.000,\n"
      "file1.mp4\n"
      "#EXTINF:30.000,\n"
      "file2.mp4\n"
      "#EXT-X-ENDLIST\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_.WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

// Verify that kSampleAesCenc is handled correctly.
TEST_F(MediaPlaylistMultiSegmentTest, SampleAesCenc) {
  valid_video_media_info_.set_reference_time_scale(90000);
  ASSERT_TRUE(media_playlist_.SetMediaInfo(valid_video_media_info_));

  media_playlist_.AddEncryptionInfo(
      MediaPlaylist::EncryptionMethod::kSampleAesCenc, "http://example.com", "",
      "0x12345678", "com.widevine", "1/2/4");

  media_playlist_.AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                             kMBytes);
  media_playlist_.AddSegment("file2.ts", 10 * kTimeScale, 30 * kTimeScale,
                             kZeroByteOffset, 5 * kMBytes);
  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:30\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXT-X-KEY:METHOD=SAMPLE-AES-CENC,"
      "URI=\"http://example.com\",IV=0x12345678,KEYFORMATVERSIONS=\"1/2/4\","
      "KEYFORMAT=\"com.widevine\"\n"
      "#EXTINF:10.000,\n"
      "file1.ts\n"
      "#EXTINF:30.000,\n"
      "file2.ts\n"
      "#EXT-X-ENDLIST\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_.WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

TEST_F(MediaPlaylistMultiSegmentTest, MultipleEncryptionInfo) {
  ASSERT_TRUE(media_playlist_.SetMediaInfo(valid_video_media_info_));

  media_playlist_.AddEncryptionInfo(MediaPlaylist::EncryptionMethod::kSampleAes,
                                    "http://example.com", "", "0x12345678",
                                    "com.widevine", "1/2/4");

  media_playlist_.AddEncryptionInfo(
      MediaPlaylist::EncryptionMethod::kSampleAes, "http://mydomain.com",
      "0xfedc", "0x12345678", "com.widevine.someother", "1");

  media_playlist_.AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                             kMBytes);
  media_playlist_.AddSegment("file2.ts", 10 * kTimeScale, 30 * kTimeScale,
                             kZeroByteOffset, 5 * kMBytes);
  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:30\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXT-X-KEY:METHOD=SAMPLE-AES,"
      "URI=\"http://example.com\",IV=0x12345678,KEYFORMATVERSIONS=\"1/2/4\","
      "KEYFORMAT=\"com.widevine\"\n"
      "#EXT-X-KEY:METHOD=SAMPLE-AES,"
      "URI=\"http://mydomain.com\",KEYID=0xfedc,IV=0x12345678,"
      "KEYFORMATVERSIONS=\"1\","
      "KEYFORMAT=\"com.widevine.someother\"\n"
      "#EXTINF:10.000,\n"
      "file1.ts\n"
      "#EXTINF:30.000,\n"
      "file2.ts\n"
      "#EXT-X-ENDLIST\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_.WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

class LiveMediaPlaylistTest : public MediaPlaylistMultiSegmentTest {
 protected:
  LiveMediaPlaylistTest()
      : MediaPlaylistMultiSegmentTest(HlsPlaylistType::kLive) {}
};

TEST_F(LiveMediaPlaylistTest, Basic) {
  ASSERT_TRUE(media_playlist_.SetMediaInfo(valid_video_media_info_));

  media_playlist_.AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                             kMBytes);
  media_playlist_.AddSegment("file2.ts", 10 * kTimeScale, 20 * kTimeScale,
                             kZeroByteOffset, 2 * kMBytes);
  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:20\n"
      "#EXTINF:10.000,\n"
      "file1.ts\n"
      "#EXTINF:20.000,\n"
      "file2.ts\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_.WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

TEST_F(LiveMediaPlaylistTest, TimeShifted) {
  ASSERT_TRUE(media_playlist_.SetMediaInfo(valid_video_media_info_));

  media_playlist_.AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                             kMBytes);
  media_playlist_.AddSegment("file2.ts", 10 * kTimeScale, 20 * kTimeScale,
                             kZeroByteOffset, 2 * kMBytes);
  media_playlist_.AddSegment("file3.ts", 30 * kTimeScale, 20 * kTimeScale,
                             kZeroByteOffset, 2 * kMBytes);
  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:20\n"
      "#EXT-X-MEDIA-SEQUENCE:1\n"
      "#EXTINF:20.000,\n"
      "file2.ts\n"
      "#EXTINF:20.000,\n"
      "file3.ts\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_.WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

TEST_F(LiveMediaPlaylistTest, TimeShiftedWithEncryptionInfo) {
  ASSERT_TRUE(media_playlist_.SetMediaInfo(valid_video_media_info_));

  media_playlist_.AddEncryptionInfo(MediaPlaylist::EncryptionMethod::kSampleAes,
                                    "http://example.com", "", "0x12345678",
                                    "com.widevine", "1/2/4");
  media_playlist_.AddEncryptionInfo(
      MediaPlaylist::EncryptionMethod::kSampleAes, "http://mydomain.com",
      "0xfedc", "0x12345678", "com.widevine.someother", "1");

  media_playlist_.AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                             kMBytes);
  media_playlist_.AddSegment("file2.ts", 10 * kTimeScale, 20 * kTimeScale,
                             kZeroByteOffset, 2 * kMBytes);
  media_playlist_.AddSegment("file3.ts", 30 * kTimeScale, 20 * kTimeScale,
                             kZeroByteOffset, 2 * kMBytes);
  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:20\n"
      "#EXT-X-MEDIA-SEQUENCE:1\n"
      "#EXT-X-KEY:METHOD=SAMPLE-AES,"
      "URI=\"http://example.com\",IV=0x12345678,KEYFORMATVERSIONS=\"1/2/4\","
      "KEYFORMAT=\"com.widevine\"\n"
      "#EXT-X-KEY:METHOD=SAMPLE-AES,"
      "URI=\"http://mydomain.com\",KEYID=0xfedc,IV=0x12345678,"
      "KEYFORMATVERSIONS=\"1\","
      "KEYFORMAT=\"com.widevine.someother\"\n"
      "#EXTINF:20.000,\n"
      "file2.ts\n"
      "#EXTINF:20.000,\n"
      "file3.ts\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_.WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

TEST_F(LiveMediaPlaylistTest, TimeShiftedWithEncryptionInfoShifted) {
  ASSERT_TRUE(media_playlist_.SetMediaInfo(valid_video_media_info_));

  media_playlist_.AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                             kMBytes);

  media_playlist_.AddEncryptionInfo(MediaPlaylist::EncryptionMethod::kSampleAes,
                                    "http://example.com", "", "0x12345678",
                                    "com.widevine", "1/2/4");
  media_playlist_.AddEncryptionInfo(
      MediaPlaylist::EncryptionMethod::kSampleAes, "http://mydomain.com",
      "0xfedc", "0x12345678", "com.widevine.someother", "1");

  media_playlist_.AddSegment("file2.ts", 10 * kTimeScale, 20 * kTimeScale,
                             kZeroByteOffset, 2 * kMBytes);

  media_playlist_.AddEncryptionInfo(MediaPlaylist::EncryptionMethod::kSampleAes,
                                    "http://example.com", "", "0x22345678",
                                    "com.widevine", "1/2/4");
  media_playlist_.AddEncryptionInfo(
      MediaPlaylist::EncryptionMethod::kSampleAes, "http://mydomain.com",
      "0xfedd", "0x22345678", "com.widevine.someother", "1");

  media_playlist_.AddSegment("file3.ts", 30 * kTimeScale, 20 * kTimeScale,
                             kZeroByteOffset, 2 * kMBytes);

  media_playlist_.AddEncryptionInfo(MediaPlaylist::EncryptionMethod::kSampleAes,
                                    "http://example.com", "", "0x32345678",
                                    "com.widevine", "1/2/4");
  media_playlist_.AddEncryptionInfo(
      MediaPlaylist::EncryptionMethod::kSampleAes, "http://mydomain.com",
      "0xfede", "0x32345678", "com.widevine.someother", "1");

  media_playlist_.AddSegment("file4.ts", 50 * kTimeScale, 20 * kTimeScale,
                             kZeroByteOffset, 2 * kMBytes);
  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:20\n"
      "#EXT-X-MEDIA-SEQUENCE:2\n"
      "#EXT-X-DISCONTINUITY-SEQUENCE:1\n"
      "#EXT-X-KEY:METHOD=SAMPLE-AES,"
      "URI=\"http://example.com\",IV=0x22345678,KEYFORMATVERSIONS=\"1/2/4\","
      "KEYFORMAT=\"com.widevine\"\n"
      "#EXT-X-KEY:METHOD=SAMPLE-AES,"
      "URI=\"http://mydomain.com\",KEYID=0xfedd,IV=0x22345678,"
      "KEYFORMATVERSIONS=\"1\","
      "KEYFORMAT=\"com.widevine.someother\"\n"
      "#EXTINF:20.000,\n"
      "file3.ts\n"
      "#EXT-X-KEY:METHOD=SAMPLE-AES,"
      "URI=\"http://example.com\",IV=0x32345678,KEYFORMATVERSIONS=\"1/2/4\","
      "KEYFORMAT=\"com.widevine\"\n"
      "#EXT-X-KEY:METHOD=SAMPLE-AES,"
      "URI=\"http://mydomain.com\",KEYID=0xfede,IV=0x32345678,"
      "KEYFORMATVERSIONS=\"1\","
      "KEYFORMAT=\"com.widevine.someother\"\n"
      "#EXTINF:20.000,\n"
      "file4.ts\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_.WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

class EventMediaPlaylistTest : public MediaPlaylistMultiSegmentTest {
 protected:
  EventMediaPlaylistTest()
      : MediaPlaylistMultiSegmentTest(HlsPlaylistType::kEvent) {}
};

TEST_F(EventMediaPlaylistTest, Basic) {
  ASSERT_TRUE(media_playlist_.SetMediaInfo(valid_video_media_info_));

  media_playlist_.AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                             kMBytes);
  media_playlist_.AddSegment("file2.ts", 10 * kTimeScale, 20 * kTimeScale,
                             kZeroByteOffset, 2 * kMBytes);
  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:20\n"
      "#EXT-X-PLAYLIST-TYPE:EVENT\n"
      "#EXTINF:10.000,\n"
      "file1.ts\n"
      "#EXTINF:20.000,\n"
      "file2.ts\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_.WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

}  // namespace hls
}  // namespace shaka
