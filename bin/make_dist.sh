#!/bin/bash

VERSION=`git show HEAD:CMakeLists.txt | grep "SET(DARNER_VERSION" | awk -F"[ \)]" '{print $2}'`

git archive --prefix darner-$VERSION/ master | gzip > darner-$VERSION.tar.gz
