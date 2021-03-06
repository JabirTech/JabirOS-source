# Common defines for all of /sys/boot/i386/
#
# $FreeBSD: releng/10.1/sys/boot/i386/Makefile.inc 260291 2014-01-04 22:00:07Z dim $

BINDIR?=	/boot

LOADER_ADDRESS?=0x200000
CFLAGS+=	-march=i386 -ffreestanding
CFLAGS.gcc+=	-mpreferred-stack-boundary=2
CFLAGS+=	-mno-mmx -mno-3dnow -mno-sse -mno-sse2 -mno-sse3 -msoft-float
LDFLAGS+=	-nostdlib

.if ${MACHINE_CPUARCH} == "amd64"
CFLAGS+=	-m32
ACFLAGS+=	-m32
# LD_FLAGS is passed directly to ${LD}, not via ${CC}:
LD_FLAGS+=	-m elf_i386_fbsd
AFLAGS+=	--32
.endif

# BTX components
.if exists(${.OBJDIR}/../btx)
BTXDIR=		${.OBJDIR}/../btx
.else
BTXDIR=		${.CURDIR}/../btx
.endif
BTXLDR=		${BTXDIR}/btxldr/btxldr
BTXKERN=	${BTXDIR}/btx/btx
BTXCRT=		${BTXDIR}/lib/crt0.o

.include "../Makefile.inc"
