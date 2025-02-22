#include "utils.h"

char gszProgName[MAXPATHLENGTH];
int mode = 0; // 0 for deterministic, 1 for trace-driven
int lineNum = 1;

// default values
double lambda = 1.0;
double mu = 0.35;
double r = 1.5;
int B = 10;   // bucket capacity
int P = 3;    // tokens required for packet
int num = 20; // how many packets

// serialization box and mutex
pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
char buffer[BUFFER_SIZE];
struct timeval start_time;
struct timeval end_time;
My402List Q1, Q2;
int Q1_packet_count = 0; // the largest packet index Q1 received
struct timeval Q1_packet_time_avg = {0, 0};
struct timeval Q2_packet_time_avg = {0, 0};
int S1_packet_count = 0;
int S2_packet_count = 0;
struct timeval S1_packet_time_avg = {0, 0};
struct timeval S2_packet_time_avg = {0, 0};
struct timeval time_in_system_avg = {0, 0};
double X = 0.0;
double X2 = 0.0;
int bucket = 0;
int packet_serviced_count = 0;
int packet_dropped = 0;
struct timeval packet_service_time_avg = {0, 0};
int stop_flag = 0;

int token_counter = 1; // actually this is amount + 1, bad naming but too lazy
int token_dropped = 0;

// statistic
int arrived_count = 0;
struct timeval arrive_time_avg = {0, 0};

struct timeval last_packet_arrival = {0, 0};

void Usage()
{
    fprintf(stderr,
            "usage: %s %s\n",
            gszProgName, "warmup2 [-lambda lambda] [-mu mu] [-r r] [-B B] [-P P] [-n num] [-t tsfile]");
    exit(-1);
}

void ProcessOptions(int argc, char *argv[], int *fd)
{
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-n") == 0)
        {
            if (i + 1 >= argc || argv[i + 1][0] == '-')
            {
                fprintf(stderr, "malformed command, value for \"-n\" is not given\n");
                exit(EXIT_FAILURE);
            }
            num = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-lambda") == 0)
        {
            if (i + 1 >= argc || argv[i + 1][0] == '-')
            {
                fprintf(stderr, "malformed command, value for \"-lambda\" is not given\n");
                exit(EXIT_FAILURE);
            }
            lambda = atof(argv[++i]);
        }
        else if (strcmp(argv[i], "-mu") == 0)
        {
            if (i + 1 >= argc || argv[i + 1][0] == '-')
            {
                fprintf(stderr, "malformed command, value for \"-mu\" is not given\n");
                exit(EXIT_FAILURE);
            }
            mu = atof(argv[++i]);
        }
        else if (strcmp(argv[i], "-r") == 0)
        {
            if (i + 1 >= argc || argv[i + 1][0] == '-')
            {
                fprintf(stderr, "malformed command, value for \"-r\" is not given\n");
                exit(EXIT_FAILURE);
            }
            r = atof(argv[++i]);
        }
        else if (strcmp(argv[i], "-B") == 0)
        {
            if (i + 1 >= argc || argv[i + 1][0] == '-')
            {
                fprintf(stderr, "malformed command, value for \"-B\" is not given\n");
                exit(EXIT_FAILURE);
            }
            B = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-P") == 0)
        {
            if (i + 1 >= argc || argv[i + 1][0] == '-')
            {
                fprintf(stderr, "malformed command, value for \"-P\" is not given\n");
                exit(EXIT_FAILURE);
            }
            P = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-t") == 0)
        {
            if (i + 1 >= argc || argv[i + 1][0] == '-')
            {
                fprintf(stderr, "malformed command, value for \"-t\" is not given\n");
                exit(EXIT_FAILURE);
            }
            char *filename = argv[++i];
            close(0);
            *fd = open(filename, O_RDONLY);
            if (*fd < 0)
            {
                switch (errno)
                {
                case EISDIR:
                    fprintf(stderr, "input file %s is a directory or line 1 is not just a number\n", filename);
                    break;
                case EACCES:
                case EPERM:
                    fprintf(stderr, "input file %s cannot be opened - access denied\n", filename);
                    break;
                case ENOENT:
                    fprintf(stderr, "input file %s does not exist\n", filename);
                    break;
                default:
                    perror("Error opening trace file");
                }
                exit(EXIT_FAILURE);
            }
            else if (*fd != 0)
            {
                fprintf(stderr, "tsfile not opened as stdin\n");
                exit(EXIT_FAILURE);
            }
            strcpy(buffer, filename);
            mode = 1;

            if (fgets(buffer, BUFFER_SIZE, stdin) != NULL)
            {
                if (strlen(buffer) > MAX_VALID_LINE_CHAR)
                {
                    fprintf(stderr, "error: line 1 has more than %d chars\n", MAX_VALID_LINE_CHAR);
                    exit(EXIT_FAILURE);
                }
                buffer[strcspn(buffer, "\n")] = '\0';
                char *endptr;
                num = strtol(buffer, &endptr, 10);
                if (endptr == buffer || *endptr != '\0')
                {
                    fprintf(stderr, "malformed input - line 1 is not just a number\n");
                    exit(EXIT_FAILURE);
                }
                lineNum++;
            }
            else
            {
                fprintf(stderr, "tsfile empty\n");
                exit(EXIT_FAILURE);
            }
        }
        else
        {
            fprintf(stderr, "malformed command, \"%s\" is not a valid commandline option\n", argv[i]);
            exit(EXIT_FAILURE);
        }
    }
}
void SetProgramName(const char *s)
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

void PrintParams()
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

long CalTimevalMilliseconds(const struct timeval *tv)
{
    return tv->tv_sec * 1000 + tv->tv_usec / 1000;
}

long CalTimevalMicroeconds(const struct timeval *tv)
{
    return tv->tv_usec % 1000;
}

struct timeval CalAvgTime(int prev_count, struct timeval *prev_avg, struct timeval *new_timeval)
{
    struct timeval result;
    if (prev_count == 0)
    {
        result.tv_sec = new_timeval->tv_sec;
        result.tv_usec = new_timeval->tv_usec;
        return result;
    }

    uint64_t total_prev = ((uint64_t)prev_avg->tv_sec * 1000000ULL + prev_avg->tv_usec) * prev_count;
    uint64_t total_new = (uint64_t)new_timeval->tv_sec * 1000000ULL + new_timeval->tv_usec;
    uint64_t total_sum = total_prev + total_new;
    int new_count = prev_count + 1;

    uint64_t avg_us = total_sum / new_count;

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

struct timeval CalTimevalAdd(const struct timeval *a, const struct timeval *b)
{
    int64_t a_us = (int64_t)a->tv_sec * 1000000LL + a->tv_usec;
    int64_t b_us = (int64_t)b->tv_sec * 1000000LL + b->tv_usec;
    int64_t sum_us = a_us + b_us;

    struct timeval result;
    result.tv_sec = sum_us / 1000000LL;
    result.tv_usec = sum_us % 1000000LL;

    if (result.tv_usec < 0)
    {
        result.tv_usec += 1000000LL;
        result.tv_sec -= 1;
    }

    return result;
}

struct timeval CalTimevalMult(struct timeval *t, double multiplier)
{
    struct timeval result;
    double total_seconds = t->tv_sec + t->tv_usec / 1000000.0;
    double new_total = total_seconds * multiplier;
    result.tv_sec = (time_t)new_total;
    result.tv_usec = (suseconds_t)((new_total - result.tv_sec) * 1000000);

    return result;
}

struct timeval CalTimevalDevision_int(struct timeval *t, int divisor)
{
    struct timeval result;
    if (divisor == 0)
    {
        fprintf(stderr, "Error: Division by zero is not allowed.\n");
        exit(1);
    }

    long total_usec = t->tv_sec * 1000000L + t->tv_usec;

    long quotient = total_usec / divisor;

    result.tv_sec = quotient / 1000000;
    result.tv_usec = quotient % 1000000;

    return result;
}

double CalTimevalDevision(struct timeval *t1, struct timeval *t2)
{
    /* Convert both timevals to seconds */
    double seconds1 = t1->tv_sec + t1->tv_usec / 1000000.0;
    double seconds2 = t2->tv_sec + t2->tv_usec / 1000000.0;

    if (seconds2 == 0.0)
    {
        fprintf(stderr, "Error: Division by zero\n");
        exit(EXIT_FAILURE);
    }

    return seconds1 / seconds2;
}

double TimevalToDouble(struct timeval *t)
{
    return t->tv_sec + t->tv_usec / 1000000.0;
}

void logLine(const char *message, const struct timeval *tv)
{
    struct timeval time_since_start_tv = CalTimeDiff_timeval(&start_time, tv);
    printf("%08ld.%03ldms: %s\n", CalTimevalMilliseconds(&time_since_start_tv), CalTimevalMicroeconds(&time_since_start_tv), message);
}

void setLastPacketTime(const struct timeval *timeval)
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
}
