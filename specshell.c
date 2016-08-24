#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>

#include "specshell.h"
#include "cpu_struct.h"

char *bench_list[NUM_OF_BENCH] = {"hmmer", "gobmk", "mcf", "bzip2", "gcc"};

char *command_list[LAST-1] = {"list", "bench", "run", "temp", "help"};

char *hmmer_input[2] = {"retro", "nph3"};

char *gobmk_input[5] = {"13x13", "nngs", "score2", "grevorc", "trevord"};

char *run_help[5] = {"hmmer::\n\t-n|--num) a number of cpus\n\t-c|--cpu) cpu numbers separated by space that you want to run the benchmark\n\t-i|--input) hmmer input <nph3|retro>\n\tex ::  specshell)run hmmer -c 1 3 5 7 -i nph3\n","gobmk::\n", "mcf::\n", "bzip2::\n", "gcc::\n"};

static int command_count;		//global command count
static int balancer_on;

#define BALANCER_ON() (balancer_on = ON);
#define BALANCER_OFF() (balancer_on = OFF);
#define TEMP_ON()	(temp_on = ON);
#define TEMP_OFF()	(temp_on = OFF);

static struct bench_operations *bops;

/*
 * pid queue declaration
 */

int pid_queue_id;

void logging(char *str, char *prefix)
{
	struct tm	*t;
	char log[100];

	timestamp = time(NULL);
	t = localtime(&timestamp);

	sprintf(log, "[%d:%d:%d]", t->tm_hour, t->tm_min, t->tm_sec);
	write(logfd, log, strlen(log));

	if (prefix != NULL)
		write(logfd, prefix, strlen(prefix));

	write(logfd, str, strlen(str));
	write(logfd, "\n", 1);

	fdatasync(logfd);
}

void construct_cpu_conf(void)
{
	char thresh[10];
	int i;

	logging("Constructing cpu datas", INIT_LABEL);

	cpu_conf.cpu_num = NUM_OF_CPU;
	cpu_conf.core_num = NUM_OF_CPU * NUM_OF_CORE;
	cpu_conf.temp_thresh = DEFAULT_THRESH;

	cpus[0].cpu_id = 0;
	cpus[0].start_idx = 0;
	cpus[0].end_idx = 3;
	cpus[0].temp_avg = 0;

	cpus[1].cpu_id = 1;
	cpus[1].start_idx = 4;
	cpus[1].end_idx = 7;
	cpus[1].temp_avg = 0;

	for (i = 0;i < NUM_OF_CORE;i++)
	{
		cores[i].cpu_id = 0;
		cores[i].core_id = i * 2 + 1;
		cores[i].temp = 0;
		cores[i].running = 0;

		inverted_index[i * 2 + 1] = i;
	}

	for (i = 0;i < NUM_OF_CORE;i++)
	{
		cores[i+4].cpu_id = 1;
		cores[i+4].core_id = i * 2;
		cores[i+4].temp = 0;
		cores[i+4].running = 0;

		inverted_index[i * 2] = i + 4;
	}

	sprintf(thresh, "%d", DEFAULT_THRESH);

	for (i = 0;i < 8;i++)
	{
		migration_checker[i] = 0;
	}

	logging("Temp thresh sets to DEFAULT_THRESH", INIT_LABEL);

	monitoring_on = OFF;


	printf("\n");

	if(temp_thresh_fd > -1)
		write(temp_thresh_fd, thresh, strlen(thresh));
}

char *separate_message(char *messages)
{
	static char *container;
	char *message;

	if (messages != NULL)
		container = messages;

	if (*container == '\0')
		return NULL;

	message = container;

	while (*container != ':' && *container != '\0')
		container++;

	if (*container == ':')
	{
		*container = '\0';
		container++;
	}

	return message;
}

int separate_temp(char *temp_line)
{
	static char *temps;
	int temp = 0;

	if (temp_line != NULL)
		temps = temp_line;

	if (*temps == '\0')
		return -1;

	while (*temps != ' ' && *temps != '\0')
	{
		temp = temp * 10 + (*temps - '0');
		temps++;
	}

	if (*temps == ' ')
		temps++;

	return temp;
}

int translate_pid (char *str)
{
	int pid = 0;

	while (*str != '@')
	{
		pid = (pid * 10) + CHAR_TO_NUM(*str);
		str++;
	}

	return pid;
}

int translate_cpu (char *str)
{
	int cpu = 0;

	while (*str != '@')
		str++;

	str++;

	while (*str != '-')
	{
		cpu = (cpu * 10) + CHAR_TO_NUM(*str);
		str++;
	}

	return cpu;
}

int translate_bench_type (char *str)
{
	int type = -1;

	while (*str != '-')
		str++;

	str++;

	if (*str !='\0')
		type = CHAR_TO_NUM(*str);

	return type;
}

struct task_inform *decompose_message(char *message)
{
	struct task_inform *task;

	if (message != NULL)
	{
		task = (struct task_inform *)malloc(sizeof(struct task_inform));

		task->pid = translate_pid(message);
		task->on_cpu = translate_cpu(message);
		task->bench_type = translate_bench_type(message);
		task->queue.next = NULL;
	}

	return task;
}

struct task_inform *find_sibiling(int pid, struct task_inform *task)
{
	struct task_inform *prev;

	prev = task;

	while (prev != NULL)
	{
		printf("%d %d\n", pid, prev->pid);
		if (prev->pid == pid)
			return prev;

		prev = prev->sibiling.next;
	}

	return NULL;
}

int insert_sibiling(struct task_inform *task)
{
	struct task_inform *sib;

	if (sib_list == NULL)
	{
		sib_list = task;
		task->sibiling.prev = task;	//head sibiling's prev sibiling is itself
		task->sibiling.next = NULL;

		return 0;
	}

	sib = sib_list;

	while (sib->sibiling.next != NULL)
		sib = sib->sibiling.next;

	sib->sibiling.next = task;
	task->sibiling.prev = sib;
	task->sibiling.next = NULL;

	return 0;
}

int remove_sibiling(struct task_inform *del_task)
{
	struct task_inform *sib;

	if (del_task == sib_list)
	{
		if (sib_list->sibiling.next != NULL)
		{
			sib_list = sib_list->sibiling.next;
			sib_list->sibiling.prev = sib_list;
		}

		return 0;
	}

	sib = del_task->sibiling.prev;

	if (sib != NULL)
	{
		sib->sibiling.next = del_task->sibiling.next;
		del_task->sibiling.next->sibiling.prev = sib;

		return 0;
	}

	return -1;
}

int insert_task(struct task_inform *ins_task)
{
	struct task_inform *prev;

	if (task_queue[ins_task->on_cpu]->queue.next == NULL)
	{
		task_queue[ins_task->on_cpu]->queue.next = ins_task;
		ins_task->queue.prev = task_queue[ins_task->on_cpu];
	}
	else
	{
		prev = task_queue[ins_task->on_cpu];

		while (prev->queue.next != NULL)
			prev = prev->queue.next;

		ins_task->queue.next = prev->queue.next;
		ins_task->queue.prev = prev;
		prev->queue.next = ins_task;
	}

	cores[inverted_index[ins_task->on_cpu]].running++;

	return ins_task->on_cpu;
}

int remove_task(struct task_inform *del_task)
{
	int del_cpu;
	struct task_inform *prev;

	if (del_task == NULL)
		return -1;

	remove_sibiling(del_task);

	del_cpu = del_task->on_cpu;
	printf("%d\n", del_task->on_cpu);

	if (del_task->queue.prev != NULL)
		del_task->queue.prev->queue.next = del_task->queue.next;
	else
	{
		prev = task_queue[del_cpu];

		/* finding predcessor */
		while (prev->queue.next != NULL && (prev->queue.next->pid != del_task->pid))
			prev = prev->queue.next;

		/* if del task is immediately child of task queue*/

		prev->queue.next = del_task->queue.next;
	}

	free(del_task);

	cores[inverted_index[del_cpu]].running--;

	return del_cpu;
}

/*
 * src_core_idx, target
 * both are core's idx
 */

int get_migration_target(int cpu, int src_core_idx, int *target)
{
	int i;
	int start;
	int bal;

	bal = 0;

	/*
	 * 1st pass : find idle core
	 *   -> 1) in same processor
	 *   -> 2) in different processor
	 */

	*target = -1;

	if (cpu == 0)
		start = 0;
	else
		start = 4;

	for (i = start;i < start + NUM_OF_CORE;i++)
	{
		if (src_core_idx == i)
			continue;

		if (cores[i].running == 0)
		{
			if (cores[i].temp + 5000 < cores[src_core_idx].temp)
			{
				*target = i;
				bal = 1;

				break;
			}
		}
	}

	if (bal == 1)
		return 1;

	if (cpu == 0)
		start = 4;
	else
		start = 0;

	for (i = start;i < start + NUM_OF_CORE;i++)
	{
		if (src_core_idx == i)
			continue;

		if (cores[i].running == 0)
		{
			if (cores[i].temp + 5000 < cores[src_core_idx].temp)
			{
				*target = i;
				bal = 1;

				break;
			}
		}
	}

	if (bal == 1)
		return 0;

	return -1;
}

/*
 * task queue management
 */

int migration(struct task_inform *task, int from_cpu, int to_cpu)
{
	struct task_inform *prev, *newhome;

	if (task == NULL)
		return -1;

	prev = task_queue[from_cpu];
	newhome = task_queue[to_cpu];

	if (prev == NULL || newhome == NULL)
		return -1;

	while (prev->queue.next != NULL && prev->queue.next->pid != task->pid)
		prev = prev->queue.next;

	if (1)
	{
		prev->queue.next = NULL;
		task->queue.next = NULL;
		newhome->queue.next = task;
	}
	else
	{
		prev->queue.next = task->queue.next;
		task->queue.next = newhome->queue.next;
		newhome->queue.next = task;
	}

	/* migration must be called 1 second after*/

	migration_checker[inverted_index[from_cpu]] = 10;
	migration_checker[inverted_index[to_cpu]] = 10;

	return to_cpu;
}

void *balancer_func(void *c)
{
	int shid;
	void *shared = (void *)0;
	char rdtemp[100];
	char pid_message[100];
	char log[100];
	int print_slice = 0;
	int min_core[NUM_OF_CPU];
	char *message;
	int migration_count = 0;
	int thermal_reading_count = 0;

	int i;

	sib_list = NULL;

	size_t cpu_size;
	cpu_set_t *cpu_setp;

	cpu_setp = CPU_ALLOC(8);
	cpu_size = CPU_ALLOC_SIZE(8);

	CPU_ZERO_S(cpu_size, cpu_setp);

	shid = shmget((key_t)PID_QUEUE_KEY, PID_QUEUE_SIZE, 0666|IPC_CREAT);
	shared = shmat(shid, (void *)0, 0);

	sprintf((char *)shared, "%d\0", MESSAGE_FIN);

	init_task_queue();

	/*Task aware phase*/

	while (!balancer_on)
	{
		/* Task Awaring */

		sprintf(pid_message, "%s", (char *)shared);

		if (CHAR_TO_NUM(pid_message[0]) == MESSAGE_INS)
		{
			struct task_inform *task;

			sprintf((char *)shared, "%d", MESSAGE_ERR);

			/*message format : <header> <pid@cpu-bench_type>:<pid@cpu-bench_type>...:<tail>*/

			message = separate_message(&pid_message[2]);

			task = decompose_message(message);

			/* task insert into appropriate task queue */

			insert_task(task);

			/* task insert into sibiling queue */

			insert_sibiling(task);

			sprintf(log, "Process(%d) is inserted in task queue(%d) nr : %d!", task->pid, task->on_cpu, cores[inverted_index[task->on_cpu]].running);
			fprintf(stdout, "%s\n", log);
			logging(log, BALANCER_LABEL);

			while ((message = separate_message(NULL)) != NULL)
			{
				task = decompose_message(message);

				insert_task(task);

				insert_sibiling(task);

				sprintf(log, "Process(%d) is inserted in task queue(%d) nr : %d!", task->pid, task->on_cpu, cores[inverted_index[task->on_cpu]].running);
				fprintf(stdout, "%s\n", log);
				logging(log, BALANCER_LABEL);
			}
		}
		else if (CHAR_TO_NUM(pid_message[0]) == MESSAGE_DEL)
		{
			/*
			 * From bench launcher, balancer takes messages for management task queue
			 * in this case, remove terminated task from task queue
			 */

			int del_pid, del_cpu;
			struct task_inform *del_task, *prev;

			sprintf((char *)shared, "%d", MESSAGE_ERR);

			message = separate_message(&pid_message[2]);

			del_pid = translate_pid(message);
			del_task = find_sibiling(del_pid, sib_list);

			if ((del_cpu = remove_task(del_task)) != -1)
			{
				sprintf(log, "Process(%d) is removed from task queue(%d) nr : %d!", del_pid, del_cpu, cores[inverted_index[del_cpu]].running);
				fprintf(stdout, "%s\n", log);
				logging(log, BALANCER_LABEL);
			}
			else
			{
				sprintf(log, "Process(%d) isn't exist in task queue(%d) nr : %d!", del_pid, del_cpu, cores[inverted_index[del_cpu]].running);
				fprintf(stdout, "%s\n", log);
				logging(log, BALANCER_LABEL);
			}

			while ((message = separate_message(NULL)) != NULL)
			{
				del_pid = translate_pid(message);
				del_task = find_sibiling(del_pid, sib_list);

				printf("del : %d\n", del_task->pid);

				if ((del_cpu = remove_task(del_task)) != -1)
				{
					sprintf(log, "Process(%d) is removed from task queue(%d) nr : %d!", del_pid, inverted_index[del_cpu], cores[inverted_index[del_cpu]].running);
					fprintf(stdout, "%s\n", log);
					logging(log, BALANCER_LABEL);
				}
				else
				{
					sprintf(log, "Process(%d) isn't exist in task queue(%d)!", del_pid, del_cpu);
					fprintf(stdout, "%s\n", log);
					logging(log, BALANCER_LABEL);
				}
			}

			sprintf((char *)shared, "%d\0", MESSAGE_FIN);
			migration_count = 0;
		}

		/* Thermal Monitoring */

		core_temp_fd = open(DEFAULT_CORETEMP, O_RDONLY);

		if(read(core_temp_fd, rdtemp, 48))
		{
			int cpu_temp;
			int	i = 0;
			char wrtemp[100];

			rdtemp[47] = '\0';

			sprintf(wrtemp, "%d %s\n", thermal_reading_count, rdtemp);

			if(!temp_on && (print_slice % 10 == 0))
				write(1, wrtemp, strlen(wrtemp));

			write(temp_logger_fd, wrtemp, strlen(wrtemp));

			cpu_temp = separate_temp(rdtemp);
			cores[inverted_index[i]].temp = cpu_temp;
			i++;

			while ((cpu_temp = separate_temp(NULL)) != -1)
			{
				cores[inverted_index[i]].temp = cpu_temp;
				i++;
			}

			print_slice++;
		}

		close(core_temp_fd);
		thermal_reading_count++;

		/*
		 * Thermal Balancing
		 * NAIVE :: Naive algorithm
		 *			If hot spot occurred, the task on this core is migrated to founded idle core
		 *			If idle core is not exist, no migrating occurred
		 * HEUR :: Heuristic algorithm
		 */

		int i, j;
		int bal;
		int target;
		struct task_inform *task;

		for (i = 0;i < 8;i++)
		{
			if (migration_checker[i] > 0)
				migration_checker[i]--;
		}


		for (i = 0;i < 8;i++)
		{
			if ((migration_checker[i] == 0) && (cores[i].running > 0) && (cores[i].temp > cpu_conf.temp_thresh))
			{
				bal = get_migration_target(cores[i].cpu_id, i, &target);

				if (bal != -1 && target != -1)
				{
					/*Do migration */
					migration_count++;
					task = task_queue[cores[i].core_id]->queue.next;

					if (bal == 1)
						sprintf(log, "Task(%d) is migrated %d(nr:%d) -> %d(nr:%d) (Intra CPU)", task->pid, cores[i].core_id, cores[i].running, cores[target].core_id, cores[target].running);
					else
						sprintf(log, "Task(%d) is migrated %d(nr:%d) -> %d(nr:%d) (Inter CPU)", task->pid, cores[i].core_id, cores[i].running, cores[target].core_id, cores[target].running);

					logging(log, BALANCER_LABEL);

					if (migration(task, cores[i].core_id, cores[target].core_id) == -1)
					{
						printf("Null task\n");

						continue;
					}

					CPU_SET(cores[target].core_id, cpu_setp);
					sched_setaffinity(task->pid, cpu_size, cpu_setp);
					CPU_CLR(cores[target].core_id, cpu_setp);

					cores[target].running++;
					cores[i].running--;
					task->on_cpu = cores[target].core_id;

					fprintf(stdout, "%s\n", log);

					if (monitoring_on == ON)
					{
						for (j = 0;j < 8;j++)
							printf("Cores[%d] : Queue[%d] : Nr : %d\n", j, cores[j].core_id, cores[j].running);
					}
				}
			}
		}

		usleep(100000);

		/* Thermal Balancing End*/
	}

	shmdt((void *)shared);

	pthread_exit(NULL);
}

void shell_init(void)
{
	char log_file_name[100];
	char log_str[STR_LENGTH];
	struct tm	*t;

	char *c;

	int err;

	/* shared memory initialization */

	void *shared = (void *)0;

	/* time stamp initialization*/

	timestamp = time(NULL);
	t = localtime(&timestamp);

	/*
	 * Change current directory to log directory
	 */

	err = chdir(LOG_DIR);

	if (err < 0)
	{
		mkdir(LOG_DIR, 0774);
		chdir(LOG_DIR);
	}

	sprintf(log_file_name, "%d%d%dspecshell.log\0", (t->tm_year + 1900), (t->tm_mon + 1), t->tm_mday);

	logfd = open(log_file_name, O_APPEND | O_CREAT | O_RDWR);

	logging("Specshell is started", INIT_LABEL);

	/*
	 * Open core temp and cpu inform from /proc file system
	 */

	chdir(SPECSHELL_HOME);
	err = chdir(TEMP_LOG_DIR);

	if (err < 0)
	{
		mkdir(TEMP_LOG_DIR, 0774);
		chdir(TEMP_LOG_DIR);
	}

	sprintf(log_file_name, "%d%d%d_%d%dtemp.log", (t->tm_year + 1900), (t->tm_mon + 1), t->tm_mday, t->tm_hour, t->tm_min);

	temp_thresh_fd = open(DEFAULT_TEMP_THRESH, O_RDWR);
	temp_logger_fd = open(log_file_name, O_WRONLY | O_CREAT);

	if (core_temp_fd < 0 || cpu_inform_fd < 0 || temp_thresh_fd < 0)
	{
		logging("Openning core temp and cpu inform from /proc fs is failed!", INIT_LABEL);

		shell_fault();
	}

	construct_cpu_conf();

	logging("Openning core temp and cpu inform from /proc fs is complete!", INIT_LABEL);

	/*
	 * By default, thermal monitoring is activated
	 */

	BALANCER_ON();
	TEMP_OFF();

	/*
	 * balancer create
	 */

	pid_queue_id = shmget((key_t)PID_QUEUE_KEY, PID_QUEUE_SIZE, 0666|IPC_CREAT);

	if (pid_queue_id == -1)
	{
		logging("shmget failed", INIT_LABEL);

		exit(0);
	}

	printf("shell init...\n");

	balancer_id = pthread_create(&balancer, NULL, balancer_func, (void *)c);

	/*
	 * Constructing bench operation table
	 */

	bops = (struct bench_operations *)malloc(sizeof(struct bench_operations));

	if (bops != NULL)
	{
		bops->run_hmmer = run_hmmer;
		bops->run_gobmk = run_gobmk;
		bops->run_mcf = run_mcf;
		bops->run_bzip = run_bzip;
		bops->run_gcc = run_gcc;
	}
	else
	{
		logging("Bench operations initialization failed!", INIT_LABEL);

		shell_fault();
	}

	logging("Bench operations initialization is complete!", INIT_LABEL);
	command_count = 0;
	printf("complete...\n");
}

void shell_exit()
{
	logging("specshell is terminated", INIT_LABEL);

	close(logfd);

	BALANCER_OFF();

	fchmod(temp_logger_fd, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

	close(temp_logger_fd);
	close(core_temp_fd);
	close(cpu_inform_fd);
}

void shell_fault(void)
{
	logging("spechell is aborted", INIT_LABEL);

	close(logfd);
	BALANCER_OFF();
	TEMP_OFF();

	close(temp_logger_fd);
	close(core_temp_fd);
	close(cpu_inform_fd);
}

char *tokenizer(char *str)
{
	static char *container;
	char *tok;

	if (str != NULL)
		container = str;

	if (*container == '\0')
		return NULL;

	tok = container;

	while (*container != ' ' && *container != '\n' && *container != '\0')
		container++;

	if (container != '\0')
	{
		*container = '\0';
		container++;
	}

	return tok;
}

/*
 * Interprets options in input, then makes bench_struct & it's configuration
 */

int parse_run (char *input, struct cmd_struct *cmd)
{
	char *options;
	struct bench_package *pack;
	struct bench_struct *bench = NULL;
	void *bench_conf = NULL;
	int cpu = 0;
	int i;

	logging(input, RUN_LABEL);

	options = tokenizer(input);

	if (!strcmp(options, "help"))
	{
		for (i = 0;i < GCC + 1;i++)
			fprintf(stdout, "%s\n", run_help[i]);
	}
	else if (!strcmp(options, "hmmer"))
	{
		cpu = 1;
		pack = (struct bench_package*)malloc(sizeof(struct bench_package));
		pack->iter = 1;

		bench_conf = (struct hmmer_config *)malloc(sizeof(struct hmmer_config));

		((struct hmmer_config *)bench_conf)->fixed = 0;
		((struct hmmer_config *)bench_conf)->mean = 500;
		((struct hmmer_config *)bench_conf)->num = 500000;
		((struct hmmer_config *)bench_conf)->sd = 350;
		((struct hmmer_config *)bench_conf)->seed = 0;
		((struct hmmer_config *)bench_conf)->input = hmmer_input[1];

		logging("Configuring hmmer arguments", RUN_LABEL);

		while ((options = tokenizer(NULL)) != NULL)
		{
			if (!strcmp(options, "-n") || !strcmp(options, "--num"))
			{
				options	= tokenizer(NULL);

				cpu = CHAR_TO_NUM(*options);
				pack->bench_num = cpu;
			}

			if (!strcmp(options, "-c") || !strcmp(options, "--cpu"))
			{
				bench = (struct bench_struct *)malloc(sizeof(struct bench_struct) * cpu);

				for (i = 0;i < cpu;i++)
				{
					options = tokenizer(NULL);

					if (*options == '\0' || !IS_NUMBER(*options))
						break;

					bench[i].key = i;
					bench[i].bench_type = HMMER;
					strcpy(bench[i].name, bench_list[HMMER]);
					bench[i].cpu = CHAR_TO_NUM(*options);

					bench[i].hmmer_conf = (struct hmmer_config *)bench_conf;


				}

				/*If number of cpu (-n option) and the number of given cpu numbers are different*/

				if (i != cpu)
				{
					char *err = "the number of benches is mismatched!";

					logging(err, RUN_LABEL);

					fprintf(stdout, "%s\n", err);

					free(bench);
					shell_fault();

					exit(-1);
				}
			}

			/*
			 * Set the benchmark iteration number
			 */

			if (!strcmp(options, "-l") || !strcmp(options, "--loop"))
			{
				int iter_num = 0;

				options = tokenizer(NULL);

				while(*options != '\0' && IS_NUMBER(*options))
				{
					iter_num = iter_num * 10 + CHAR_TO_NUM(*options);

					options++;
				}

				pack->iter = iter_num;
			}

			if (!strcmp(options, "-i") || !strcmp(options, "--input"))
			{
				options = tokenizer(NULL);

				if (*options != '\0')
				{
					if(!strcmp(options, hmmer_input[0]))
					{
						pack->input = hmmer_input[0];
						((struct hmmer_config *)bench_conf)->input = hmmer_input[0];
					}
					else
					{
						pack->input = hmmer_input[1];
						((struct hmmer_config *)bench_conf)->input = hmmer_input[1];
					}
				}
				else
					pack->input = hmmer_input[1];
			}
		}

		if (bench == NULL)
		{
			bench = (struct bench_struct *)malloc(sizeof(struct bench_struct));

			bench->key = 0;
			bench->bench_type = HMMER;
			strcpy(bench->name, bench_list[HMMER]);
			bench->cpu = 0;
			bench->hmmer_conf = (struct hmmer_config *)bench_conf;

			pack->bench_num = 1;
		}

		pack->bench_num = cpu;
		pack->benches = bench;
		pack->bop = bops;

		if (pack->input == NULL)
			pack->input = hmmer_input[1];		//default input

	}
	else if(!strcmp(options, "gobmk"))
	{

	}
	else if(!strcmp(options, "mcf"))
	{

	}
	else if(!strcmp(options, "bzip2"))
	{

	}
	else if(!strcmp(options, "gcc"))
	{

	}

	do_run(pack);

	logging("do_run complete", RUN_LABEL);

	return cmd->cmd_type;
}

void print_list()
{
	int i;

	fprintf(stdout, "Command list ========================\n");

	for (i = 0;i < LAST - 1;i++)
		fprintf(stdout, "%s\n", command_list[i]);
}

void print_benches()
{
	int i;

	fprintf(stdout, "Bench list ========================\n");

	for (i = 0;i < NUM_OF_BENCH;i++)
		fprintf(stdout, "%s\n", bench_list[i]);
}

void do_temp(struct cmd_struct *cmd)
{
	char log[100];
	logging("temperature monitoring", DO_LABEL);

	if (temp_on == OFF)
	{
		if (!strcmp(cmd->command, "on"))
		{
			TEMP_ON();

			/*do thermal reading*/
			fprintf(stdout, "\nThermal monitoring on\n");

			return ;
		}
		else if (!strcmp(cmd->command, "thresh"))
		{
			fprintf(stdout, "Current temperature threshold is %d\n", cpu_conf.temp_thresh);
			fprintf(stdout, "New thresh : ");
			scanf("%d", &temp_thresh);

			cpu_conf.temp_thresh = temp_thresh;
			sprintf(log, "Temperature threshold is set by %d!", temp_thresh);
			fprintf(stdout, "%s\n", log);
			logging(log, DO_LABEL);
		}
		else if (strcmp(cmd->command, "off"))
			fprintf(stdout, "\n%s -- wrong option\nusage) temp on/off/thresh\n", cmd->command);

		return ;
	}
	else
	{
		if (!strcmp(cmd->command, "off"))
		{
			TEMP_OFF();

			/**/

			fprintf(stdout, "\nThermal monitoring off\n");

			return ;
		}
		else if (!strcmp(cmd->command, "thresh"))
		{
			fprintf(stdout, "Current temperature threshold is %d\n", cpu_conf.temp_thresh);
			fprintf(stdout, "New thresh : ");
			scanf("%d", &temp_thresh);

			cpu_conf.temp_thresh = temp_thresh;
			sprintf(log, "Temperature threshold is set by %d!", temp_thresh);
			fprintf(stdout, "%s\n", log);
			logging(log, DO_LABEL);
		}
		else if (strcmp(cmd->command, "on"))
			fprintf(stdout, "\n%s -- wrong option\nusage) temp on/off/thresh", cmd->command);

		return ;
	}
}

int do_command (struct cmd_struct *cmd)
{
	switch (cmd->cmd_type)
	{
		case LIST:
			logging("list command", DO_LABEL);
			print_list();

			break;

		case BENCH:
			logging("bench command", DO_LABEL);
			print_benches();

			break;

		case TEMP:
			do_temp(cmd);

			break;

		case HELP:

			break;

		case LAST:
			logging("Out of command", DO_LABEL);
			fprintf(stdout, "Out of command :: %s\n", cmd->command);
			break;
	}

	return cmd->cmd_type;
}

int parse_command (char *input)
{
	char tok[10];
	int i = 0;

	struct cmd_struct cmd;

	logging(input, PARSING_LABEL);

	if (!strcmp(input, "exit"))
		return -1;

	while (*input != ' ' && *input != '\0')
	{
		tok[i] = *input;
		input++;
		i++;
	}

	tok[i] = '\0';
	input++;
	i = 0;

	while (i < LAST - 1)
	{
		if (!strcmp(tok, command_list[i]))
			break;
		i++;
	}

	/*
	 * Bench running
	 */

	if (i == RUN - 1)
	{
		cmd.cmd_type = RUN;

		return parse_run(input, &cmd);
	}

	/*
	 * Thermal monitoring on/off
	 */

	if (i == TEMP - 1)
	{
		if (*input == '\0')
			cmd.command = NULL;

		cmd.command = input;
	}
	else
	{
		cmd.command = tok;
	}

	cmd.cmd_type = i + 1;

	return do_command(&cmd);
}

int get_line (char *buf)
{
	int n = 0;

	while ((*(buf++) = getchar()) != '\n')
		n++;

	if(*(buf-1) == '\n')
		*(buf-1) = '\0';

	if (n < STR_LENGTH)
		return n;
	else
		return -1;
}

int main(int argc, char *argv[])
{
	int i;
	int exit = 1;

	char input[STR_LENGTH];

	shell_init();

	while (exit != -1)
	{
		fprintf(stdout, "%s::%4d)", PROMPT, command_count++);

		if (get_line(input) == -1)
		{
			fprintf(stdout, "Input string length must be smaller than 255\n");

			continue;
		}

		exit = parse_command(input);
	}

	shell_exit();

	return 0;
}

