# Detect and Download the necessary packages

PREINSTALL_COMMAND=$(./prepare-native-raspbian.sh | grep sudo |sed 's/^...........................//')
sudo apt-get update
$($PREINSTALL_COMMAND)

#  Compile

make -j4 ffmpeg && mkae -j4 && sudo make install
