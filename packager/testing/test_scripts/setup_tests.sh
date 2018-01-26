#!/bin/bash

# This script starts docker and systemd

# Version of CentOS/RHEL
el_version=$1

# Path to test directory
test_dir=packager/testing/test_scripts

# Run tests in container by mapping repository volumes
docker run --privileged -d -ti -e "container=docker" \
       	-v /sys/fs/cgroup:/sys/fs/cgroup \
	-v `pwd`:/shaka-packager:r \
	-v ../depot_tools:/depot_tools:r \
	-v deploy:/deploy:rw \
       	-v `pwd`/$test_dir/artifacts:/artifacts:r \
	centos:centos${OS_VERSION} /usr/sbin/init

DOCKER_CONTAINER_ID=$(docker ps | grep centos | awk '{print $1}')

docker logs $DOCKER_CONTAINER_ID

# Execute the build and test script
docker exec -ti $DOCKER_CONTAINER_ID \
	/bin/bash -xec "bash -xe /shaka-packager/$test_dir/test_centos.sh ${OS_VERSION};
  echo -ne \"------\nEND SHAKA-PACKAGER TESTS\n\";"

# Do cleanup
docker ps -a
docker stop $DOCKER_CONTAINER_ID
docker rm -v $DOCKER_CONTAINER_ID
