/*
 * Author:      William Chia-Wei Cheng (bill.cheng@usc.edu)
 *
 * @(#)$Id: listtest.c,v 1.2 2020/05/18 05:09:12 william Exp $
 */

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

#include "cs402.h"

#include "my402list.h"

#define BUFFER_SIZE 2000
#define MAX_VALID_LINE_CHAR 1024
#define MAX_DESCRIPTION_LENGTH 24

static char gszProgName[MAXPATHLENGTH];
static char tfileName[MAXPATHLENGTH] = {0};
static const int rowLength[] = {17, 26, 16, 16};

/* ----------------------- Utility Functions ----------------------- */

static void Usage()
{
    fprintf(stderr,
            "usage: %s %s\n",
            gszProgName, "sort [tfile]");
    exit(-1);
}

static int isValidFileName(const char *filename)
{
    int p = 0;
    while (filename[p++] != 0)
        ;
    while (p > 0)
    {
        if (filename[p] != '/')
            p--;
        else
            break;
    }
    if (filename[p] == '/')
        p++;
    if (filename[p] == 'f')
    {
        p++;
        for (int i = p; filename[i] != 0; i++)
        {
            if (!isdigit(filename[i]))
            {
                return 0;
            }
        }
        return 1;
    }
    return 0;
}

static void ProcessOptions(int argc, char *argv[], int *fd)
{
    if (argc > 1)
    {
        if (strcmp(argv[1], "sort") != 0)
        {
            fprintf(stderr, "malformed command, \"%s\" is not a valid commandline option\n", argv[1]);
            Usage();
        }

        if (argc == 3)
        {
            strcpy(tfileName, argv[2]);

            if (isValidFileName(tfileName) == 0)
            {
                fprintf(stderr, "input file \"%s\" is in the wrong format\n", tfileName);
                exit(1);
            }
            if (access(tfileName, F_OK) == -1)
            {
                fprintf(stderr, "input file \"%s\" does not exist\n", tfileName);
                exit(1);
            }

            if (access(tfileName, R_OK) == -1)
            {
                fprintf(stderr, "input file \"%s\" cannot be opened - access denied\n", tfileName);
                exit(1);
            }

            struct stat file_stat;
            if (stat(tfileName, &file_stat) == 0 && S_ISDIR(file_stat.st_mode))
            {
                fprintf(stderr, "input file \"%s\" is a directory or input file is not in the right format\n", tfileName);
                exit(1);
            }
            close(0);
            if ((*fd = open(tfileName, O_RDONLY)) == -1)
            {
                perror(tfileName);
                exit(1);
            }
        }
    }
    else
    {
        Usage();
        exit(1);
    }
}

static void SetProgramName(char *s)
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

static void printTableHeader()
{
    char row_data[81] = {0};
    row_data[0] = '+';
    int index = 1;
    for (int i = 0; i < sizeof(rowLength) / sizeof(int); i++)
    {
        for (int j = 0; j < rowLength[i]; j++)
        {
            row_data[index++] = '-';
        }
        row_data[index++] = '+';
    }
    printf("%s\n", row_data);
}

/* ----------------------- Program Functions ------------------------ */

typedef struct tagTransactionNode
{
    int isDeposit;
    time_t timestamp;
    int amount_cents;
    char description[MAX_DESCRIPTION_LENGTH + 1];
} TransactionNode;

static void BubbleForward(My402List *pList, My402ListElem **pp_elem1, My402ListElem **pp_elem2)
/* (*pp_elem1) must be closer to First() than (*pp_elem2) */
{
    My402ListElem *elem1 = (*pp_elem1), *elem2 = (*pp_elem2);
    void *obj1 = elem1->obj, *obj2 = elem2->obj;
    My402ListElem *elem1prev = My402ListPrev(pList, elem1);
    /*  My402ListElem *elem1next=My402ListNext(pList, elem1); */
    /*  My402ListElem *elem2prev=My402ListPrev(pList, elem2); */
    My402ListElem *elem2next = My402ListNext(pList, elem2);

    My402ListUnlink(pList, elem1);
    My402ListUnlink(pList, elem2);
    if (elem1prev == NULL)
    {
        (void)My402ListPrepend(pList, obj2);
        *pp_elem1 = My402ListFirst(pList);
    }
    else
    {
        (void)My402ListInsertAfter(pList, obj2, elem1prev);
        *pp_elem1 = My402ListNext(pList, elem1prev);
    }
    if (elem2next == NULL)
    {
        (void)My402ListAppend(pList, obj1);
        *pp_elem2 = My402ListLast(pList);
    }
    else
    {
        (void)My402ListInsertBefore(pList, obj1, elem2next);
        *pp_elem2 = My402ListPrev(pList, elem2next);
    }
}

static void BubbleSortForwardList(My402List *pList, int num_items)
{
    My402ListElem *elem = NULL;
    int i = 0;

    if (My402ListLength(pList) != num_items)
    {
        fprintf(stderr, "error: list length is not %1d in BubbleSortForwardList()\n", num_items);
        exit(1);
    }
    for (i = 0; i < num_items; i++)
    {
        int j = 0, something_swapped = FALSE;
        My402ListElem *next_elem = NULL;

        for (elem = My402ListFirst(pList), j = 0; j < num_items - i - 1; elem = next_elem, j++)
        {
            time_t cur_val = (time_t)(((TransactionNode *)(elem->obj))->timestamp), next_val = 0;

            next_elem = My402ListNext(pList, elem);
            next_val = (time_t)(((TransactionNode *)(next_elem->obj))->timestamp);

            if (cur_val > next_val)
            {
                BubbleForward(pList, &elem, &next_elem);
                something_swapped = TRUE;
            }
        }
        if (!something_swapped)
            break;
    }
}

static void Process(int fd)
{
    // open file to stdin
    if (strlen(tfileName) > 0)
    {
    }

    // create the list
    My402List transactions;
    memset(&transactions, 0, sizeof(My402List));
    if (My402ListInit(&transactions) != 1)
    {
        perror("error: error creating My402List\n");
        exit(1);
    }

    // read file line by line
    char buffer[BUFFER_SIZE] = {0};
    int tfileLineNumber = 0;
    while (fgets(buffer, BUFFER_SIZE, stdin) != NULL)
    {
        // line too long
        if (strlen(buffer) > MAX_VALID_LINE_CHAR)
        {
            fprintf(stderr, "error: line %d has more than %d chars\n", tfileLineNumber, MAX_VALID_LINE_CHAR);
            exit(1);
        }

        // new obj
        TransactionNode *node = (TransactionNode *)malloc(sizeof(TransactionNode));
        if (node == NULL)
        {
            perror("error: failed to malloc space for new transaction node\n");
            exit(1);
        }
        node->isDeposit = -1;

        // read single line char by char
        int section = 0;
        int amount_cents = 0;
        int amount_index = 0;
        time_t timestamp = 0;
        char description[25] = {'\0'};
        int description_index = 0;
        const time_t curr_time = time(0);
        for (int i = 0; i < strlen(buffer); i++) // each char in line
        {
            if (section == 0)
            {
                if (buffer[i] == '\t')
                {
                    if (node->isDeposit == -1)
                    {
                        fprintf(stderr, "error: deposit sign not found on line %d: %d\n", tfileLineNumber, (int)buffer[i + 1]);
                        exit(1);
                    }
                    section++;
                    continue;
                }

                if (buffer[i + 1] != 9)
                {
                    fprintf(stderr, "error: second char is not tab on line %d: %d\n", tfileLineNumber, (int)buffer[i + 1]);
                    exit(1);
                }

                if (buffer[i] == '+')
                {
                    node->isDeposit = 1;
                }
                else if (buffer[i] == '-')
                {
                    node->isDeposit = 0;
                }
                else
                {
                    fprintf(stderr, "error: invalid deposit sign '%c' on line %d\n", buffer[i], tfileLineNumber);
                    exit(1);
                }
            }
            else if (section == 1)
            {
                // timestamp
                if (buffer[i] == '\t')
                {
                    if (timestamp >= (long)1e11 || timestamp > curr_time)
                    {
                        fprintf(stderr, "error: invalid timestamp which is greater than current time on line %d\n", tfileLineNumber);
                        exit(1);
                    }
                    node->timestamp = timestamp;
                    section++;
                    continue;
                }

                if (buffer[i] < '0' || buffer[i] > '9')
                {
                    fprintf(stderr, "error: invalid timestamp character on line %d\n", tfileLineNumber);
                    exit(1);
                }
                timestamp = timestamp * 10 + (buffer[i] - '0');
            }
            else if (section == 2)
            {
                // amount
                if (amount_index == 0 && buffer[i] == '0' && buffer[i + 1] != '.')
                {
                    fprintf(stderr, "error: first charactor of transaction amount is 0 on line %d\n", tfileLineNumber);
                    exit(1);
                }

                if (buffer[i] == '\t')
                {
                    if (amount_cents >= (long)1e9)
                    {
                        fprintf(stderr, "error: transaction amount more than 10 millions on line %d: %d\n", tfileLineNumber, amount_cents);
                        exit(1);
                    }
                    node->amount_cents = amount_cents;
                    section++;
                }
                else if (buffer[i] == '.')
                {
                    if (buffer[i + 3] != '\t')
                    {
                        fprintf(stderr, "error: decimal number isn't 2 digits on line %d\n", tfileLineNumber);
                        exit(1);
                    }
                }
                else if (buffer[i] >= '0' && buffer[i] <= '9')
                {
                    amount_cents = amount_cents * 10 + (buffer[i] - '0');
                    amount_index++;
                }
                else
                {
                    fprintf(stderr, "error: invalid transaction amount on line %d, char is '%c'\n", tfileLineNumber, buffer[i]);
                    exit(1);
                }
            }
            else if (section == 3)
            {
                // description
                if (buffer[i] == '\n')
                {
                    strcpy(node->description, description);
                }
                else if (buffer[i] == '\t')
                {
                    fprintf(stderr, "error: Line %d: only 4 fields are allowed, found 5\n", tfileLineNumber);
                    exit(1);
                }
                else if (buffer[i] != '\0')
                {
                    if (description_index >= 24)
                    {
                        strcpy(node->description, description);
                        break;
                    }
                    description[description_index++] = buffer[i];
                }
                else
                {
                    fprintf(stderr, "error: Line %d: char %d is %c which is invalid\n", tfileLineNumber, i, buffer[i]);
                    exit(1);
                }
            }
        }

        // not 3 tabs
        if (section != 3)
        {
            fprintf(stderr, "line %d does not have 4 sections\n", tfileLineNumber);
            exit(1);
        }

        (void)transactions.Append(&transactions, node);

        tfileLineNumber++;
    }

    // check file empty
    if (tfileLineNumber == 0)
    {
        perror("file empty");
        exit(1);
    }

    BubbleSortForwardList(&transactions, tfileLineNumber);

    // write to file
    printTableHeader();
    printf("|       Date      | Description              |         Amount |        Balance |\n");
    printTableHeader();

    // print out the table
    int balance = 0;
    My402ListElem *curr = transactions.First(&transactions);
    for (int i = 0; i < tfileLineNumber; i++)
    {
        for (int section = 0; section < 4; section++)
        {
            TransactionNode *node = (TransactionNode *)(curr->obj);
            switch (section)
            {
            case 0:
            {
                char data[18] = {0}; // 17 chars with \0
                struct tm *time_info;
                time_info = localtime(&(node->timestamp));
                strftime(data, sizeof(data), "%a %b %e %Y", time_info);
                printf("| %s ", data);
                break;
            }
            case 1:
            {
                printf("| %-24s ", ((TransactionNode *)(curr->obj))->description);
                break;
            }
            case 2:
            {
                const int dollars = node->amount_cents / 100;
                const int cents = node->amount_cents % 100;

                char buffer[20];
                snprintf(buffer, sizeof(buffer), "%d", dollars);

                char formatted[25];
                int j = 0, mod = strlen(buffer) % 3;
                for (int i = 0; i < strlen(buffer); i++)
                {
                    if (i > 0 && (i - mod) % 3 == 0)
                    {
                        formatted[j++] = ',';
                    }
                    formatted[j++] = buffer[i];
                }

                formatted[j] = '\0';
                printf("| %c %8s.%02d%c ", node->isDeposit ? ' ' : '(', formatted, cents, node->isDeposit ? ' ' : ')');
                break;
            }
            case 3:
                if (node->isDeposit)
                {
                    balance += node->amount_cents;
                }
                else
                {
                    balance -= node->amount_cents;
                }
                int balance_dollar = balance / 100;
                if (balance >= 1e8)
                {
                    printf("|   ?,???,???.?? |\n");
                    break;
                }
                char buffer[20];
                snprintf(buffer, sizeof(buffer), "%d", abs(balance_dollar));

                char formatted[25];
                int j = 0, mod = strlen(buffer) % 3;
                for (int i = 0; i < strlen(buffer); i++)
                {
                    if (i > 0 && (i - mod) % 3 == 0)
                    {
                        formatted[j++] = ',';
                    }
                    formatted[j++] = buffer[i];
                }

                formatted[j] = '\0';

                printf("| %c %8s.%02d%c |\n", balance >= 0 ? ' ' : '(', formatted, abs(balance % 100), balance >= 0 ? ' ' : ')');
                break;
            }
        }

        curr = transactions.Next(&transactions, curr);
    }
    printTableHeader();
}

/* ----------------------- main() ----------------------- */

int main(int argc, char *argv[])
{
    int fd;

    SetProgramName(*argv);
    ProcessOptions(argc, argv, &fd);

    Process(fd);
    return 0;
}
