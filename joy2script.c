/*
   joy2script 
   
   This program gets events from a joystick device and
   runs an associated command
		
		- Brian Hrebec (brianh32@gmail.com)
		

		joy2script is based on joy2key by Peter Amstutz

		The latest version of joy2script can be found at 
			http://www.brianhrebec.com/joy2script

		Revision History
		----------------
        2.0 (3 June 2008) - Added repeat timers, reworked the config file format
        1.0 (3 June 2008) - First version.

*/

#include "config.h"

#define JOY2SCRIPT_VERSION                "2.0"

#define MAX_ACTION_STRING              1024 
#define DEFAULT_AUTOREPEAT             5
#define DEFAULT_DEADZONE               100
#define DEFAULT_DEADZONE_SIZE               50
#define DEFAULT_DEVICE                 "/dev/input/js0"
#define DEFAULT_CONFIG_FILE            ".joy2scriptrc" /* located in $(HOME) */
#define EMAIL                          "brianh32@gmail.com"
#define MAX_MODES		       16

#define DEBUG 0

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <syslog.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/timerfd.h>
#include <sys/time.h>
#include <sys/select.h>
#include <linux/joystick.h>

int jsfd=-1;
char numaxes, numbuttons;
int current_mode=0;
int daemonize = 1;

struct s_axis {
    char *action_on;
    char *action_off;
    int deadzone;
    int deadzone_size;
    int asymmetric;
    int repeat_rate_high;
    int repeat_rate_low;
    int repeat;
    int output_low;
    int output_high;
    int time_to_repeat;
    char on;
    int value;
    int timer_fd;
    struct itimerspec old_its;
};

struct s_button {
    char *action_on;
    char *action_off;
    int repeat_rate; 
    int time_to_repeat;
    char on;
    int timer_fd;
};

struct s_mode {
    struct s_axis axis[256];
    struct s_button button[256];
} mode[MAX_MODES];

char *device=DEFAULT_DEVICE, 
    *config_file=DEFAULT_CONFIG_FILE;

typedef enum {NONE, X, RAWCONSOLE, TERMINAL} target_type;
typedef enum {PRESS, RELEASE} press_or_release_type;

target_type target=NONE;

void process_args(int argc, char **argv);
void parse_config();
int check_device(int argc, char **argv);
void sendkey( unsigned int keycode, press_or_release_type PoR, int iscap);
void cleanup(int s);
void calibrate(int num);
void send_axis_action(struct s_axis *axis, char *action);
void repeat_event(fd_set *js_fdset);
void axis_event(int number, int value);
void button_event(int number, int value);
int scale_value(int value, int max, int lower, int upper);

int check_config(int argc, char **argv);
void make_daemon();

int main(int argc, char **argv)
{
    struct js_event js;
    fd_set js_fdset;
    
    puts("joy2script - reads joystick status and take action accordingly ");
    puts("By Brian Hrebec ("EMAIL")");
    puts("This is free software under the GNU General Public License (GPL v2)");
    puts("              (see COPYING in the joy2script archive)");
    printf("Version: %s   Binary built on %s at %s\n\n", 
		   JOY2SCRIPT_VERSION, __DATE__, __TIME__);

    memset(mode, 0, sizeof(mode));
    
    argc=check_config(argc, argv);
    process_args(argc, argv);

    if((jsfd=open(device,O_RDONLY))==-1)
    {
		printf("Error opening %s!\n", device);
		puts("Are you sure you have joystick support in your kernel?");
		return 1;
    }
    if (ioctl(jsfd, JSIOCGAXES, &numaxes)) {
/* acording to the American Heritage Dictionary of the English 
   Language 'axes' *IS* the correct pluralization of 'axis' */
		perror("joy2key: error getting axes"); 
		return 1;
    }
    if (ioctl(jsfd, JSIOCGBUTTONS, &numbuttons)) {
		perror("joy2key: error getting buttons");
		return 1;
    }

    FD_ZERO(&js_fdset);

    memset(&js, 0, sizeof(struct js_event));

    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    if (daemonize) 
    {
        puts("Initialization complete, daemonizing...!\n");
        make_daemon();
    }
    else
    {
        puts("Initialization complete, entering main loop, ^C to exit...");
    }

    /* Main Loop */
    for(;;)
    {
        int i;

		memset(&js, 0, sizeof(struct js_event));

        /* Add timer fds to set for select() */
        int nfds = 0;
        for (i = 0; i < numaxes; i++)
            if (mode[current_mode].axis[i].timer_fd != -1)
                FD_SET(mode[current_mode].axis[i].timer_fd, &js_fdset);
        nfds += i;

        for (i = 0; i < numbuttons; i++)
            if (mode[current_mode].button[i].timer_fd != -1)
                FD_SET(mode[current_mode].button[i].timer_fd, &js_fdset);
        nfds += i;

        FD_SET(jsfd, &js_fdset); /* joystick fd */
        nfds++;

        select (nfds, &js_fdset, NULL, NULL, NULL);

        if (FD_ISSET(jsfd, &js_fdset)) 
        {
            read(jsfd, &js, sizeof(struct js_event));
            switch(js.type)
            {
            case JS_EVENT_BUTTON:
                button_event(js.number, js.value);
                break;
            case JS_EVENT_AXIS:
                axis_event(js.number, js.value);
                break;
            }
        } 
        else 
        {
            repeat_event(&js_fdset);
        }
    }
}

void make_daemon() {
    int pid, sid;

    /* Fork off the parent process */
    pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }
    /* If we got a good PID, then
       we can exit the parent process. */
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    /* Change the file mode mask */
    umask(0);

    /* Create a new SID for the child process */
    sid = setsid();
    if (sid < 0) {
        /* Log the failure */
        exit(EXIT_FAILURE);
    }

    /* Change the current working directory */
    if ((chdir("/")) < 0) {
        /* Log the failure */
        exit(EXIT_FAILURE);
    }

    /* Close out the standard file descriptors */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

void repeat_event(fd_set *js_fdset) 
{
    int i;
    for (i = 0; i < numaxes; i++) 
    {
        struct s_axis* axis;
        axis = &mode[current_mode].axis[i];
        if (axis->timer_fd != -1 && 
                FD_ISSET(axis->timer_fd, js_fdset)) 
        {
            unsigned long long m;
            read(axis->timer_fd, &m, sizeof(m));
            send_axis_action(axis, axis->action_on);
        }
    }

    for (i = 0; i < numbuttons; i++) 
    {
        struct s_button* button;
        button = &mode[current_mode].button[i];
        if (button->timer_fd != -1 && 
                FD_ISSET(button->timer_fd, js_fdset)) 
        {
            unsigned long long m;
            read(button->timer_fd, &m, sizeof(m));
            system(button->action_on);
        }
    }
}

void button_event(int number, int value) 
{
    struct s_button* button;
    button = &mode[current_mode].button[number];

    if (value) 
    {
        button->on = 1;

        if (button->repeat_rate > 0) 
        {
            struct itimerspec its;
            its.it_interval.tv_sec = button->repeat_rate / 1000;
            its.it_interval.tv_nsec = button->repeat_rate % 1000 * 1000000;
            its.it_value.tv_sec = its.it_interval.tv_sec;
            its.it_value.tv_nsec = its.it_interval.tv_nsec;
            int tfd = timerfd_create(CLOCK_MONOTONIC, 0);
            timerfd_settime(tfd, 0, &its, NULL);
            button->timer_fd = tfd;
        }

        system(button->action_on);
    } 
    else 
    {
        button->on = 0;

        if (button->timer_fd != -1) 
        {
            close(button->timer_fd);
            button->timer_fd = -1;
        }

        system(button->action_off);
    }
}

int scale_value(int value, int max, int lower, int upper)
{
    double v = (double)value / max;
    int diff = upper - lower;

    return (int) lower + (v * diff);
}


int timespec_subtract (struct timespec* result, 
        struct timespec* x, struct timespec* y)
{
    if (x->tv_nsec < y->tv_nsec) 
    {
        result->tv_sec = x->tv_sec - y->tv_sec - 1;
        result->tv_nsec = 1000000000 + x->tv_nsec - y->tv_nsec;
    }
    else
    {
        result->tv_sec = x->tv_sec - y->tv_sec;
        result->tv_nsec = x->tv_nsec - y->tv_nsec;
    }

    /* Return 1 if result is negative. */
    return result->tv_sec < 0 || result->tv_nsec < 0;
}


void axis_event(int number, int value)
{
    struct s_axis* axis;
    axis = &mode[current_mode].axis[number];

    if (axis->asymmetric)
        axis->value = value + 32767;
    else
        axis->value = value;
    

    if ((abs(axis->value) < axis->deadzone - axis->deadzone_size) 
            && axis->on) 
    {
        /*turn it off*/
        send_axis_action(axis, axis->action_off);
        axis->on=0;

        if (axis->timer_fd != -1) 
        {
            close(axis->timer_fd);
            axis->timer_fd = -1;
        }
    }
    else if ((abs(axis->value) > 
                axis->deadzone + axis->deadzone_size) ) 
    {
        if (!axis->on || 
            (axis->repeat && axis->repeat_rate_low == 0 &&
                    axis->repeat_rate_high == 0)) 
        {
            send_axis_action(axis, axis->action_on);
        }

        if (axis->repeat && (axis->repeat_rate_low != 0 || 
                  axis->repeat_rate_high != 0)) {

            int ms;
            if (axis->asymmetric)
                ms = scale_value(axis->value, 65536, 
                        axis->repeat_rate_low, axis->repeat_rate_high);
            else
                ms = scale_value(axis->value, 32768, 
                        axis->repeat_rate_low, axis->repeat_rate_high);

            int tfd;
            struct itimerspec its;
            struct itimerspec current_its;
            its.it_interval.tv_sec = ms / 1000;
            its.it_interval.tv_nsec = ms % 1000 * 1000000;
            its.it_value.tv_sec = its.it_interval.tv_sec;
            its.it_value.tv_nsec = its.it_interval.tv_nsec;

            if (axis->timer_fd == -1) 
            {
                tfd = timerfd_create(CLOCK_MONOTONIC, 0);
                memset(&current_its, 0, sizeof(current_its));
            } 
            else 
            {
                tfd = axis->timer_fd;
                timerfd_gettime(tfd, &current_its);
            }

            /* If the amount of time  is less than
             * the amount the new timer wants, set the difference
             * as the new time. */
            struct timespec elapsed_time;
            timespec_subtract(&elapsed_time, &axis->old_its.it_value, 
                    &current_its.it_value);
            axis->old_its = its;

            if (elapsed_time.tv_sec < its.it_value.tv_sec ||
                    elapsed_time.tv_nsec < its.it_value.tv_nsec)
            {
                timespec_subtract(&its.it_value,
                       &its.it_value, &elapsed_time);

            }


            timerfd_settime(tfd, 0, &its, NULL);
            axis->timer_fd = tfd;
        }

        axis->on=1;
    }
}

void send_axis_action(struct s_axis *axis, char* action)
{
	char buffer[MAX_ACTION_STRING];
    char *p_buffer = buffer;
	int len=0;
	char val[64];

	if (!action) 
        return;

    while (*action != '\0')
    {
        if (len > MAX_ACTION_STRING - 1) {
            printf("Error: action string too long");
            return;
        }

        if (*action == '%')   
        {
            action++;
            char spec = *action++;
            if (spec == 'v') /*value*/				
            {
                int cvalue;
                if (axis->asymmetric)
                    cvalue = scale_value(axis->value, 65536, 
                            axis->output_low, axis->output_high);
                else
                    cvalue = scale_value(axis->value, 32768, 
                            axis->output_low, axis->output_high);

                sprintf(val, "%d", cvalue);
                char *v = val;
                while (*v) {
                    *p_buffer++ = *v++;
                    len++;
                }

                continue;
            }
            else if (spec == 's') /*sign */
            {
                sprintf(val, "%s", axis->value < 0 ? "-1" : "+1");
                char *v = val;
                while (*v) {
                    *p_buffer++ = *v++;
                    len++;
                }
                continue;
            } else {
                *p_buffer++ = '%';
                *p_buffer++ = spec;
                len += 2;
            }
        } 
        else 
        {
            *p_buffer++ = *action++;
        }
    }
    *p_buffer = '\0';

#if DEBUG
    printf("Axis action: %s\n", buffer);
#endif
    system(buffer);
}

int check_config(int argc, char **argv)
{
    int i, x;
    
    for(i=1; i<argc; i++)
    {
	if(!strcmp("-config", argv[i]))
	{
		if(i+2>argc) 
		{
			puts("Not enough arguments to -config");
			exit(1);
		}
		config_file=argv[++i];
		argc-=2;
		for(x=i; x<argc; x++) argv[x]=argv[x+2];
	}
    }
    parse_config();
    return argc;
}

void parse_config()
{
    FILE *file;
    char line[1024];
    int current_mode=-1;
    int current_item=-1;/*axis/button #*/
    int parsing_axis=-1;
    int x;
	if(!strcmp(config_file, DEFAULT_CONFIG_FILE))
	{
		x=strlen(getenv("HOME")) + strlen(config_file) + 2;
		config_file=(char*)malloc(x);
		sprintf(config_file, "%s/%s", getenv("HOME"), DEFAULT_CONFIG_FILE);
	}
	if((file=fopen(config_file, "r"))==NULL)
	{
		printf("Cannot open config file \"%s\"\n", config_file);
		exit(1);
	}
	while(!feof(file))
	{
        fscanf(file, " %[^ \t=] ", line);
		current_mode=0;
        
		if(!strcmp(line, "[mode"))
		{
			fscanf(file, " %d ] ", &current_mode);
			if (current_mode > MAX_MODES-1) {
				printf("error: Too many modes defined! Only %d allowed.", MAX_MODES);
				exit(1);
			}
#if DEBUG
            printf("Found mode: %d\n", current_mode);
#endif
		}
		else if(!strcmp(line, "[axis")) 
		{
			if (current_mode == -1)
			{
				printf("Error parsing axis: no mode given");
				exit(1);
			}
			fscanf(file, " %d ] ", &current_item);
            mode[current_mode].axis[current_item].output_low = 0;
            mode[current_mode].axis[current_item].output_high = 32768;
            mode[current_mode].axis[current_item].deadzone = DEFAULT_DEADZONE;
            mode[current_mode].axis[current_item].deadzone_size = 
                DEFAULT_DEADZONE_SIZE;
            mode[current_mode].axis[current_item].timer_fd = -1;
			parsing_axis=1;
#if DEBUG
            printf("Found axis: %d\n", current_item);
#endif
		} 
		else if(!strcmp(line, "[button")) 
		{
			if (current_mode == -1)
			{
				printf("Error parsing button: no mode given");
				exit(1);
			}
			fscanf(file, " %d ] ", &current_item);
			parsing_axis=0;
#if DEBUG
            printf("Found button: %d\n", current_item);
#endif
		} 
		else if (!strcmp(line, "action_on"))
		{
			if (current_item == -1)
			{
				printf("Error parsing action: no axis or button given");
				exit(1);
			}
			fscanf(file, " = ");
            fgets(line, 1024, file);
			if (parsing_axis)
				mode[current_mode].axis[current_item].action_on=strdup(line);
			else
				mode[current_mode].button[current_item].action_on=strdup(line);
#if DEBUG
            printf("Found action_on: %s\n", line);
#endif
		}
		else if (!strcmp(line, "action_off"))
		{
			if (current_item == -1)
			{
				printf("Error parsing action_off: no axis or button given");
				exit(1);
			}
			fscanf(file, " = ");
            fgets(line, 1024, file);
			if (parsing_axis)
				mode[current_mode].axis[current_item].action_off=strdup(line);
			else
				mode[current_mode].button[current_item].action_off=strdup(line);
#if DEBUG
            printf("Found action_off: %s\n", line);
#endif
		}
		else if (!strcmp(line, "repeat_rate"))
		{
			if (current_item == -1)
			{
				printf("Error parsing repeat_rate: no axis or button given");
				exit(1);
			}
			fscanf(file, " = %d ", &x);
			if (parsing_axis) {
				mode[current_mode].axis[current_item].repeat_rate_low=x;
				mode[current_mode].axis[current_item].repeat_rate_high=x;
            } else {
				mode[current_mode].button[current_item].repeat_rate=x;
            }
		}
		else if (!strcmp(line, "repeat_rate_high"))
		{
			if (current_item == -1)
			{
				printf("Error parsing repeat_rate_high: no axisgiven");
				exit(1);
			}
			if (parsing_axis==0)
				printf("repeat_rate_high has no meaning for a button");
			fscanf(file, " = %d ", &x);
			if (parsing_axis)
				mode[current_mode].axis[current_item].repeat_rate_high=x;
		}
		else if (!strcmp(line, "repeat_rate_low"))
		{
			if (current_item == -1)
			{
				printf("Error parsing repeat_rate_low: no axisgiven");
				exit(1);
			}
			if (parsing_axis==0)
				printf("repeat_rate_low has no meaning for a button");
			fscanf(file, " = %d ", &x);
			if (parsing_axis)
				mode[current_mode].axis[current_item].repeat_rate_low=x;
		}
		else if (!strcmp(line, "asymmetric"))
		{
			if (current_item == -1)
			{
				printf("Error parsing asymmetric: no axis or button given");
				exit(1);
			}
			if (parsing_axis==0)
				printf("asymmetric has no meaning for a button");
			fscanf(file, " = %d ", &x);
			if (parsing_axis)
				mode[current_mode].axis[current_item].asymmetric=x;
		}
		else if (!strcmp(line, "deadzone"))
		{
			if (current_item == -1)
			{
				printf("Error parsing deadzone: no axis or button given");
				exit(1);
			}
			if (parsing_axis==0)
				printf("deadzone has no meaning for a button");
			fscanf(file, " = %d ", &x);
			if (parsing_axis)
				mode[current_mode].axis[current_item].deadzone=x;
#if DEBUG
            printf("Found deadzone: %d\n", x);
#endif
		}
		else if (!strcmp(line, "deadzone_size"))
		{
			if (current_item == -1)
			{
				printf("Error parsing deadzone_size: no axis or button given");
				exit(1);
			}
			if (parsing_axis==0)
				printf("deadzone_size has no meaning for a button");
			fscanf(file, " = %d ", &x);
			if (parsing_axis)
				mode[current_mode].axis[current_item].deadzone_size=x/2;
		}
		else if (!strcmp(line, "output_high"))
		{
			if (current_item == -1)
			{
				printf("Error parsing output_high: no axis or button given");
				exit(1);
			}
			if (parsing_axis==0)
				printf("output_high has no meaning for a button");
			fscanf(file, " = %d ", &x);
			if (parsing_axis)
				mode[current_mode].axis[current_item].output_high=x;
		}
		else if (!strcmp(line, "output_low"))
		{
			if (current_item == -1)
			{
				printf("Error parsing output_low: no axis or button given");
				exit(1);
			}
			if (parsing_axis==0)
				printf("output_low has no meaning for a button");
			fscanf(file, " = %d ", &x);
			if (parsing_axis)
				mode[current_mode].axis[current_item].output_low=x;
		}
		else if (!strcmp(line, "repeat"))
		{
			if (current_item == -1)
			{
				printf("Error parsing output_low: no axis or button given");
				exit(1);
			}
			if (parsing_axis==0)
				printf("repeat has no meaning for a button");
			fscanf(file, " = %d ", &x);
			if (parsing_axis)
				mode[current_mode].axis[current_item].repeat=x;
        } 
        else if (!strcmp(line, "#"))
        {
            fgets(line, 1024, file);
            /* Comment */
		} 
        else if (strlen(line)) {
            printf("Unrecognized option: %s\n", line);
        }
        *line = '\0';
    }	    
	fclose(file);
}


void process_args(int argc, char **argv)
{
    int i;

    if(!argv[1]) return;
    for(i=(argc == 1 || argv[1][0] == '-') ? 1 : 2; i<argc; i++)
    {
		if(!strcmp(argv[i], "-dev"))
		{
			if(i+2>argc) 
			{
				puts("Not enough arguments to -dev");
				exit(1);
			}
			device=strdup(argv[++i]);
			continue;
		} else if (!strcmp(argv[i], "--no-daemon")) {
            daemonize = 0;
        }

		printf("Unknown option %s\n", argv[i]);
		puts("Usage: joy2script [\"Window Name\"]");
		printf("\n       [ -dev {%s} ]", DEFAULT_DEVICE);
		printf("\n       [ -config {%s} ]", DEFAULT_CONFIG_FILE);
		printf("\n       [ --no-daemon ]");

		puts("\n\nnote: [] denotes `optional' option or argument,");
		puts("      () hints at the wanted arguments for options");
		puts("      {} denotes default (compiled-in) parameters");
		exit(1);
    }

	
}

void cleanup(int s)
{
    printf("\n%s caught, cleaning up & quitting.\n", 
		   s==SIGINT ? "SIGINT" : 
		   (s==SIGTERM ? "SIGTERM" : ((s == 0) ? "Window die" : "Unknown")));
/* Because the window has just closed, it will print out an error upon
   calling these functions.  To suppress this superflous error, don't call 
   them :) */
/*    XFlush(thedisp); */
/*    XCloseDisplay(thedisp); */
#ifdef ENABLE_CONSOLE
    if(target==RAWCONSOLE || target==TERMINAL) close(consolefd);
#endif
    exit(0);
}

