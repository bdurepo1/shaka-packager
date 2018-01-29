#!/bin/bash

# This script starts docker and systemd
export PATH=/tmp/depot_tools:$PATH
export BUILD_DIR=`pwd`/packager/testing/test_scripts

# Create a build container
# Build will execute in the container by mapping repository volumes

if [ ${OS_TYPE} -eq "centos" ]; then
	docker run --privileged  -d -ti -e "container=docker" \
		-v /tmp/depot_tools:/depot_tools:rw \
		-v $BUILD_DIR/artifacts:/artifacts:rw \
       		-v $BUILD_DIR/resources:/resources:rw \
		centos:centos${OS_VERSION} /usr/sbin/init
fi

DOCKER_CONTAINER_ID=$(docker ps | grep centos | awk '{print $1}')
docker logs $DOCKER_CONTAINER_ID

# Execute the build and test script in the build container
if [ ${OS_TYPE} -eq "centos" ]; then
	docker exec -ti $DOCKER_CONTAINER_ID \
		/bin/bash -xec "bash -xe /resources/build_centos_rpm.sh"
fi

# Do cleanup
docker ps -a
docker stop $DOCKER_CONTAINER_ID
docker rm -v $DOCKER_CONTAINER_ID
