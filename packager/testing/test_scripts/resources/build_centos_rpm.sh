#!/bin/bash -xe

OS_VERSION=$1

# Clean the yum cache and install dependencies
yum -y clean all
yum -y clean expire-cache
yum install -y git rpm-build python curl gcc-c++ findutils bzip2 ncurses-libs ncurses-devel clang

# Prepare the RPM environment
mkdir -p /tmp/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

cp /resources/shaka-packager.spec /tmp/rpmbuild/SPECS
# Build the RPM
rpmbuild --define '_topdir /tmp/rpmbuild' -ba /tmp/rpmbuild/SPECS/shaka-packager.spec

# After building the RPM, try to install it
# Fix the lock file error on EL7.  /var/lock is a symlink to /var/run/lock
mkdir -p /var/run/lock

yum localinstall -y /tmp/rpmbuild/RPMS/x86_64/shaka-packager* 

# Run unit tests

# Copy the rpm to the deploy directory
cp /tmp/rpmbuild/RPMS/x86_64/shaka-packager* /artifacts
