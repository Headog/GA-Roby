#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <threads.h>

#define WIDTH 10
#define HEIGHT 10
#define CANS 50
#define RULES 200
#define EPOCHS 1000
#define STEPS 200
#define EXECS 100
#define MIXES 2
#define WEIGHT M_E

#define THREADS 10
static_assert(!(EXECS % THREADS), "EXECS is not divisible by THREADS");

#define PICK_UP_CAN_REWARD 10
#define PICK_UP_AIR_PUNISH -1
#define KICK_WALL_PUNISH -5

#define PROGRESS_BAR_UNITS 10
static_assert(!(EPOCHS % PROGRESS_BAR_UNITS),
              "EPOCHS is not divisible by PROGRESS_BAR_UNITS");
#define WEIGHTED(X, MAX)                                                  \
    ((int)(pow(WEIGHT, -((double)(X) / MAX * (log(MAX) / log(WEIGHT)))) * \
           (MAX)) -                                                       \
     1)
#define _SITUATIONS 243
#define _ACTIONS 7
#define _DIRECTIONS 4

enum action
{
    move_north,
    move_south,
    move_east,
    move_west,
    stand_still,
    pick_up,
    move_random,
};

struct result
{
    enum action rule[_SITUATIONS];
    int score;
};

struct history
{
    int strategy[_SITUATIONS][_ACTIONS][PROGRESS_BAR_UNITS];
    enum action best_rule[_SITUATIONS];
    int best_score;
};

void create_random_world(bool world[HEIGHT][WIDTH])
{
    bool *target;
    int i;
    for (i = 0; i < HEIGHT; i++)
    {
        memset(world[i], false, sizeof(bool) * WIDTH);
    }
    for (i = 0; i < CANS; i++)
    {
        do
        {
            target = world[random() % HEIGHT] + random() % WIDTH;
        } while (*target);
        *target = true;
    }
}

void create_random_rule(enum action rule[_SITUATIONS])
{
    for (int i = 0; i < _SITUATIONS; i++)
    {
        rule[i] = random() % _ACTIONS;
    }
}

int get_situation(const int x, const int y, const bool world[HEIGHT][WIDTH])
{
    int situation = 0;
    if (y != 0)
    {
        situation += world[y - 1][x] ? 1 : 2;
    }
    situation *= 3;
    if (y != HEIGHT - 1)
    {
        situation += world[y + 1][x] ? 1 : 2;
    }
    situation *= 3;
    if (x != WIDTH - 1)
    {
        situation += world[y][x + 1] ? 1 : 2;
    }
    situation *= 3;
    if (x != 0)
    {
        situation += world[y][x - 1] ? 1 : 2;
    }
    situation *= 3;
    situation += world[y][x] ? 1 : 2;
    return situation;
}

int act(int *x, int *y, bool world[HEIGHT][WIDTH], const enum action action)
{
    switch (action)
    {
    case move_north:
        if (*y == 0)
        {
            return KICK_WALL_PUNISH;
        }
        else
        {
            *y -= 1;
            return 0;
        }
    case move_south:
        if (*y == HEIGHT - 1)
        {
            return KICK_WALL_PUNISH;
        }
        else
        {
            *y += 1;
            return 0;
        }
    case move_east:
        if (*x == WIDTH - 1)
        {
            return KICK_WALL_PUNISH;
        }
        else
        {
            *x += 1;
            return 0;
        }
    case move_west:
        if (*x == 0)
        {
            return KICK_WALL_PUNISH;
        }
        else
        {
            *x -= 1;
            return 0;
        }
    case stand_still:
        return 0;
    case pick_up:
        if (world[*y][*x])
        {
            world[*y][*x] = false;
            return PICK_UP_CAN_REWARD;
        }
        else
        {
            return PICK_UP_AIR_PUNISH;
        }
    case move_random:
        return act(x, y, world, random() % _DIRECTIONS);
    }
    return 0;
}

void mix_rules(const enum action rule1[_SITUATIONS],
               const enum action rule2[_SITUATIONS],
               enum action result1[_SITUATIONS],
               enum action result2[_SITUATIONS])
{
    int i;
    int split = random() % (_SITUATIONS - 1);
    memcpy(result1, rule1, split * sizeof(enum action));
    memcpy(result2, rule2, split * sizeof(enum action));
    memcpy(result1 + split, rule2 + split,
           (_SITUATIONS - split) * sizeof(enum action));
    memcpy(result2 + split, rule1 + split,
           (_SITUATIONS - split) * sizeof(enum action));
    for (i = 0; i < MIXES; i++)
    {
        result1[random() % _SITUATIONS] = random() % _ACTIONS;
        result2[random() % _SITUATIONS] = random() % _ACTIONS;
    }
}

void get_rule_str(const enum action rule[_SITUATIONS],
                  char result[_SITUATIONS + 1])
{
    for (int i = 0; i < _SITUATIONS; i++)
    {
        result[i] = '0' + rule[i];
    }
    result[_SITUATIONS] = '\0';
}

int compare_results(const void *result1, const void *result2)
{
    return ((struct result *)result2)->score - ((struct result *)result1)->score;
}

void train(struct history *history, FILE *log_file)
{
    int i, j, k, l;
    int x, y, score, progress;
    enum action rules[RULES][_SITUATIONS];
    bool world[HEIGHT][WIDTH];
    bool temp_world[HEIGHT][WIDTH];
    struct result results[RULES];

    progress = 0;
    for (i = 0; i < RULES; i++)
    {
        create_random_rule(rules[i]);
    }

    printf("  Progress: ");
    for (i = 0; i < PROGRESS_BAR_UNITS; i++)
    {
        putchar('_');
    }
    for (i = 0; i < PROGRESS_BAR_UNITS; i++)
    {
        putchar('\b');
    }
    fflush(stdout);
    for (i = 0; i < EPOCHS; i++)
    {
        for (k = 0; k < RULES; k++)
        {
            results[k].score = 0;
        }

        for (j = 0; j < EXECS; j++)
        {
            create_random_world(world);

            for (k = 0; k < RULES; k++)
            {
                memcpy(temp_world, world, sizeof(bool) * HEIGHT * WIDTH);
                x = 0;
                y = 0;
                score = 0;

                for (l = 0; l < STEPS; l++)
                {
                    score += act(&x, &y, temp_world,
                                 rules[k][get_situation(x, y, temp_world)]);
                }
                results[k].score += score;
            }
        }

        for (j = 0; j < RULES; j++)
        {
            memcpy(results[j].rule, rules[j], sizeof(enum action) * _SITUATIONS);
        }
        qsort(results, RULES, sizeof(struct result), compare_results);

        if (results[0].score > history->best_score)
        {
            memcpy(history->best_rule, results[0].rule,
                   sizeof(enum action) * _SITUATIONS);
            history->best_score = results[0].score;
        }
        fprintf(log_file, "%d, ", results[0].score / EXECS);
        for (j = 0; j < _SITUATIONS; j++)
        {
            fprintf(log_file, "%d,", results[0].rule[j]);
            history->strategy[j][results[0].rule[j]][progress]++;
        }
        fputc('\n', log_file);
        if ((i + 1) % (EPOCHS / PROGRESS_BAR_UNITS) == 0)
        {
            putchar('*');
            fflush(stdout);
            progress++;
        }
        if (i == EPOCHS - 1)
        {
            putchar('\n');
            break;
        }

        for (j = 0; j < RULES - 1; j += 2)
        {
            mix_rules(results[WEIGHTED(random() % RULES, RULES)].rule,
                      results[WEIGHTED(random() % RULES, RULES)].rule, rules[j],
                      rules[j + 1]);
        }
    }
}

void release_history(struct history *history, FILE *log_file)
{
    const static char *block_state[] = {"WALL", "CAN", "AIR"};
    int north, south, east, west, center;
    int i, _i, j;

    for (i = 0; i < _SITUATIONS; i++)
    {
        _i = i;
        center = _i % 3;
        _i -= center;
        _i /= 3;
        west = _i % 3;
        _i -= west;
        _i /= 3;
        east = _i % 3;
        _i -= east;
        _i /= 3;
        south = _i % 3;
        _i -= south;
        _i /= 3;
        north = _i % 3;
        fprintf(log_file, "\t%s\t\tmove_north: ", block_state[north]);
        for (j = 0; j < PROGRESS_BAR_UNITS; j++)
        {
            fprintf(log_file, "%d", history->strategy[i][move_north][j]);
            if (j != PROGRESS_BAR_UNITS - 1)
            {
                fputs(" -> ", log_file);
            }
        }
        fputs(", move_south: ", log_file);
        for (j = 0; j < PROGRESS_BAR_UNITS; j++)
        {
            fprintf(log_file, "%d", history->strategy[i][move_south][j]);
            if (j != PROGRESS_BAR_UNITS - 1)
            {
                fputs(" -> ", log_file);
            }
        }
        fprintf(log_file, "\n%s\t%s\t%s\tmove_east: ", block_state[west],
                block_state[center], block_state[east]);
        for (j = 0; j < PROGRESS_BAR_UNITS; j++)
        {
            fprintf(log_file, "%d", history->strategy[i][move_east][j]);
            if (j != PROGRESS_BAR_UNITS - 1)
            {
                fputs(" -> ", log_file);
            }
        }
        fputs(", move_west: ", log_file);
        for (j = 0; j < PROGRESS_BAR_UNITS; j++)
        {
            fprintf(log_file, "%d", history->strategy[i][move_west][j]);
            if (j != PROGRESS_BAR_UNITS - 1)
            {
                fputs(" -> ", log_file);
            }
        }
        fprintf(log_file, "\n\t%s\t\tpick_up: ", block_state[south]);
        for (j = 0; j < PROGRESS_BAR_UNITS; j++)
        {
            fprintf(log_file, "%d", history->strategy[i][pick_up][j]);
            if (j != PROGRESS_BAR_UNITS - 1)
            {
                fputs(" -> ", log_file);
            }
        }
        fputs(", move_random: ", log_file);
        for (j = 0; j < PROGRESS_BAR_UNITS; j++)
        {
            fprintf(log_file, "%d", history->strategy[i][move_random][j]);
            if (j != PROGRESS_BAR_UNITS - 1)
            {
                fputs(" -> ", log_file);
            }
        }
        fputs("\n\n\n", log_file);
    }
}

void release_best_steps(struct history *history, FILE *log_file)
{
    int i, j, k;
    int x, y;
    bool world[HEIGHT][WIDTH];
    create_random_world(world);

    fprintf(log_file, "The best rule's result (average score = %d/%d):\n\n",
            history->best_score / EXECS, CANS * PICK_UP_CAN_REWARD);

    x = 0;
    y = 0;
    for (i = 0; i <= STEPS; i++)
    {
        fprintf(log_file, "Step %d:\n", i);
        for (j = 0; j < (WIDTH + 1) * 2; j++)
        {
            fputc('-', log_file);
        }
        fputc('\n', log_file);
        for (j = 0; j < HEIGHT; j++)
        {
            fputc('|', log_file);
            for (k = 0; k < WIDTH; k++)
            {
                if (j == y && k == x)
                {
                    if (world[j][k])
                    {
                        fputs("@ ", log_file);
                    }
                    else
                    {
                        fputs("O ", log_file);
                    }
                }
                else
                {
                    if (world[j][k])
                    {
                        fputs(". ", log_file);
                    }
                    else
                    {
                        fputs("  ", log_file);
                    }
                }
            }
            fputs("|\n", log_file);
        }
        for (j = 0; j < (WIDTH + 1) * 2; j++)
        {
            fputc('-', log_file);
        }
        fputs("\n\n\n", log_file);

        if (i == STEPS)
        {
            break;
        }

        act(&x, &y, world, history->best_rule[get_situation(x, y, world)]);
    }

    fputs("Done.", log_file);
}

int main(void)
{
    struct history *history;
    FILE *train_log, *history_log, *best_steps_log;

    puts("Initializing...");
    train_log = fopen("train_log.csv", "w");
    history_log = fopen("strategy.txt", "w");
    best_steps_log = fopen("best_steps.txt", "w");
    if (train_log == NULL || history_log == NULL || best_steps_log == NULL)
    {
        perror("Unable to open log file");
        exit(EXIT_FAILURE);
    }
    history = calloc(1, sizeof(struct history));
    if (history == NULL)
    {
        perror("Unable to alloc memory for history");
        exit(EXIT_FAILURE);
    }
    srandom((unsigned int)time(NULL));

    puts("Training...");
    train(history, train_log);

    puts("Releasing...");
    release_history(history, history_log);
    release_best_steps(history, best_steps_log);

    fclose(train_log);
    fclose(history_log);
    fclose(best_steps_log);
    free(history);
    puts("Done.");
    return 0;
}
