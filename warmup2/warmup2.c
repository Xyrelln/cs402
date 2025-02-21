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

#include "cs402.h"

#include "my402list.h"

#define BUFFER_SIZE 200
#define MAX_VALID_LINE_CHAR 1024

typedef struct formatted_time
{
    long milliseconds;
    long microseconds;
} formatted_time;

static char gszProgName[MAXPATHLENGTH];
static int mode = 0; // 0 for deterministic, 1 for trace-driven
static int lineNum = 1;

// default values
static double lambda = 1.0;
static double mu = 0.35;
static double r = 1.5;
static int B = 10;   // bucket capacity
static int P = 3;    // tokens required for packet
static int num = 20; // how many packets

// serialization box and mutex
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
char buffer[BUFFER_SIZE];
static struct timeval start_time;
static My402List Q1, Q2;
static int Q1_packet_count = 0; // the largest packet index Q1 received
static int bucket = 0;
static int packet_serviced_count = 0;
static struct timeval packet_service_time_avg;
static int stop_flag = 0;

// statistic
static int arrived_count = 0;
static long long arrive_time_avg_milliseconds = 0;

static struct timeval last_packet_arrival;

typedef struct Packet
{
    struct timeval Q1_arrival, Q1_leave, Q2_arrival, Q2_leave, Server_arrival, Server_leave; // for statistics
    int initial_arrival_delay;
    int token_needed;
    int service_time;
    int index;
} Packet;

/* ----------------------- Utility Functions ----------------------- */

static void Usage()
{
    fprintf(stderr,
            "usage: %s %s\n",
            gszProgName, "warmup2 [-lambda lambda] [-mu mu] [-r r] [-B B] [-P P] [-n num] [-t tsfile]");
    exit(-1);
}

static void ProcessOptions(int argc, char *argv[], int *fd)
{
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-n") == 0)
        {
            if (i + 1 >= argc)
                Usage();
            num = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-lambda") == 0)
        {
            if (i + 1 >= argc)
                Usage();
            lambda = atof(argv[++i]);
        }
        else if (strcmp(argv[i], "-mu") == 0)
        {
            if (i + 1 >= argc)
                Usage();
            mu = atof(argv[++i]);
        }
        else if (strcmp(argv[i], "-r") == 0)
        {
            if (i + 1 >= argc)
                Usage();
            r = atof(argv[++i]);
        }
        else if (strcmp(argv[i], "-B") == 0)
        {
            if (i + 1 >= argc)
                Usage();
            B = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-P") == 0)
        {
            if (i + 1 >= argc)
                Usage();
            P = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-t") == 0)
        {
            if (i + 1 >= argc)
                Usage();
            close(0);
            *fd = open(argv[++i], O_RDONLY);
            if (*fd < 0)
            {
                perror("Error opening trace file");
                exit(EXIT_FAILURE);
            }
            else if (*fd != 0)
            {
                perror("tsfile not opened as stdin");
            }
            strcpy(buffer, argv[i]);
            mode = 1; // set mode to trace-driven when -t is used

            // when using tsfile, read the first line for num of packets
            // read how many packets
            if (fgets(buffer, BUFFER_SIZE, stdin) != NULL)
            {
                if (strlen(buffer) > MAX_VALID_LINE_CHAR)
                {
                    fprintf(stderr, "error: line 1 has more than %d chars\n", MAX_VALID_LINE_CHAR);
                    exit(1);
                }
                num = atoi(buffer);
                lineNum++;
            }
            else
            {
                perror("tsfile empty\n");
                exit(1);
            }
        }
        else
        {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            Usage();
        }
    }
}

static void SetProgramName(const char *s)
{
    char *c_ptr = strrchr(s, DIR_SEP);

    if (c_ptr == NULL)
    {
        strcpy(gszProgName, s);
    }
    else
    {
        strcpy(gszProgName, ++c_ptr);
    }
}

static void PrintParams()
{
    printf("Emulation Parameters:\n");
    printf("\tnumber to arrive = %d\n", num);
    if (mode == 0)
    {
        printf("\tlambda = %d\n", num);
        printf("\tmu = %g\n", mu);
    }
    printf("\tr = %g\n", r);
    printf("\tB = %d\n", B);
    if (mode == 0)
        printf("\tP = %d\n", P);
    if (mode == 1)
        printf("\ttsfile = %s\n", buffer);
}

static formatted_time CalTimeDiff(const struct timeval *former, const struct timeval *latter)
{
    long elapsed_sec = latter->tv_sec - former->tv_sec;
    long elapsed_usec = latter->tv_usec - former->tv_usec;

    // negative microseconds
    if (elapsed_usec < 0)
    {
        elapsed_sec -= 1;
        elapsed_usec += 1000000;
    }

    formatted_time ft;
    ft.milliseconds = elapsed_sec * 1000 + elapsed_usec / 1000;
    ft.microseconds = elapsed_usec % 1000;

    return ft;
}

static formatted_time CalElapsed(const struct timeval *original)
{
    struct timeval timestamp;
    gettimeofday(&timestamp, 0);

    return CalTimeDiff(original, &timestamp);
}

struct timeval CalAvgTime(int prev_count, struct timeval prev_avg, struct timeval new_timeval)
{
    if (prev_count == 0)
    {
        return new_timeval;
    }

    uint64_t total_prev = ((uint64_t)prev_avg.tv_sec * 1000000ULL + prev_avg.tv_usec) * prev_count;
    uint64_t total_new = (uint64_t)new_timeval.tv_sec * 1000000ULL + new_timeval.tv_usec;
    uint64_t total_sum = total_prev + total_new;
    int new_count = prev_count + 1;

    uint64_t avg_us = total_sum / new_count;

    struct timeval result;
    result.tv_sec = avg_us / 1000000ULL;
    result.tv_usec = avg_us % 1000000ULL;

    return result;
}

struct timeval CalTimeDiff_timeval(const struct timeval *former, const struct timeval *latter)
{
    struct timeval diff;

    long elapsed_sec = latter->tv_sec - former->tv_sec;
    long elapsed_usec = latter->tv_usec - former->tv_usec;

    // negative microseconds
    if (elapsed_usec < 0)
    {
        elapsed_sec -= 1;
        elapsed_usec += 1000000;
    }

    diff.tv_sec = elapsed_sec;
    diff.tv_usec = elapsed_usec;
    return diff;
}

static void log(const char *message)
{
    formatted_time ft = CalElapsed(&start_time);
    printf("%08ld.%03ldms: %s\n", ft.milliseconds, ft.microseconds, message);
}

static void setLastPacketTime(const struct timeval *timeval)
{
    last_packet_arrival.tv_sec = timeval->tv_sec;
    last_packet_arrival.tv_usec = timeval->tv_usec;
}

void Init()
{
    if (My402ListInit(&Q1) != 1)
    {
        perror("error: error creating My402List\n");
        exit(1);
    }

    if (My402ListInit(&Q2) != 1)
    {
        perror("error: error creating My402List\n");
        exit(1);
    }

    gettimeofday(&start_time, 0);
    setLastPacketTime(&start_time);

    packet_service_time_avg.tv_sec = 0;
    packet_service_time_avg.tv_usec = 0;
}

/* ----------------------- packet distributer ----------------------- */

void ResumePacketToQ2()
{
    Packet *first = (Packet *)(Q1.First(&Q1)->obj);
    Q1.Unlink(&Q1, Q1.First(&Q1));
    gettimeofday(&first->Q1_leave, 0);

    // log leave Q1
    formatted_time ft_diff = CalTimeDiff(&first->Q1_arrival, &first->Q1_leave);
    snprintf(buffer, sizeof(buffer), "p%d leaves Q1, time in Q1 = %ld.%03ldms, token bucket now has %d token", first->index, ft_diff.milliseconds, ft_diff.microseconds, bucket);
    log(buffer);

    Q2.Append(&Q2, first);
    gettimeofday(&first->Q2_arrival, 0);

    // log arrive at Q2
    snprintf(buffer, sizeof(buffer), "p%d enters Q2", first->index);
    log(buffer);
}

// must have access to box
void AppendPacketToQ1(Packet *packet)
{
    Q1.Append(&Q1, packet);
    Q1_packet_count++;

    snprintf(buffer, sizeof(buffer), "p%d enters Q1", packet->index);
    log(buffer);

    gettimeofday(&packet->Q1_arrival, 0);

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

    if (packet->token_needed > B)
    {
        formatted_time ft = CalElapsed(&last_packet_arrival);
        snprintf(buffer, sizeof(buffer), "p%d arrives, needs %d tokens, inter-arrival time = %ld.%03ldms, dropped", packet->index, packet->token_needed, ft.milliseconds, ft.microseconds);
        log(buffer);
        gettimeofday(&packet->Q1_arrival, 0);
        setLastPacketTime(&packet->Q1_arrival);

        pthread_mutex_lock(&m);
        Q1_packet_count = packet_counter + 1;
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

    formatted_time ft = CalElapsed(&last_packet_arrival);
    snprintf(buffer, sizeof(buffer), "p%d arrives, needs %d tokens, inter-arrival time = %ld.%03ldms", packet_counter, packet->token_needed, ft.milliseconds, ft.microseconds);
    log(buffer);

    AppendPacketToQ1(packet);

    gettimeofday(&packet->Q1_arrival, 0);
    setLastPacketTime(&packet->Q1_arrival);

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
    int token_counter = 1;
    while (1)
    {
        pthread_mutex_lock(&m);
        if (stop_flag == 1)
        {
            pthread_mutex_unlock(&m);
            return NULL;
        }
        pthread_mutex_unlock(&m);

        usleep(interval_millisecond * 1000);

        pthread_mutex_lock(&m);
        if (stop_flag == 1)
        {
            pthread_mutex_unlock(&m);
            return NULL;
        }
        pthread_mutex_unlock(&m);

        if (bucket < B)
        {
            bucket++;
            snprintf(buffer, sizeof(buffer), "token t%d arrives, token bucket now has %d %s", token_counter, bucket, bucket > 1 ? "tokens" : "token");
            log(buffer);
        }
        else
        {
            snprintf(buffer, sizeof(buffer), "token t%d arrives, dropped", token_counter);
            log(buffer);
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
    snprintf(buffer, sizeof(buffer), "p%d begins service at S%d, requesting %dms of service", serving_packet->index, server_index, serving_packet->service_time);
    log(buffer);

    gettimeofday(&serving_packet->Server_arrival, 0);
}

void ServicePacket(int server_index, Packet *serving_packet)
{
    usleep(serving_packet->service_time * 1000);

    gettimeofday(&serving_packet->Server_leave, 0);

    // log service
    const formatted_time ft_diff = CalTimeDiff(&serving_packet->Server_arrival, &serving_packet->Server_leave);
    const formatted_time ft_time_in_system = CalTimeDiff(&serving_packet->Q1_arrival, &serving_packet->Server_leave);
    snprintf(buffer, sizeof(buffer), "p%d departs from S%d, service time = %ld.%03ldms, time in system = %ld.%03ldms", serving_packet->index, server_index, ft_diff.milliseconds, ft_diff.microseconds, ft_time_in_system.milliseconds, ft_time_in_system.microseconds);
    log(buffer);

    // statistics
    pthread_mutex_lock(&m);
    packet_service_time_avg = CalAvgTime(packet_serviced_count, packet_service_time_avg, CalTimeDiff_timeval(&serving_packet->Server_arrival, &serving_packet->Server_leave));

    free(serving_packet);

    pthread_mutex_lock(&m);
    packet_serviced_count++;
    if (packet_serviced_count >= num)
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

        ServicePacket(server_index, serving_packet);
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
        log("SIGINT caught");
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

        snprintf(buffer, sizeof(buffer), "p%d removed from Q%d", p_idx, idx);
        log(buffer);
    }
}

void DisplayStatistics()
{
    printf("\nStatistics:\n\n");
    printf("\taverage packet inter-arrival time = %gs\n", (double)arrive_time_avg_milliseconds / 1000.0);
}

void Process(int fd)
{
    // mask sigint on main thread
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGUSR1);
    sigprocmask(SIG_BLOCK, &set, NULL);

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
}

/* ----------------------- main() ----------------------- */

int main(int argc, char *argv[])
{
    int fd;

    SetProgramName(*argv);
    ProcessOptions(argc, argv, &fd);
    Init();

    PrintParams();
    log("emulation begins");

    Process(fd);

    log("emulation ends");

    DisplayStatistics();

    return 0;
}