#ifndef VALS_H
#define VALS_H

#include "cs402.h"
#include "my402list.h"
#include <pthread.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#define BUFFER_SIZE 200
#define MAX_VALID_LINE_CHAR 1024

extern char gszProgName[MAXPATHLENGTH];
extern int mode; // 0 for deterministic, 1 for trace-driven
extern int lineNum;

// default values
extern double lambda;
extern double mu;
extern double r;
extern int B;   // bucket capacity
extern int P;   // tokens required for packet
extern int num; // how many packets

// serialization box and mutex
extern pthread_mutex_t m;
extern pthread_cond_t cv;
extern char buffer[BUFFER_SIZE];
extern struct timeval start_time;
extern struct timeval end_time;
extern My402List Q1, Q2;
extern int Q1_packet_count; // the largest packet index Q1 received
extern struct timeval Q1_packet_time_avg;
extern struct timeval Q2_packet_time_avg;
extern int S1_packet_count;
extern int S2_packet_count;
extern struct timeval S1_packet_time_avg;
extern struct timeval S2_packet_time_avg;
extern struct timeval time_in_system_avg;
extern double X;
extern double X2;
extern int bucket;
extern int packet_serviced_count;
extern int packet_dropped;
extern struct timeval packet_service_time_avg;
extern int stop_flag;

extern int token_counter; // actually this is amount + 1, bad naming but too lazy
extern int token_dropped;

// statistic
extern int arrived_count;
extern long long arrive_time_avg_milliseconds;

extern struct timeval last_packet_arrival;

#endif