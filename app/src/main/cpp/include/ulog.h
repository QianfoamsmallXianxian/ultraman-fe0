#ifndef ULOG_H
#define ULOG_H
#ifdef __cplusplus
extern "C" {
#endif
void ulog_init(const char *dir);
void ulog_write(int prio, const char *tag, const char *fmt, ...);
#ifdef __cplusplus
}
#endif
#endif
