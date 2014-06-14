OMXPlayer
=========

OMXPlayer is a commandline OMX player for the Raspberry Pi. It was developed as
a testbed for the XBMC Raspberry PI implementation and is quite handy to use
standalone. 

Downloading OMXPlayer
---------------------

    git clone https://github.com/popcornmix/omxplayer.git

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

Install with
    
    sudo make install
    

Using OMXPlayer
---------------

    Usage: omxplayer [OPTIONS] [FILE]
    Options :
         -h / --help                    print this help
         -v / --version                 print version info
         -k / --keys                    print key bindings
         -n / --aidx  index             audio stream index    : e.g. 1
         -o / --adev  device            audio out device      : e.g. hdmi/local/both
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
         -l / --pos n                   start position (hh:mm:ss)
         -b / --blank                   set background to black
              --loop                    loop file. Ignored if file is not seekable, start position applied if given
              --no-boost-on-downmix     don't boost volume when downmixing
              --vol n                   Set initial volume in millibels (default 0)
              --no-osd                  do not display status information on screen
              --subtitles path          external subtitles in UTF-8 srt format
              --font path               subtitle font
                                        (default: /usr/share/fonts/truetype/freefont/FreeSans.ttf)
              --italic-font path        (default: /usr/share/fonts/truetype/freefont/FreeSansOblique.ttf)
              --font-size size          font size as thousandths of screen height
                                        (default: 55)
              --align left/center       subtitle alignment (default: left)
              --no-ghost-box            no semitransparent boxes behind subtitles
              --lines n                 number of lines to accommodate in the subtitle buffer
                                        (default: 3)
              --win "x1 y1 x2 y2"       Set position of video window
              --audio_fifo  n           Size of audio output fifo in seconds
              --video_fifo  n           Size of video output fifo in MB
              --audio_queue n           Size of audio input queue in MB
              --video_queue n           Size of video input queue in MB
              --threshold   n           Amount of buffered data required to come out of buffering in seconds
              --timeout     n           Amount of time a file/network operation can stall for before timing out (default 10s)
              --orientation n           Set orientation of video (0, 90, 180 or 270)
              --fps n                   Set fps of video where timestamps are not present
              --live                    Set for live tv or vod type stream
              --layout                  Set output speaker layout (e.g. 5.1)
              --key-config <file>       Uses key bindings specified in <file> instead of the default

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

Key Config Syntax
-----------------

A key config file is a series of rules of the form [action]:[key]. Multiple keys can be bound
to the same action, and comments are supported by adding a # in front of the line.
The list of valid [action]s roughly corresponds to the list of default key bindings above and are:

    DECREASE_SPEED
    INCREASE_SPEED
    REWIND
    FAST_FORWARD
    SHOW_INFO
    PREVIOUS_AUDIO
    NEXT_AUDIO
    PREVIOUS_CHAPTER
    NEXT_CHAPTER
    PREVIOUS_SUBTITLE
    NEXT_SUBTITLE
    TOGGLE_SUBTITLE
    DECREASE_SUBTITLE_DELAY
    INCREASE_SUBTITLE_DELAY
    EXIT
    PAUSE
    DECREASE_VOLUME
    INCREASE_VOLUME
    SEEK_BACK_SMALL
    SEEK_FORWARD_SMALL
    SEEK_BACK_LARGE
    SEEK_FORWARD_LARGE
    STEP

Valid [key]s include all alpha-numeric characters and most symbols, as well as:
    left
    right
    up
    down
    esc
    hex [keycode]

For example:

    EXIT:esc
    PAUSE:p
    #Note that this next line has a space after the :
    PAUSE: 
    REWIND:left
    SEEK_FORWARD_SMALL:hex 0x4f43
    EXIT:q

## DBUS Control

`omxplayer` can be controlled via DBUS.  There are three interfaces, all of
which present a different set of commands.  For examples on working with DBUS
take a look at the supplied [dbuscontrol.sh](dbuscontrol.sh) file.

### Root Interface

The root interface is accessible under the name
`org.mpris.MediaPlayer2`.

#### Quit

Stops the currently playing video.  This will cause the currently running
omxplayer process to terminate.

   Params       |   Type   
:-------------: | -------
 Return         | `null`

### Properties Interface

The properties interface is accessible under the name
`org.freedesktop.DBus.Properties`.

#### CanQuit

Whether or not the player can quit.

   Params       |   Type   
:-------------: | ---------
 Return         | `boolean`

#### Fullscreen

Whether or not the player can is fullscreen.

   Params       |   Type   
:-------------: | ---------
 Return         | `boolean`

#### CanSetFullscreen

Whether or not the player can set fullscreen.

   Params       |   Type   
:-------------: | ---------
 Return         | `boolean`

#### CanRaise

Not sure what this means.

   Params       |   Type   
:-------------: | ---------
 Return         | `boolean`

#### HasTrackList

Whether or not the player has a track list.

   Params       |   Type   
:-------------: | ---------
 Return         | `boolean`

#### Identity

Name of the player.

   Params       |   Type   
:-------------: | --------
 Return         | `string`

#### SupportedUriSchemes

Playable URI formats.

   Params       |   Type   
:-------------: | ----------
 Return         | `string[]`

#### SupportedMimeTypes

Supported mime types.  **Note**: currently not implemented.

   Params       |   Type   
:-------------: | ----------
 Return         | `string[]`

#### CanGoNext

Whether or not the play can skip to the next track.

   Params       |   Type   
:-------------: | ---------
 Return         | `boolean`

#### CanGoPrevious

Whether or not the player can skip to the previous track.

   Params       |   Type   
:-------------: | ---------
 Return         | `boolean`

#### CanSeek

Whether or not the player can seek.

   Params       |   Type   
:-------------: | ---------
 Return         | `boolean`


#### CanControl

Whether or not the player can be controlled.

   Params       |   Type   
:-------------: | ---------
 Return         | `boolean`

#### CanPlay

Whether or not the player can play.

Return type: `boolean`.

#### CanPause

Whether or not the player can pause.

   Params       |   Type
:-------------: | ---------
 Return         | `boolean`

#### PlaybackStatus

The current state of the player, either "Paused" or "Playing".

   Params       |   Type
:-------------: | ---------
 Return         | `string` 

#### Volume

When called with an argument it will set the volume and return the current
volume.  When called without an argument it will simply return the current
volume.  As defined by the [MPRIS][MPRIS_volume] specifications, this value
should be greater than or equal to 0 and no larger than about 1.

Millibels can be converted to/from acceptable values using the following:

    volume = pow(10, mB / 2000.0);
    mB     = 2000.0 * log10(volume)

   Params       |   Type    | Description
:-------------:	| --------- | ---------------------------
 1 (optional)   | `double`  | Volume to set
 Return         | `double`  | Current volume

[MPRIS_volume]: http://specifications.freedesktop.org/mpris-spec/latest/Player_Interface.html#Simple-Type:Volume

#### Mute

Mute the audio stream.  If the volume is already muted, this does nothing.

   Params       |   Type
:-------------: | -------
 Return         | `null` 

#### Unmute

Unmute the audio stream.  If the stream is already unmuted, this does nothing.

   Params       |   Type
:-------------: | -------
 Return         | `null` 

#### Position

Returns the current position of the playing media.

   Params       |   Type    | Description
:-------------: | --------- | --------------------------------
 Return         | `int64`   | Current position in microseconds

#### Duration

Returns the total length of the playing media.

   Params       |   Type    | Description
:-------------: | --------- | ----------------------------
 Return         | `int64`   | Total length in microseconds


#### MinimumRate

Returns the minimum playback rate of the video.

   Params       |   Type
:-------------: | -------
 Return         | `double` 

#### MaximumRate

Returns the maximum playback rate of the video.

   Params       |   Type
:-------------: | -------
 Return         | `double` 


### Player Interface


#### Next

Skip to the next chapter.

   Params       |   Type
:-------------: | -------
 Return         | `null` 

#### Previous

Skip to the previous chapter.

   Params       |   Type
:-------------: | -------
 Return         | `null` 

#### Pause

Toggles the play state.  If the video is playing, it will be paused, if it is
paused it will start playing.

   Params       |   Type
:-------------: | -------
 Return         | `null` 

#### PlayPause

Same as the `Pause` method.

#### Stop

Stops the video.

   Params       |   Type
:-------------: | -------
 Return         | `null` 

#### Seek

Perform a *relative* seek, i.e. seek plus or minus a certain number of
microseconds from the current position in the video.

   Params       |   Type            | Description
:-------------: | ----------------- | ---------------------------
 1              | `int64`           | Microseconds to seek
 Return         | `null` or `int64` | If the supplied offset is invalid, `null` is returned, otherwise the offset (in microseconds) is returned

#### SetPosition

Seeks to a specific location in the file.  This is an *absolute* seek.

   Params       |   Type            | Description
:-------------: | ----------------- | ------------------------------------
 1              | `string`          | Path (not currently used)
 2              | `int64`           | Position to seek to, in microseconds
 Return         | `null` or `int64` | If the supplied position is invalid, `null` is returned, otherwise the position (in microseconds) is returned


#### ListSubtitles

Returns a array of all known subtitles.  The length of the array is the number
of subtitles.  Each item in the araay is a string in the following format:

    <index>:<language>:<name>:<codec>:<active>

Any of the fields may be blank, except for `index`.  `language` is the language
code, such as `eng`, `chi`, `swe`, etc.  `name` is a description of the
subtitle, such as `foreign parts` or `SDH`.  `codec` is the name of the codec
used by the subtitle, sudh as `subrip`.  `active` is either the string `active`
or an empty string.

   Params       |   Type
:-------------: | ----------
 Return         | `string[]` 

#### ListAudio

Returns and array of all known audio streams.  The length of the array is the
number of streams.  Each item in the array is a string in the following format:

    <index>:<language>:<name>:<codec>:<active>

See `ListSubtitles` for a description of what each of these fields means.  An
example of a possible string is:

    0:eng:DD 5.1:ac3:active

   Params       |   Type
:-------------: | ----------
 Return         | `string[]` 

#### ListVideo

Returns and array of all known video streams.  The length of the array is the
number of streams.  Each item in the array is a string in the following format:

    <index>:<language>:<name>:<codec>:<active>

See `ListSubtitles` for a description of what each of these fields means.  An
example of a possible string is:

    0:eng:x264:h264:active

#### SelectSubtitle

Selects the subtitle at a given index.

   Params       |   Type    | Description
:-------------: | ----------| ------------------------------------
 1              | `int32`   | Index of subtitle to select
 Return         | `boolean` | Returns `true` if subtitle was selected, `false otherwise


#### SelectAudio

Selects the audio stream at a given index.

   Params       |   Type    | Description
:-------------: | ----------| ------------------------------------
 1              | `int32`   | Index of audio stream to select
 Return         | `boolean` | Returns `true` if stream was selected, `false otherwise

#### ShowSubtitles

Turns on subtitles.

   Params       |   Type 
:-------------: | -------
 Return         | `null` 

#### HideSubtitles

Turns off subtitles.

   Params       |   Type 
:-------------: | -------
 Return         | `null` 

#### Action

Execute a "keyboard" command.  For available codes, see
[KeyConfig.h](blob/master/KeyConfig.h).


   Params       |   Type    | Description
:-------------: | ----------| ------------------
 1              | `int32`   | Command to execute
 Return         | `null`    | 


