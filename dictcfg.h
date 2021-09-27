#ifndef _DICTCFG_H_
#define _DICTCFG_H_ 1

#include "dict.h"

#include <stdio.h>

void log_printf(FILE *fp, const char *Format, ...);

dict_p dictcfg_load(const char *cfg_path, FILE *fp_log);
dict_p dictcfg_section(dict_p cfg,const char *name);

int dictcfg_getint(dict_p section, const char *key, int def);
char *dictcfg_getstr(dict_p section, const char *key, char *def);
double dictcfg_getfloat(dict_p section, const char *key, double def);

#endif
