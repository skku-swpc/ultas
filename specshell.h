#define SPECSHELL_HOME "/home/sebuns/work/specshell"
#define SPEC_DIR "specset"
#define SPEC_LOG "speclog"

#define PROMPT "specshell"

#define NUM_OF_BENCH 5
#define STR_LENGTH 255

#define HMMER_PATH "specset/hmmer/"
#define MCF_PATH "specset/mcf"
#define BZIP_PATH "specset/bzip2/"
#define GOBMK_PATH "specset/gobmk/"
#define GCC_PATH "specset/gcc"

#define DEFAULT_CORETEMP	"/proc/tpas/core_temp"
#define DEFAULT_CPUINFORM	"/proc/tpas/cpu_inform"
#define DEFAULT_TEMP_THRESH	"/proc/tpas/temp_thresh"

#define DEFAULT_TEMP_LOG	"templog/"

#define LOG_DIR "log"
#define TEMP_LOG_DIR "templog"

#define INIT_LABEL "Init :: "
#define PARSING_LABEL "Parsing :: "
#define DO_LABEL "\tDo :: "
#define RUN_LABEL "\tRun :: "
#define BENCH_LABEL "\t\tBench :: "
#define BALANCER_LABEL "\tBalancer :: "


#define IS_NUMBER(c) (c >= '0' && c <= '9')
#define CHAR_TO_NUM(c) (c - '0')

#define TIME_STAMP(t) {\
	timestamp = time(NULL);\
	t = localtime(&timestamp);\
}

#define PAGE_SIZE 4096

#define PID_QUEUE_KEY 3538
#define PID_QUEUE_SIZE PAGE_SIZE

#include <time.h>
#include <pthread.h>

/*
 * Command list
 * list : print command list
 * bench : print bench list
 * run : run bench
 * temp : tmperature logging on/off
 * help :
 */

/*
 * spec run example::
 * specshell::1)run hmmer --op num --op num ...
 * temp example::
 * specshell::2)temp on
 * specshell::3)temp off
 */

enum {LIST=1, BENCH, RUN, TEMP, HELP, LAST};
enum {ON=0, OFF};
enum {HMMER=0, GOBMK, MCF, BZIP2, GCC};
enum {MESSAGE_ERR=0, MESSAGE_INS, MESSAGE_DEL, MESSAGE_FIN, MESSAGE_TAIL};
enum {NAIVE=0};

struct hmmer_config
{
	unsigned int fixed;
	unsigned int mean;
	unsigned int num;
	unsigned int sd;
	unsigned int seed;
	char *input;		//input data
};

struct gobmk_config
{
	char *input;
};

struct mcf_config
{

};

struct bzip_config
{

};

struct gcc_config
{

};

struct bench_struct
{
	int		key;		//same as this bench's index
	int		bench_type; //bench type index
	char 	name[10];	//bench name
	int		cpu;		//where is this bench running on?

	union
	{
		struct hmmer_config *hmmer_conf;
		struct gobmk_config *gobmk_conf;
		struct mcf_config *mcf_conf;
		struct bzip_config *bzip_conf;
		struct gcc_config *gcc_conf;
	};
};

/*
 * Package that composed of homogeneous benchmarks
 */

struct bench_package
{
	int	bench_num;
	int	iter;
	struct bench_struct *benches;
	const struct bench_operations *bop;
	char *input;
};

struct bench_operations
{
	int (*run_hmmer)(struct bench_package *);
	int (*run_gobmk)(struct bench_package *);
	int (*run_mcf)(struct bench_package *);
	int (*run_bzip)(struct bench_package *);
	int (*run_gcc)(struct bench_package *);
};

struct cmd_struct
{
	int cmd_type;	//list, bench, run, temp

	union
	{
		char *command;
		struct bench_struct *bench;
	};
};

int logfd;		//command log
FILE *outfp;		//

pthread_t balancer;
int balancer_id;

int monitoring_on;

static time_t timestamp;		//global time stamp

struct balancing_switch
{
 	int on;
 	int balancing_on;
};

int temp_on;

void shell_fault();

void logging(char *, char *);

int do_run(struct bench_package *benches);

int run_hmmer (struct bench_package *);
int run_gobmk (struct bench_package *);
int run_mcf (struct bench_package *);
int run_bzip (struct bench_package *);
int run_gcc (struct bench_package *);
