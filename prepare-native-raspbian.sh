#!/bin/bash

echo "Patching makefiles..."
echo "FLOAT=hard

CFLAGS +=  -mfloat-abi=hard -mcpu=arm1176jzf-s -fomit-frame-pointer -mabi=aapcs-linux -mtune=arm1176jzf-s -mfpu=vfp -Wno-psabi -mno-apcs-stack-check -O3 -mstructure-size-boundary=32 -mno-sched-prolog -march=armv6zk

BUILDROOT	:=/usr/local/src/omxplayer
TOOLCHAIN	:=/usr/
LD			:= \$(TOOLCHAIN)/bin/ld
CC			:= \$(TOOLCHAIN)/bin/gcc 
CXX       	:= \$(TOOLCHAIN)/bin/g++
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
			-I/usr/include \
			-I/usr/include/freetype2" > Makefile.include

sed -i '/--enable-cross-compile \\/d;' Makefile.ffmpeg
sed -i 's/			--cross-prefix=$(HOST)-//g;' Makefile.ffmpeg
sed -i 's/			--disable-debug \\/			--disable-debug /g;' Makefile.ffmpeg

sed -i 's/$(HOST)-//g;' Makefile.*
sed -i 's/ -j9//g;' Makefile.*
sed -i 's/#arm-unknown-linux-gnueabi-strip/arm-unknown-linux-gnueabi-strip/g;' Makefile
sed -i 's/arm-unknown-linux-gnueabi-strip/strip/g;' Makefile

echo "
install: dist
	cp omxplayer-dist/* / -r" >> Makefile

echo "Installing packages..."
sudo apt-get update
sudo apt-get -y install ca-certificates git-core subversion binutils libva1 libpcre3-dev libidn11-dev libboost1.50-dev libfreetype6-dev libusb-1.0-0-dev
sudo apt-get -y install gcc-4.7 g++-4.7
pushd /usr/bin && sudo rm gcc && sudo ln -s gcc-4.7 gcc && sudo rm g++ && sudo ln -s g++-4.7 g++ && popd


echo "Installing the rpi-update script..."
sudo wget http://goo.gl/1BOfJ -O /usr/bin/rpi-update && sudo chmod +x /usr/bin/rpi-update
echo "Updating firmware..."
sudo rpi-update

echo "In order to compile ffmpeg you need to set memory_split to 16 for 256MB RAM PIs (0 does not work) or to at most 256 for 512MB RAM PIs, respectively. Otherwise there is not enough RAM to compile ffmpeg. Please do the apropriate in the raspi-config and select finish to reboot your RPi. Warning: to run compiled omxplayer please start raspi-config again and set memory_split to at least 128. [Press RETURN to continue]"
read -r REPLY
sudo raspi-config

