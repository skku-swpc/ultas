/*This can be changed by server's environment*/

#define NUM_OF_CPU 2
#define NUM_OF_CORE 4
#define DEFAULT_THRESH 55000


struct core_data
{
	int		cpu_id;
	int		core_id;
	unsigned int temp;	//temperature
	int		running;	//task is running on this cpu?
};

struct cpu_data
{
	int		cpu_id;
	int		start_idx;
	int		end_idx;
	unsigned int temp_avg;
};

struct cpu_struct
{
	int cpu_num;
	int core_num;
	unsigned int temp_thresh;
};

struct task_list
{
	struct task_inform *prev;
	struct task_inform *next;
};

struct task_inform
{
	pid_t 	pid;
	int	 	on_cpu;
	int 	bench_type;
	struct task_list 	queue;		//must be terminated by NULL
	struct task_list 	sibiling;
};

struct task_inform *task_queue[NUM_OF_CPU * NUM_OF_CORE];
int	inverted_index[NUM_OF_CPU * NUM_OF_CORE];
int migration_checker[NUM_OF_CPU * NUM_OF_CORE];

struct cpu_struct cpu_conf;
struct cpu_data cpus[NUM_OF_CPU];
struct core_data cores[NUM_OF_CORE * NUM_OF_CPU];

struct task_inform *sib_list;

int core_temp_fd;
int cpu_inform_fd;
int temp_thresh_fd;

int temp_logger_fd;

int temp_thresh;

/*Task queue is managed by singular linked list*/

static void init_task_queue()
{
	int i;

	for (i = 0;i < NUM_OF_CPU * NUM_OF_CORE;i++)
	{
		task_queue[i] = (struct task_inform *)malloc(sizeof(struct task_inform));

		task_queue[i]->on_cpu = i;
		task_queue[i]->pid = 0;		//head task
		task_queue[i]->queue.next = NULL;
	}
}
