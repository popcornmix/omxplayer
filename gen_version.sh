#!/bin/bash
#
# Author:
#     Sergio Conde <skgsergio@gmail.com>
#
# License:
#     This script is part of omxplayer and it should be
# distributed under the same license.
#

date=$(date -R 2> /dev/null)
hash="UNKNOWN"
branch="UNKNOWN"
repo="UNKNOWN"

ref=$(git symbolic-ref -q HEAD 2> /dev/null)
if [ x"$?" = x"0" ]; then
    hash=$(git rev-parse --short $ref 2> /dev/null)
    branch=${ref#refs/heads/}

    upstream=$(git for-each-ref --format='%(upstream:short)' $ref 2> /dev/null)
    if [ x"$upstream" != x"" ]; then
	repo=$(git config remote.${upstream%/$branch}.url)
    fi
fi

echo "#ifndef __VERSION_H__"
echo "#define __VERSION_H__"
echo "#define VERSION_DATE \"$date\""
echo "#define VERSION_HASH \"$hash\""
echo "#define VERSION_BRANCH \"$branch\""
echo "#define VERSION_REPO \"$repo\""
echo "#endif"
