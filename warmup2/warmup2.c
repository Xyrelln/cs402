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

#include "cs402.h"
#include "my402list.h"

#include "vals.h"
#include "utils.h"

typedef struct formatted_time
{
    long milliseconds;
    long microseconds;
} formatted_time;

typedef struct Packet
{
    struct timeval initial_arrival, Q1_arrival, Q1_leave, Q2_arrival, Q2_leave, Server_arrival, Server_leave; // for statistics
    int initial_arrival_delay;
    int token_needed;
    int service_time;
    int index;
} Packet;

/* ----------------------- packet distributer ----------------------- */

void ResumePacketToQ2()
{
    Packet *first = (Packet *)(Q1.First(&Q1)->obj);
    Q1.Unlink(&Q1, Q1.First(&Q1));
    gettimeofday(&first->Q1_leave, 0);

    // log leave Q1
    struct timeval tv = CalTimeDiff_timeval(&first->Q1_arrival, &first->Q1_leave);
    snprintf(buffer, sizeof(buffer), "p%d leaves Q1, time in Q1 = %ld.%03ldms, token bucket now has %d token", first->index + 1, CalTimevalMilliseconds(&tv), CalTimevalMicroeconds(&tv), bucket);
    logLine(buffer, &first->Q1_leave);

    // append to Q2
    Q2.Append(&Q2, first);
    gettimeofday(&first->Q2_arrival, 0);

    // log arrive at Q2
    snprintf(buffer, sizeof(buffer), "p%d enters Q2", first->index + 1);
    logLine(buffer, &first->Q2_arrival);
}

// must have access to box
void AppendPacketToQ1(Packet *packet)
{
    Q1.Append(&Q1, packet);
    Q1_packet_count++;

    gettimeofday(&packet->Q1_arrival, 0);

    snprintf(buffer, sizeof(buffer), "p%d enters Q1", packet->index + 1);
    logLine(buffer, &packet->Q1_arrival);

    if (Q1.num_members == 1) // he's the only guy
    {
        if (bucket >= packet->token_needed)
        {
            bucket -= packet->token_needed;
            ResumePacketToQ2();
        }
    }
}

// schedule packet
int SchedulePacket(int arrival_time, int packet_counter, Packet *packet)
{
    // check before sleep
    if (stop_flag == 1)
    {
        free(packet);
        return 1;
    }

    usleep(arrival_time * 1000); // sleep for arrival_time milliseconds

    // add arrival time sum
    arrive_time_avg_milliseconds = arrive_time_avg_milliseconds * ((double)arrived_count / (double)(arrived_count + 1)) + ((double)arrival_time / (double)(arrived_count + 1));
    arrived_count += 1;

    // also check after wake up
    if (stop_flag == 1)
    {
        free(packet);
        return 2;
    }

    gettimeofday(&packet->initial_arrival, 0);
    struct timeval inter_arrival_tv = CalTimeDiff_timeval(&last_packet_arrival, &packet->initial_arrival);
    setLastPacketTime(&packet->initial_arrival);

    if (packet->token_needed > B)
    {
        packet_dropped++;

        pthread_mutex_lock(&m);
        snprintf(buffer, sizeof(buffer), "p%d arrives, needs %d tokens, inter-arrival time = %ld.%03ldms, dropped", packet->index + 1, packet->token_needed, CalTimevalMilliseconds(&inter_arrival_tv), CalTimevalMicroeconds(&inter_arrival_tv));
        logLine(buffer, &packet->initial_arrival);
        Q1_packet_count = packet_counter + 1;
        if (packet_dropped + packet_serviced_count >= num)
        {
            kill(getpid(), SIGUSR1);
        }
        pthread_cond_broadcast(&cv);
        pthread_mutex_unlock(&m);

        free(packet);
        return 0;
    }

    pthread_mutex_lock(&m);
    while (!(Q1_packet_count >= packet_counter))
    {
        pthread_cond_wait(&cv, &m);
    }

    snprintf(buffer, sizeof(buffer), "p%d arrives, needs %d tokens, inter-arrival time = %ld.%03ldms", packet_counter + 1, packet->token_needed, inter_arrival_tv.tv_sec * 1000 + inter_arrival_tv.tv_usec / 1000, inter_arrival_tv.tv_usec % 1000);
    logLine(buffer, &packet->initial_arrival);

    AppendPacketToQ1(packet);

    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&m);

    return 0;
}

// packet arrival thread first proc
void *t_packet(void *arg) // total amount of packets, requires P tokens, packet rate
{
    // generate packets
    for (int i = 0; i < num; i++)
    {
        Packet *newPacket = malloc(sizeof(Packet));
        newPacket->index = i;

        char buffer[BUFFER_SIZE] = {0};
        // read a line of tsfile and set packet parameters
        if (mode == 1)
        {
            if (fgets(buffer, BUFFER_SIZE, stdin) != NULL)
            {
                if (strlen(buffer) > MAX_VALID_LINE_CHAR)
                {
                    fprintf(stderr, "error: line %d has more than %d chars\n", lineNum, MAX_VALID_LINE_CHAR);
                    exit(1);
                }

                int res = sscanf(buffer, "%d\t%d\t%d", &newPacket->initial_arrival_delay, &newPacket->token_needed, &newPacket->service_time);
                if (res < 3)
                {
                    fprintf(stderr, "tffile error: line %d expected 3 parameters, received %d\n", i + 1, res);
                    exit(1);
                }
            }
            else
            {
                // should not end, there are more packets, err
                fprintf(stderr, "tsfile error: specified containing %d on line 1, but only %d lines of records provided\n", num, i);
            }
        }
        else
        {
            // not using tsfile, using default
            newPacket->initial_arrival_delay = min(round(1000 / lambda), 10000);
            newPacket->token_needed = P;
            newPacket->service_time = round(1000 / mu);
        }

        if (SchedulePacket(newPacket->initial_arrival_delay, i, newPacket) != 0)
            return NULL; // because it's sigint
    }

    return NULL;
}

/* ----------------------- token distributer ----------------------- */

// token arrival thread first proc
void *t_token(void *arg) // bucket cap, token rate
{
    int interval_millisecond = min(round(1000.0 / r), 10000);
    while (1)
    {
        pthread_mutex_lock(&m);
        if (stop_flag == 1 || (Q1.num_members == 0 && num <= arrived_count))
        {
            pthread_mutex_unlock(&m);
            return NULL;
        }
        pthread_mutex_unlock(&m);

        usleep(interval_millisecond * 1000);

        pthread_mutex_lock(&m);
        if (stop_flag == 1 || (Q1.num_members == 0 && num <= arrived_count))
        {
            pthread_mutex_unlock(&m);
            return NULL;
        }

        struct timeval token_arrival_tv;
        gettimeofday(&token_arrival_tv, 0);

        if (bucket < B)
        {
            bucket++;
            snprintf(buffer, sizeof(buffer), "token t%d arrives, token bucket now has %d %s", token_counter, bucket, bucket > 1 ? "tokens" : "token");
            logLine(buffer, &token_arrival_tv);
        }
        else
        {
            token_dropped++;
            snprintf(buffer, sizeof(buffer), "token t%d arrives, dropped", token_counter);
            logLine(buffer, &token_arrival_tv);
        }

        token_counter++;

        while (Q1.num_members > 0 && bucket >= ((Packet *)(Q1.First(&Q1)->obj))->token_needed)
        {
            bucket -= ((Packet *)(Q1.First(&Q1)->obj))->token_needed;
            ResumePacketToQ2();
        }

        pthread_cond_broadcast(&cv);
        pthread_mutex_unlock(&m);
    }

    return NULL;
}

/* ----------------------- server ----------------------- */

void TakeFirstPacketFromQ2(int server_index, Packet **p_serving_packet)
{
    *p_serving_packet = (Packet *)(Q2.First(&Q2)->obj);
    Packet *serving_packet = *p_serving_packet;

    Q2.Unlink(&Q2, Q2.First(&Q2));
    gettimeofday(&serving_packet->Q2_leave, 0);

    // log leave Q2
    struct timeval Q2_duration_tv = CalTimeDiff_timeval(&serving_packet->Q2_arrival, &serving_packet->Q2_leave);
    snprintf(buffer, sizeof(buffer), "p%d leaves Q2, time in Q2 = %ld.%03ldms", serving_packet->index + 1, CalTimevalMilliseconds(&Q2_duration_tv), CalTimevalMicroeconds(&Q2_duration_tv));
    logLine(buffer, &serving_packet->Q2_leave);
}

void UpdateVariance(struct timeval *new_time)
{
    double new_sample = TimevalToDouble(new_time);
    X = (double)packet_serviced_count / (double)(packet_serviced_count + 1) * X + new_sample / (double)(packet_serviced_count + 1);
    X2 = (double)packet_serviced_count / (double)(packet_serviced_count + 1) * X2 + new_sample * new_sample / (double)(packet_serviced_count + 1);
}

void ServicePacket(int server_index, Packet *serving_packet, int *packet_served)
{
    gettimeofday(&serving_packet->Server_arrival, 0);

    pthread_mutex_lock(&m);
    if (server_index == 1)
    {
        S1_packet_count++;
    }
    else
    {
        S2_packet_count++;
    }
    snprintf(buffer, sizeof(buffer), "p%d begins service at S%d, requesting %dms of service", serving_packet->index + 1, server_index, serving_packet->service_time);
    logLine(buffer, &serving_packet->Server_arrival);
    pthread_mutex_unlock(&m);

    usleep(serving_packet->service_time * 1000);

    gettimeofday(&serving_packet->Server_leave, 0);

    // log service
    struct timeval service_time_tv = CalTimeDiff_timeval(&serving_packet->Server_arrival, &serving_packet->Server_leave);
    struct timeval time_in_system_tv = CalTimeDiff_timeval(&serving_packet->initial_arrival, &serving_packet->Server_leave);

    pthread_mutex_lock(&m);

    snprintf(buffer, sizeof(buffer), "p%d departs from S%d, service time = %ld.%03ldms, time in system = %ld.%03ldms", serving_packet->index + 1, server_index, CalTimevalMilliseconds(&service_time_tv), CalTimevalMicroeconds(&service_time_tv), CalTimevalMilliseconds(&time_in_system_tv), CalTimevalMicroeconds(&time_in_system_tv));
    logLine(buffer, &serving_packet->Server_leave);

    // statistics
    struct timeval tv = CalTimeDiff_timeval(&serving_packet->Server_arrival, &serving_packet->Server_leave);
    packet_service_time_avg = CalAvgTime(packet_serviced_count, &packet_service_time_avg, &tv);

    tv = CalTimeDiff_timeval(&serving_packet->Q1_arrival, &serving_packet->Q1_leave);
    Q1_packet_time_avg = CalAvgTime(packet_serviced_count, &Q1_packet_time_avg, &tv);

    tv = CalTimeDiff_timeval(&serving_packet->Q2_arrival, &serving_packet->Q2_leave);
    Q2_packet_time_avg = CalAvgTime(packet_serviced_count, &Q2_packet_time_avg, &tv);

    tv = CalTimeDiff_timeval(&serving_packet->Server_arrival, &serving_packet->Server_leave);
    if (server_index == 1)
    {
        S1_packet_time_avg = CalAvgTime(*packet_served, &S1_packet_time_avg, &tv);
    }
    else
    {
        S2_packet_time_avg = CalAvgTime(*packet_served, &S2_packet_time_avg, &tv);
    }

    tv = CalTimeDiff_timeval(&serving_packet->Q1_arrival, &serving_packet->Server_leave);
    time_in_system_avg = CalAvgTime(packet_serviced_count, &time_in_system_avg, &tv);
    UpdateVariance(&tv);

    packet_serviced_count++;
    (*packet_served)++;
    pthread_mutex_unlock(&m);

    free(serving_packet);

    pthread_mutex_lock(&m);
    if (packet_serviced_count + packet_dropped >= num)
    {
        kill(getpid(), SIGUSR1);
        pthread_cond_broadcast(&cv);
        pthread_mutex_unlock(&m);
    }
    pthread_mutex_unlock(&m);
}

// server thread first proc
void *t_server(void *arg) // process rate
{
    int server_index = *(int *)arg;
    Packet *serving_packet = 0;
    int packet_served = 0;

    while (1)
    {
        pthread_mutex_lock(&m);
        if (stop_flag == 1)
        {
            pthread_mutex_unlock(&m);
            break;
        }

        while (!(Q2.num_members > 0))
        {
            if (stop_flag == 1)
            {
                pthread_mutex_unlock(&m);
                return NULL;
            }
            pthread_cond_wait(&cv, &m);
        }

        TakeFirstPacketFromQ2(server_index, &serving_packet);
        pthread_mutex_unlock(&m);

        ServicePacket(server_index, serving_packet, &packet_served);
    }
    return NULL;
}

void *t_sig_handler(void *arg)
{
    int sig;
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGUSR1);
    sigwait(&set, &sig);

    if (sig == SIGINT)
    {
        struct timeval sig_caught_tv;
        gettimeofday(&sig_caught_tv, 0);
        pthread_mutex_lock(&m);
        logLine("SIGINT caught", &sig_caught_tv);
        pthread_mutex_unlock(&m);
    }

    pthread_mutex_lock(&m);
    stop_flag = 1;

    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&m);

    return NULL;
}

void cleanQueue(My402List *Q, int idx)
{
    My402ListElem *ele = Q->First(Q);

    while (ele != NULL)
    {
        int p_idx = ((Packet *)(ele->obj))->index;
        free(ele->obj);
        ele = Q->Next(Q, ele);

        struct timeval unlink_tv;
        gettimeofday(&unlink_tv, 0);
        snprintf(buffer, sizeof(buffer), "p%d removed from Q%d", p_idx + 1, idx);
        logLine(buffer, &unlink_tv);
    }
}

void DisplayStatistics()
{
    printf("\nStatistics:\n\n");
    printf("\taverage packet inter-arrival time = %g\n", (double)arrive_time_avg_milliseconds / 1000.0);
    printf("\taverage packet service time = %ld.%06ld\n", packet_service_time_avg.tv_sec, packet_service_time_avg.tv_usec);

    printf("\n");
    struct timeval duration = CalTimeDiff_timeval(&start_time, &end_time);
    printf("\taverage number of packets in Q1 = %.6g\n", packet_serviced_count * CalTimevalDevision(&Q1_packet_time_avg, &duration));
    printf("\taverage number of packets in Q2 = %.6g\n", packet_serviced_count * CalTimevalDevision(&Q2_packet_time_avg, &duration));
    printf("\taverage number of packets at S1 = %.6g\n", S1_packet_count * CalTimevalDevision(&S1_packet_time_avg, &duration));
    printf("\taverage number of packets at S2 = %.6g\n", S2_packet_count * CalTimevalDevision(&S2_packet_time_avg, &duration));

    printf("\n");
    printf("\taverage time a packet spent in system = %ld.%06ld\n", time_in_system_avg.tv_sec, time_in_system_avg.tv_usec);
    printf("\tstandard deviation for time spent in system = %.6g\n", sqrt(X2 - X * X));

    printf("\n");
    printf("\ttoken drop probability = %.6g\n", (double)token_dropped / (double)(token_counter - 1));
    printf("\tpacket drop probability = %.6g\n", (double)packet_dropped / (double)(packet_serviced_count + packet_dropped));
}

void Process(int fd)
{
    // mask sigint on main thread
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGUSR1);
    sigprocmask(SIG_BLOCK, &set, NULL);

    logLine("emulation begins", &start_time);

    pthread_t packet_thread, token_thread, s1_thread, s2_thread, sig_thread;

    int s1_index = 1;
    int s2_index = 2;

    pthread_create(&packet_thread, NULL, t_packet, NULL);
    pthread_create(&token_thread, NULL, t_token, NULL);
    pthread_create(&s1_thread, NULL, t_server, (void *)(&s1_index));
    pthread_create(&s2_thread, NULL, t_server, (void *)(&s2_index));
    pthread_create(&sig_thread, NULL, t_sig_handler, NULL);

    pthread_join(packet_thread, NULL);
    pthread_join(token_thread, NULL);
    pthread_join(s1_thread, NULL);
    pthread_join(s2_thread, NULL);
    pthread_join(sig_thread, NULL);

    // clean up
    cleanQueue(&Q1, 1);
    cleanQueue(&Q2, 2);

    gettimeofday(&end_time, 0);

    logLine("emulation ends", &end_time);
}

/* ----------------------- main() ----------------------- */

int main(int argc, char *argv[])
{
    int fd;

    SetProgramName(*argv);
    ProcessOptions(argc, argv, &fd);
    Init();

    PrintParams();

    Process(fd);

    DisplayStatistics();

    return 0;
}