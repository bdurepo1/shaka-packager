# RPM spec file for shaka-packager

Name: shaka-packager
Version: 1.6.2
Release: 1%{?gitrev:.%{gitrev}git}%{?dist}
Summary: A media packaging and development framework for VOD and Live DASH and HLS applications.
BuildArch: x86_64

Group: Applications/System
License: BSD 3-clause "New" or "Revised" License
URL: https://github.com/google/shaka-packager
Distribution: CentOS 7
Vendor: Google, Inc.
Packager: Brandon A. Durepo <bdurepo1@users.noreply.github.com>

# Required dependencies

Requires: git
Requires: python
Requires: curl
Requires: gcc-c++
Requires: findutils
Requires: bzip2
Requires: ncurses-libs
Requires: ncurses-devel
BuildRequires: clang

%description
%{summary}

%prep
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git /tmp/depot_tools
export PATH=/tmp/depot_tools/:$PATH
gclient config https://www.github.com/google/shaka-packager.git --name=src --unmanaged
gclient sync  --no-history

%build
export PATH=/tmp/depot_tools/:$PATH
cd src/ && ninja -C out/Release

%install
cd src/out/Release
mkdir -p $RPM_BUILD_ROOT/usr/bin && \ 
cp -p packager protoc mpd_generator $RPM_BUILD_ROOT/usr/bin/

%files
%defattr(-, root, root)
/usr/bin/packager

%clean
rm -Rf /tmp/depot_tools
%changelog

