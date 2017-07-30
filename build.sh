#!/bin/sh
#
# clearram -- clear system RAM and reboot on demand (for zubwolf)
# Copyright (C) 2017 by Lucía Andrea Illanes Albornoz <lucia@luciaillanes.de>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#

set -o errexit;
help() { echo "${0} -b[uild] -c[lean] [-d[ebug] [<breakpoint>]] -h[elp] -r[un] -v[nc]"; exit 1; };
unset BFLAG CFLAG DFLAG RFLAG VFLAG QEMU_ARGS_EXTRA QEMU_PID VNC_PID;
UNAME_SYS="$(uname -s)";
if [ ${#} -eq 0 ]; then
	help;
fi;
while [ ${#} -gt 0 ]; do
case "${1}" in
-b*) BFLAG=1; ;;
-c*) CFLAG=1; ;;
-d*) DFLAG=1; [ -n "${2##-*}" ] && { DARG="${2}"; shift; }; ;;
-r*) RFLAG=1; ;;
-v*) VFLAG=1; ;;
*) help; ;;
esac; shift; done
if [ ${BFLAG:-0} -eq 1 ]; then
	if [ ${CFLAG:-0} -eq 1 ]; then
		make -f "Makefile.${UNAME_SYS}" clean;
	fi;
	make -f "Makefile.${UNAME_SYS}" ${DFLAG:+DEBUG=1};
fi;
if [ ${RFLAG:-0} -eq 1 ]; then
	case "${UNAME_SYS}" in
	Linux)
		cd build-Linux;
		cp -af clearram-Linux.ko rootfs/root;
		(cd rootfs && find | cpio -H newc -R root -o) > rootfs.cpio;
		QEMU_ARGS_EXTRA="									\
		-append		noapic									\
		-initrd		rootfs.cpio								\
		-kernel		bzImage									\
		-object		memory-backend-file,id=mem,size=4096M,mem-path=/dev/hugepages,share=on	\
		";
		;;
	FreeBSD)
		echo not supported; exit 1;
		;;
	esac;
	qemu-system-x86_64										\
		${QEMU_ARGS_EXTRA}									\
		-m		4096									\
		-numa		node,mem=2048M,nodeid=0							\
		-numa		node,mem=2048M,nodeid=1							\
		-s											\
		-smp		2,cores=2,threads=1,sockets=1						\
		-vnc		127.0.0.1:0								\
		&
	QEMU_PID="${!}";
	if [ ${VFLAG:-0} -eq 1 ]; then
		sleep 1; echo 1 second...;
		vncviewer	"vnc://127.0.0.1:5900" &
		VNC_PID="${!}";
	fi;
	set +o errexit;
	if [ ${DFLAG:-0} -eq 1 ]; then
		sleep 1; echo 2 seconds..;
		sleep 1; echo 1 second...;
		case "${UNAME_SYS}" in
		Linux)
		gdb vmlinux										\
			-ex	"set breakpoint pending on"						\
			-ex	"target remote 127.0.0.1:1234"						\
			-ex	"lx-symbols"								\
			-ex	"break ${DARG}"								\
			-ex	"continue"								\
			-ex	"layout src"								\
			; ;;
		FreeBSD)
			echo not supported; exit 1;
			;;
		esac;
	fi;
	if [ ${VFLAG:-0} -eq 1 ]; then
		wait ${VNC_PID};
	elif [ ${DFLAG:-0} -eq 0 ]; then
		wait ${QEMU_PID};
	fi;
fi;
for KILL_PID in ${QEMU_PID} ${VNC_PID}; do
	kill "${KILL_PID}" 2>/dev/null; done;

# vim:fileencoding=utf-8 foldmethod=marker noexpandtab sw=8 ts=8 tw=120
