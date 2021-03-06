#!/bin/sh
# $FreeBSD: releng/10.1/tools/regression/pjdfstest/tests/mkfifo/02.t 211178 2010-08-11 16:33:17Z pjd $

desc="mkfifo returns ENAMETOOLONG if a component of a pathname exceeded {NAME_MAX} characters"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..4"

nx=`namegen_max`
nxx="${nx}x"

expect 0 mkfifo ${nx} 0644
expect fifo,0644 stat ${nx} type,mode
expect 0 unlink ${nx}
expect ENAMETOOLONG mkfifo ${nxx} 0644
