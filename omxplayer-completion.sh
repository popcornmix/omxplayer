_omxplayer ()
{

    local cur prev words cword

    _init_completion -n : || return

    case "$prev" in
        --subtitles)
            _filedir '@(srt|sub|txt)'
            return 0
            ;;

    esac

    case "$cur" in
        -*)
            COMPREPLY=( $( compgen -W '--subtitles' ) )
            ;;
        *)
            _filedir '@(mp?(e)g|MP?(E)G|wm[av]|WM[AV]|avi|AVI|asf|ASF|vob|VOB|bin|BIN|dat|DAT|vcd|VCD|ps|PS|pes|PES|fl[iv]|FL[IV]|fxm|FXM|viv|VIV|rm?(j)|RM?(J)|ra?(m)|RA?(M)|yuv|YUV|mov|MOV|qt|QT|mp[234]|MP[234]|m4[av]|M4[AV]|og[gmavx]|OG[GMAVX]|w?(a)v|W?(A)V|dump|DUMP|mk[av]|MK[AV]|m4a|M4A|aac|AAC|m[24]v|M[24]V|dv|DV|rmvb|RMVB|mid|MID|t[ps]|T[PS]|3g[p2]|3gpp?(2)|mpc|MPC|flac|FLAC|vro|VRO|divx|DIVX|aif?(f)|AIF?(F)|m2t?(s)|M2T?(S)|vdr|VDR|xvid|XVID|ape|APE|gif|GIF|nut|NUT|bik|BIK|webm|WEBM|amr|AMR|awb|AWB|iso|ISO|opus|OPUS)?(.part)'
            return 0
            ;;
    esac

    return 0
} &&
complete -F _omxplayer omxplayer
