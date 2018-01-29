#!/bin/bash

# This script starts docker and systemd
export PATH=/tmp/depot_tools:$PATH

# Create a build container
# Build will execute in the container by mapping repository volumes
docker run --privileged  -d -ti -e "container=docker" \
	-v /tmp/depot_tools:/depot_tools:rw \
	-v `pwd`/artifacts:/artifacts:rw \
       	-v `pwd`/resources:/resources:rw \
	centos:centos7 /usr/sbin/init

DOCKER_CONTAINER_ID=$(docker ps | grep centos | awk '{print $1}')

docker logs $DOCKER_CONTAINER_ID

# Execute the build and test script in the build container
docker exec -ti $DOCKER_CONTAINER_ID \
	/bin/bash -xec "bash -xe /resources/build_centos_rpm.sh"

# Do cleanup
docker ps -a
docker stop $DOCKER_CONTAINER_ID
docker rm -v $DOCKER_CONTAINER_ID
