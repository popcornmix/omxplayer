# Detect and Download the necessary packages

PREINSTALL_COMMAND=$(../prepare-native-raspbian.sh | grep sudo |sed 's/^...........................//')
sudo apt-get update
$($PREINSTALL_COMMAND)

#  Compile

make -C .. -j4 ffmpeg && make -C .. -j4 && sudo make -C .. install
