
//omx package command, it could be omx or dbus or shell
void omx_cmd(const char* cmd, char* arg);

void omx_ctl(const char *act, char* arg);

void dbus_ctl(const char *act, char *arg);

size_t strlen_checker(const char* str, int limit);
