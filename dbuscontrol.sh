#!/bin/bash

#set -x

OMXPLAYER_DBUS_ADDR="/tmp/omxplayerdbus.${USER}"
OMXPLAYER_DBUS_PID="/tmp/omxplayerdbus.${USER}.pid"
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

volume)
	volume=`dbus-send --print-reply=double --session --reply-timeout=500 --dest=org.mpris.MediaPlayer2.omxplayer /org/mpris/MediaPlayer2 org.freedesktop.DBus.Properties.Volume ${2:+double:}$2`
	[ $? -ne 0 ] && exit 1
	volume="$(awk '{print $2}' <<< "$volume")"
	echo "Volume: $volume"
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

setalpha)
	dbus-send --print-reply=literal --session --dest=org.mpris.MediaPlayer2.omxplayer /org/mpris/MediaPlayer2 org.mpris.MediaPlayer2.Player.SetAlpha objpath:/not/used int64:$2 >/dev/null
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

volumeup)
	dbus-send --print-reply=literal --session --dest=org.mpris.MediaPlayer2.omxplayer /org/mpris/MediaPlayer2 org.mpris.MediaPlayer2.Player.Action int32:18 >/dev/null
	;;

volumedown)
	dbus-send --print-reply=literal --session --dest=org.mpris.MediaPlayer2.omxplayer /org/mpris/MediaPlayer2 org.mpris.MediaPlayer2.Player.Action int32:17 >/dev/null
	;;

togglesubtitles)
	dbus-send --print-reply=literal --session --dest=org.mpris.MediaPlayer2.omxplayer /org/mpris/MediaPlayer2 org.mpris.MediaPlayer2.Player.Action int32:12 >/dev/null
	;;

hidesubtitles)
	dbus-send --print-reply=literal --session --dest=org.mpris.MediaPlayer2.omxplayer /org/mpris/MediaPlayer2 org.mpris.MediaPlayer2.Player.Action int32:30 >/dev/null
	;;

showsubtitles)
	dbus-send --print-reply=literal --session --dest=org.mpris.MediaPlayer2.omxplayer /org/mpris/MediaPlayer2 org.mpris.MediaPlayer2.Player.Action int32:31 >/dev/null
	;;

screenshot)
	dbus-send --print-reply=literal --session --dest=org.mpris.MediaPlayer2.omxplayer /org/mpris/MediaPlayer2 org.mpris.MediaPlayer2.Player.Screenshot >/dev/null
	;;

*)
	echo "usage: $0 status|pause|stop|seek|volumeup|volumedown|setposition [position in microseconds]|hidevideo|unhidevideo|togglesubtitles|hidesubtitles|showsubtitles|setvideopos [x1 y1 x2 y2]|setalpha [alpha (0..255)]|screenshot" >&2
	exit 1
	;;
esac
