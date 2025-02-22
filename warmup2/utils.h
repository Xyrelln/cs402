#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>
#include <stdint.h>

#include "cs402.h"
#include "my402list.h"

#include "vals.h"

void Usage(void);
int isValidFileName(const char *filename);
void ProcessOptions(int argc, char *argv[], int *fd);
void SetProgramName(const char *s);
void PrintParams(void);
long CalTimevalMilliseconds(const struct timeval *tv);
long CalTimevalMicroeconds(const struct timeval *tv);
struct timeval CalAvgTime(int prev_count, struct timeval *prev_avg, struct timeval *new_timeval);
struct timeval CalTimeDiff_timeval(const struct timeval *former, const struct timeval *latter);
struct timeval CalTimevalAdd(const struct timeval *a, const struct timeval *b);
struct timeval CalTimevalMult(struct timeval *t, double multiplier);
struct timeval CalTimevalDevision_int(struct timeval *t, int divisor);
double CalTimevalDevision(struct timeval *t1, struct timeval *t2);
double TimevalToDouble(struct timeval *t);
void logLine(const char *message, const struct timeval *tv);
void setLastPacketTime(const struct timeval *timeval);
void Init(void);

#endif
