#!/bin/sh

check_dpkg_installed() {
	echo -n "."
	if [ $(dpkg-query -W -f='${Status}' $1 2>/dev/null | grep -c "ok installed") -eq 0 ];
	then
		MISSING_PKGS="$MISSING_PKGS $1"
	fi
}

echo "Modifying for native build on Debian"

if [ -z `which sudo` ] ; then
    apt-get install -y sudo
fi
    
echo "Checking dpkg database for missing packages"
REQUIRED_PKGS="ca-certificates git-core subversion binutils libasound2-dev libva1 libpcre3-dev libidn11-dev libboost1.50-dev libfreetype6-dev libusb-1.0-0-dev libdbus-1-dev libssl1.0-dev libssh-dev libsmbclient-dev gcc-4.7 g++-4.7 sed pkg-config"
MISSING_PKGS=""
for pkg in $REQUIRED_PKGS
do
	check_dpkg_installed $pkg
done
echo ""
if [ ! -z "$MISSING_PKGS" ]; then
	echo "You are missing required packages."
	echo "Run sudo apt-get update && sudo apt-get install $MISSING_PKGS"
	exit 1
else
	echo "All dependencies met"
fi

if [ -e "patch.flag" ]
then
	echo "Makefiles already patched, nothing to do here"
else
	echo "Patching makefiles..."
	echo "FLOAT=hard

CFLAGS +=  -mfloat-abi=hard -mcpu=arm1176jzf-s -fomit-frame-pointer -mabi=aapcs-linux -mtune=arm1176jzf-s -mfpu=vfp -Wno-psabi -mno-apcs-stack-check -O3 -mstructure-size-boundary=32 -mno-sched-prolog -march=armv6zk `pkg-config dbus-1 --cflags`

BUILDROOT	:=/usr/local/src/omxplayer
TOOLCHAIN	:=/usr/
LD			:= \$(TOOLCHAIN)/bin/ld
CC			:= \$(TOOLCHAIN)/bin/gcc-4.7
CXX       	:= \$(TOOLCHAIN)/bin/g++-4.7
OBJDUMP		:= \$(TOOLCHAIN)/bin/objdump
RANLIB		:= \$(TOOLCHAIN)/bin/ranlib
STRIP		:= \$(TOOLCHAIN)/bin/strip
AR			:= \$(TOOLCHAIN)/bin/ar
CXXCP 		:= \$(CXX) -E

LDFLAGS		+= -L/opt/vc/lib -L/lib -L/usr/lib -lfreetype
INCLUDES	+= -I/opt/vc/include/interface/vcos/pthreads \
			-I/opt/vc/include \
			-I/opt/vc/include/interface/vmcs_host \
			-I/opt/vc/include/interface/vmcs_host/linux \
			-I/usr/lib/arm-linux-gnueabihf/dbus-1.0/include \
			-I/usr/include \
			-I/usr/include/freetype2" > Makefile.include

	sed -i '/--enable-cross-compile \\/d;' Makefile.ffmpeg
	sed -i 's/			--cross-prefix=$(HOST)-//g;' Makefile.ffmpeg

	sed -i 's/$(HOST)-//g;' Makefile.*
	sed -i 's/ -j9//g;' Makefile.*
	sed -i 's/#arm-unknown-linux-gnueabi-strip/arm-unknown-linux-gnueabi-strip/g;' Makefile
	sed -i 's/arm-unknown-linux-gnueabi-strip/strip/g;' Makefile

	cat <<EOF >>Makefile
install:
	cp -r \$(DIST)/* /

uninstall:
	rm -rf /usr/bin/omxplayer
	rm -rf /usr/bin/omxplayer.bin
	rm -rf /usr/lib/omxplayer
	rm -rf /usr/share/doc/omxplayer
	rm -rf /usr/share/man/man1/omxplayer.1
EOF
	touch "patch.flag"
fi

echo "Checking for OMX development headers"
# These can either be supplied by dpkg or via rpi-update.
# First, check dpkg to avoid messing with dpkg-managed files!
REQUIRED_PKGS="libraspberrypi-dev libraspberrypi0 libraspberrypi-bin"
MISSING_PKGS=""
for pkg in $REQUIRED_PKGS
do
	check_dpkg_installed $pkg
done
echo ""
if [ ! -z "$MISSING_PKGS" ]; then
	echo "You are missing required packages."
	echo "Run sudo apt-get update && sudo apt-get install $MISSING_PKGS"
	echo "Alternative: install rpi-update with sudo wget http://goo.gl/1BOfJ -O /usr/local/bin/rpi-update && sudo chmod +x /usr/local/bin/rpi-update && sudo rpi-update"
	exit 1
else
	echo "All dependencies met"
fi

echo "Checking amount of RAM in system"
#We require ~230MB of total RAM
TOTAL_RAM=`grep MemTotal /proc/meminfo | awk '{print $2}'`
TOTAL_SWAP=`grep SwapTotal /proc/meminfo | awk '{print $2}'`
if [ "$TOTAL_RAM" -lt 230000 ]; then
	echo "Your system has $TOTAL_RAM kB RAM available, which is too low. Checking swap space."
	if [ "$TOTAL_SWAP" -lt 230000 ]; then
		echo "Your system has $TOTAL_SWAP kB swap available, which is too low."
		echo "In order to compile ffmpeg you need to set memory_split to 16 for 256MB RAM PIs (0 does not work) or to at most 256 for 512MB RAM PIs, respectively."
		echo "Otherwise there is not enough RAM to compile ffmpeg. Please do the apropriate in the raspi-config and select finish to reboot your RPi."
		echo "Warning: to run compiled omxplayer please start raspi-config again and set memory_split to at least 128."
	else
		echo "You have enough swap space to compile, but speed will be lower and SD card wear will be increased."
		echo "In order to compile ffmpeg you need to set memory_split to 16 for 256MB RAM PIs (0 does not work) or to at most 256 for 512MB RAM PIs, respectively."
		echo "Otherwise there is not enough RAM to compile ffmpeg. Please do the apropriate in the raspi-config and select finish to reboot your RPi."
		echo "Warning: to run compiled omxplayer please start raspi-config again and set memory_split to at least 128."
	fi
else
	echo "You should have enough RAM available to successfully compile and run omxplayer."
fi
