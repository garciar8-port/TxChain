#!/bin/bash
# Build the EE 450 submission tarball for TxChain.
#
#   Usage:  ./package.sh <uscusername>
#   Output: ee450_garcia_rodrigo_<uscusername>.tar.gz
#
# The tarball contains ONLY the files the grader expects: the six source files,
# the Makefile, and the README. Block files are intentionally excluded (the
# grader places their own), as are the project instructions, executables, and
# any git/editor cruft.

set -e

USC="$1"
if [ -z "$USC" ]; then
    echo "Usage: ./package.sh <uscusername>   (e.g. ./package.sh garciar8)"
    exit 1
fi

NAME="ee450_garcia_rodrigo_${USC}"
STAGE="/tmp/${NAME}"

# Files that belong in the submission.
FILES="serverM.cpp serverA.cpp serverB.cpp serverC.cpp client.cpp monitor.cpp Makefile README.md"

# Make sure everything compiles before packaging.
make clean >/dev/null 2>&1 || true
make all
make clean >/dev/null 2>&1

# Stage a clean copy and build a flat tarball (matches the project's
# `tar cvf ... *` instruction), then gzip it.
rm -rf "$STAGE" "${NAME}.tar" "${NAME}.tar.gz"
mkdir -p "$STAGE"
cp $FILES "$STAGE"/
( cd "$STAGE" && tar cvf "${NAME}.tar" * && gzip "${NAME}.tar" )
mv "$STAGE/${NAME}.tar.gz" .
rm -rf "$STAGE"

echo
echo "Created ${NAME}.tar.gz — contents:"
tar tzf "${NAME}.tar.gz"
