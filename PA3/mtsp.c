#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <limits.h>
#include <memory.h>
#include <pthread.h>
#include <semaphore.h>

#define MAX_SUBTASK 11

int cities[51][51]; // Map of city distance
int minPath[51] = {
    0,
};            // The best path for the shortest distance
int size;     // The total number of cities
int min = -1; // Store minimum distance of traversed route
int threadLimit;
int tidx = 0;

long long checkedRoute = 0; // Number of checked route by single process

typedef struct
{
    pthread_t tid;
    long long checked_route;
} thread_info;

thread_info threadList[8];

typedef struct
{
    sem_t filled;
    sem_t empty;
    pthread_mutex_t lock;
    int **elem;
    int capacity;
    int num;
    int front;
    int rear;
} bounded_buffer;

bounded_buffer *buf = 0x0;

void bounded_buffer_init(bounded_buffer *buf, int capacity)
{
    sem_init(&(buf->filled), 0, 0);
    sem_init(&(buf->empty), 0, capacity);
    pthread_mutex_init(&(buf->lock), 0x0);
    buf->capacity = capacity;
    buf->elem = (int **)calloc(sizeof(int *), capacity);
    buf->num = 0;
    buf->front = 0;
    buf->rear = 0;
}

void bounded_buffer_queue(bounded_buffer *buf, int *msg, int idx)
{
    sem_wait(&(buf->empty));
    pthread_mutex_lock(&(buf->lock));
    (buf->elem)[buf->rear] = (int *)malloc(sizeof(msg[0]) * idx);
    memcpy((buf->elem)[buf->rear], msg, sizeof(msg[0]) * idx);
    buf->rear = (buf->rear + 1) % buf->capacity;
    buf->num += 1;
    pthread_mutex_unlock(&(buf->lock));
    sem_post(&(buf->filled));
}

int *bounded_buffer_dequeue(bounded_buffer *buf)
{
    int *r = 0x0;
    sem_wait(&(buf->filled));
    pthread_mutex_lock(&(buf->lock));

    r = buf->elem[buf->front];
    buf->front = (buf->front + 1) % buf->capacity;
    buf->num -= 1;

    pthread_mutex_unlock(&(buf->lock));
    sem_post(&(buf->empty));
    return r;
}

/* Read line number from given file to figure out the number N */
int getNcities(char *arg)
{
    FILE *fp = fopen(arg, "r");
    char temp[256];
    int line = 0;

    while (fgets(temp, 256, fp) != NULL)
    {
        line++;
    }

    fclose(fp);
    return line;
}

/* Print min distance, path and number of checked route */
void printResult()
{
    printf("\nThe shortest distance: %d\n", min);
    printf("Path: (");
    for (int i = 0; i < size; i++)
    {
        printf("%d ", minPath[i]);
    }
    printf("%d)\n", minPath[0]);
    printf("The number of checked route is %lld.\n", checkedRoute);
}

/* Behavior when SIGINT invoked */
void sigintHandler()
{
    printResult();
    exit(0);
}

/* Recursively traverse all the possible routes and calculate the length */
void _travel(int idx, int *visited, int *path, int length, int tidx)
{
    if (idx == size)
    {
        path[idx] = path[0]; // Set route from last city to starting city.

        length += cities[path[idx - 1]][path[idx]]; // Add the last city length
        checkedRoute++;                             // Number of routes that the child process traversed
        threadList[tidx].checked_route++;

        if (min == -1 || min > length)
        {                                           // Check if the length of current permuation is the best
            min = length;                           // Set the best value
            memcpy(minPath, path, sizeof(minPath)); // Save the best path
        }
        length -= cities[path[idx - 1]][path[idx]]; // Remove the current city and return to try other permutation
    }
    else
    {
        for (int i = 0; i < size; i++)
        {
            if (visited[i] == 0)
            {                                                  // Check if the route is already visited
                path[idx] = i;                                 // Record the order of visiting
                visited[i] = 1;                                // Mark as visited
                length += cities[path[idx - 1]][i];            // Add length
                _travel(idx + 1, visited, path, length, tidx); // Move to the next city
                length -= cities[path[idx - 1]][i];            // Restore length to before visiting the city
                visited[i] = 0;                                // Reset the marking
            }
        }
    }
}

/* Create subtasks, create child process and assign the tasks to child process */
void subtaskMaker(int idx, int size, int *visited, int *path)
{
    /* When prefix of the substask is created. */
    if (idx == size - MAX_SUBTASK)
    {
        bounded_buffer_queue(buf, path, idx);
    }
    else
    {
        for (int i = 0; i < size; i++)
        {
            if (visited[i] == 0)
            {                                               // Check if the route is already visited
                path[idx] = i;                              // Record the order of visiting
                visited[i] = 1;                             // Mark as visited
                subtaskMaker(idx + 1, size, visited, path); // Move to the next city
                visited[i] = 0;                             // Reset the marking
            }
        }
    }
}

void *producer_func(void *ptr)
{
    int path[51] = {
        0,
    };
    int visited[51] = {
        0,
    }; // Mark visited city
    subtaskMaker(0, size, visited, path);
    return 0x0;
}

void *consumer_func(void *ptr)
{
    int td = tidx;
    threadList[tidx++].tid = pthread_self();
    while (1)
    {
        int *prefix;
        int path[51] = {
            0,
        };
        int visited[51] = {
            0,
        }; // Mark visited city
        int length = 0;
        prefix = bounded_buffer_dequeue(buf);

        for (int i = 0; i < size - MAX_SUBTASK; i++)
        {
            visited[prefix[i]] = 1;
            path[i] = prefix[i];
        }

        _travel(size - MAX_SUBTASK, visited, path, length, td);
    }
    return 0x0;
}

int main(int argc, char *argv[])
{

    signal(SIGINT, sigintHandler);
    FILE *fp = fopen(argv[1], "r");
    threadLimit = atoi(argv[2]); // Limit number of child process

    pthread_t producer;
    pthread_t consumer[threadLimit];

    buf = malloc(sizeof(bounded_buffer));
    bounded_buffer_init(buf, threadLimit);

    /* Get number of cities */
    size = getNcities(argv[1]);

    /* Put the length value into array from given tsp file */
    for (int i = 0; i < size; i++)
    {
        for (int j = 0; j < size; j++)
        {
            fscanf(fp, "%d", &cities[i][j]);
        }
    }
    fclose(fp);

    pthread_create(&producer, 0x0, producer_func, 0x0);
    for (int i = 0; i < threadLimit; i++)
    {
        pthread_create(&(consumer[i]), 0x0, consumer_func, 0x0);
        threadList[i].checked_route = 0;
    }

    while (1)
    {
        char op[10];
        printf("input option(statm, threads, num N): ");
        scanf("%s", op);

        if (op[0] == 's')
        {
            printResult();
        }
        else if (op[0] == 't')
        {
            for (int i = 0; i < threadLimit; i++)
            {
                printf("tid : %ul \n # checked route : %lld\n", threadList[i].tid, threadList[i].checked_route);
            }
        }
    }
    pthread_join(producer, 0x0);
    for (int i = 0; i < threadLimit; i++)
    {
        int status;
        status = pthread_join(consumer[i], 0x0);
    }

    return 0;
}