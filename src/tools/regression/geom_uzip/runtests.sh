#!/bin/sh
#
# $FreeBSD: releng/10.1/tools/regression/geom_uzip/runtests.sh 133641 2004-08-13 09:53:52Z fjoe $
#

dir=`dirname $0`

for ts in `dirname $0`/test-*.sh; do
	sh $ts
done
