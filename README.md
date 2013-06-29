OMXPlayer
=========

OMXPlayer is a commandline OMX player for the Raspberry Pi. It was developed as
a testbed for the XBMC Raspberry PI implementation and is quite handy to use
standalone. 

Downloading OMXPlayer
---------------------

    git clone git://github.com/huceke/omxplayer.git

Compiling OMXPlayer
-------------------

GCC version 4.7 is required.

### Cross Compiling

You need the content of your sdcard somewhere mounted or copied. There might be
development headers to install on the running Pi system for the crosscompiling.

Edit Makefile.include and change the settings according your locations.

    make ffmpeg
    make
    make dist

Installing OMXPlayer
--------------------

Copy over `omxplayer-dist/*` to the Pi `/`. You may want to specify a valid font
path inside the `omxplayer` shell script.

### Compiling on the Pi

You can also compile it on the PI the native way ;)
Run this script (which will install packages and update firmware)
    ./prepare-native-raspbian.sh
and build with
    make ffmpeg
    make

Using OMXPlayer
---------------

    Usage: omxplayer [OPTIONS] [FILE]
    Options :
         -h / --help                    print this help
         -v / --version                 print version info
         -k / --keys                    print key bindings
         -n / --aidx  index             audio stream index    : e.g. 1
         -o / --adev  device            audio out device      : e.g. hdmi/local
         -i / --info                    dump stream format and exit
         -s / --stats                   pts and buffer stats
         -p / --passthrough             audio passthrough
         -d / --deinterlace             force deinterlacing
              --nodeinterlace           force no deinterlacing
         -w / --hw                      hw audio decoding
         -3 / --3d mode                 switch tv into 3d mode (e.g. SBS/TB)
         -y / --hdmiclocksync           adjust display refresh rate to match video (default)
         -z / --nohdmiclocksync         do not adjust display refresh rate to match video
         -t / --sid index               show subtitle with index
         -r / --refresh                 adjust framerate/resolution to video
         -g / --genlog                  generate log file
         -l / --pos n                   start position (in seconds)
         -b / --blank                   set background to black
              --boost-on-downmix        boost volume when downmixing
              --vol n                   Set initial volume in millibels (default 0)
              --subtitles path          external subtitles in UTF-8 srt format
              --font path               subtitle font
                                        (default: /usr/share/fonts/truetype/freefont/FreeSans.ttf)
              --italic-font path        (default: /usr/share/fonts/truetype/freefont/FreeSansOblique.ttf)
              --font-size size          font size as thousandths of screen height
                                        (default: 55)
              --align left/center       subtitle alignment (default: left)
              --lines n                 number of lines to accommodate in the subtitle buffer
                                        (default: 3)
              --win "x1 y1 x2 y2"       Set position of video window
              --audio_fifo  n           Size of audio output fifo in seconds
              --video_fifo  n           Size of video output fifo in MB
              --audio_queue n           Size of audio input queue in MB
              --video_queue n           Size of video input queue in MB
              --threshold   n           Amount of buffered data required to come out of buffering in seconds

For example:

    ./omxplayer -p -o hdmi test.mkv

Key Bindings
------------

While playing you can use the following keys to control omxplayer:

    z			Show Info
    1			Decrease Speed
    2			Increase Speed
    j			Previous Audio stream
    k			Next Audio stream
    i			Previous Chapter
    o			Next Chapter
    n			Previous Subtitle stream
    m			Next Subtitle stream
    s			Toggle subtitles
    d			Subtitle delay -250 ms
    f			Subtitle delay +250 ms
    q			Exit OMXPlayer
    Space or p	Pause/Resume
    -			Decrease Volume
    +			Increase Volume
    Left Arrow	Seek -30 s
    Right Arrow	Seek +30 s
    Down Arrow	Seek -600 s
    Up Arrow	Seek +600 s
