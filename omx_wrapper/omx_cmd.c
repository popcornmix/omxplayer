#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "debug.h"
#include "omx_cmd.h"

#define MAX_ARG_LEN 200
#define MAX_CMD_LEN 100

#define strsame(str1, str2) (strcmp(str1, str2) == 0)

static const char omx_path[] = "../omxplayer";
static const char dbus_path[] = "../dbuscontrol.sh";

void omx_cmd(const char* cmd, char* arg)
{

    //omxplayer
    if(strsame(cmd, "play"))
        goto omx;
    //dbus message
    if(strsame(cmd, "pause") || strsame(cmd, "stop") || strsame(cmd, "volumeup") \
       || strsame(cmd, "volumedown"))
        goto dbus;
    //shell
        //shell_ctl();

    elog("No such omx command(%s)\n", cmd);
    return;

omx:
    omx_ctl(cmd, arg);
    return;
dbus:
    dbus_ctl(cmd, arg);
    return;
/*shell*/
}

void omx_ctl(const char *act, char* arg)
{
    strlen_checker(arg, MAX_ARG_LEN);//just check the limit
    
    char cmd[MAX_CMD_LEN];
    memset(cmd, '\0', MAX_CMD_LEN);

    if(arg)
        assert(sprintf(cmd, "%s %s %s", dbus_path, act, arg) < MAX_CMD_LEN);
    else
        assert(sprintf(cmd, "%s %s", dbus_path, act) < MAX_CMD_LEN);
    system(cmd);
}

void dbus_ctl(const char *act, char *arg)
{    
    strlen_checker(arg, MAX_ARG_LEN);//just check the limit

    char cmd[MAX_CMD_LEN];
    memset(cmd, '\0', MAX_CMD_LEN);

    if(arg)
        assert(sprintf(cmd, "%s %s %s", omx_path, act, arg) < MAX_CMD_LEN);
    else
        assert(sprintf(cmd, "%s %s", omx_path, act) < MAX_CMD_LEN);
    system(cmd);
}

size_t strlen_checker(const char* str, int limit)
{
    if(str) {
        dlog("str = NULL");
        return 0;
    }

    size_t len = strlen(str);
    if(len > limit) {
        elog("strlen over the limitation %d", limit);
        exit(EXIT_FAILURE);
    }

    return len;
}
