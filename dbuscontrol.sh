#!/bin/bash

#set -x

OMXPLAYER_DBUS_ADDR="/tmp/omxplayerdbus"
OMXPLAYER_DBUS_PID="/tmp/omxplayerdbus.pid"
export DBUS_SESSION_BUS_ADDRESS=`cat $OMXPLAYER_DBUS_ADDR`
export DBUS_SESSION_BUS_PID=`cat $OMXPLAYER_DBUS_PID`

[ -z "$DBUS_SESSION_BUS_ADDRESS" ] && { echo "Must have DBUS_SESSION_BUS_ADDRESS" >&2; exit 1; }

case $1 in
status)
	duration=`dbus-send --print-reply=literal --session --reply-timeout=500 --dest=org.mpris.MediaPlayer2.omxplayer /org/mpris/MediaPlayer2 org.freedesktop.DBus.Properties.Duration`
	[ $? -ne 0 ] && exit 1
	duration="$(awk '{print $2}' <<< "$duration")"

	position=`dbus-send --print-reply=literal --session --reply-timeout=500 --dest=org.mpris.MediaPlayer2.omxplayer /org/mpris/MediaPlayer2 org.freedesktop.DBus.Properties.Position`
	[ $? -ne 0 ] && exit 1
	position="$(awk '{print $2}' <<< "$position")"

	playstatus=`dbus-send --print-reply=literal --session --reply-timeout=500 --dest=org.mpris.MediaPlayer2.omxplayer /org/mpris/MediaPlayer2 org.freedesktop.DBus.Properties.PlaybackStatus`
	[ $? -ne 0 ] && exit 1
	playstatus="$(sed 's/^ *//;s/ *$//;' <<< "$playstatus")"

	paused="true"
	[ "$playstatus" == "Playing" ] && paused="false"
	echo "Duration: $duration"
	echo "Position: $position"
	echo "Paused: $paused"
	;;

pause)
	dbus-send --print-reply=literal --session --dest=org.mpris.MediaPlayer2.omxplayer /org/mpris/MediaPlayer2 org.mpris.MediaPlayer2.Player.Action int32:16 >/dev/null
	;;

stop)
	dbus-send --print-reply=literal --session --dest=org.mpris.MediaPlayer2.omxplayer /org/mpris/MediaPlayer2 org.mpris.MediaPlayer2.Player.Action int32:15 >/dev/null
	;;

seek)
	dbus-send --print-reply=literal --session --dest=org.mpris.MediaPlayer2.omxplayer /org/mpris/MediaPlayer2 org.mpris.MediaPlayer2.Player.Seek int64:$2 >/dev/null
	;;

setposition)
	dbus-send --print-reply=literal --session --dest=org.mpris.MediaPlayer2.omxplayer /org/mpris/MediaPlayer2 org.mpris.MediaPlayer2.Player.SetPosition objpath:/not/used int64:$2 >/dev/null
	;;

setvideopos)
	dbus-send --print-reply=literal --session --dest=org.mpris.MediaPlayer2.omxplayer /org/mpris/MediaPlayer2 org.mpris.MediaPlayer2.Player.VideoPos objpath:/not/used string:"$2 $3 $4 $5" >/dev/null
	;;

hidevideo)
	dbus-send --print-reply=literal --session --dest=org.mpris.MediaPlayer2.omxplayer /org/mpris/MediaPlayer2 org.mpris.MediaPlayer2.Player.Action int32:28 >/dev/null
	;;

unhidevideo)
	dbus-send --print-reply=literal --session --dest=org.mpris.MediaPlayer2.omxplayer /org/mpris/MediaPlayer2 org.mpris.MediaPlayer2.Player.Action int32:29 >/dev/null
	;;

setvideolayer)
	dbus-send --print-reply=literal --session --dest=org.mpris.MediaPlayer2.omxplayer /org/mpris/MediaPlayer2 org.mpris.MediaPlayer2.Player.VideoLayer int64:$2 >/dev/null
	;;

volumeup)
	dbus-send --print-reply=literal --session --dest=org.mpris.MediaPlayer2.omxplayer /org/mpris/MediaPlayer2 org.mpris.MediaPlayer2.Player.Action int32:18 >/dev/null
	;;

volumedown)
	dbus-send --print-reply=literal --session --dest=org.mpris.MediaPlayer2.omxplayer /org/mpris/MediaPlayer2 org.mpris.MediaPlayer2.Player.Action int32:17 >/dev/null
	;;

*)
	echo "usage: $0 status|pause|stop|seek|volumeup|volumedown|setposition [position in microseconds]|hidevideo|unhidevideo|setvideopos [x1 y1 x2 y2]|setvideolay n" >&2
	exit 1
	;;
esac
