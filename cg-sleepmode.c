/* See LICENSE file for copyright and license details. */
#include "cg-base.h"

#include <libclut.h>

#if defined(_GNU_SOURCE)
# undef _GNU_SOURCE
#endif
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>



/**
 * The default filter priority for the program
 */
const int64_t default_priority = (int64_t)3 << 59;

/**
 * The default class for the program
 */
char default_class[] = PKGNAME "::cg-sleepmode::standard";

/**
 * Class suffixes
 */
const char *const *class_suffixes = (const char *const[]){NULL};



/**
 * -r: fade-out time for the red channel
 */
static char *rflag;

/**
 * -g: fade-out time for the green channel
 */
static char *gflag;

/**
 * -b: fade-out time for the blue channel
 */
static char *bflag;

/**
 * The duration, in seconds, of the red channel's fade out
 */
static double red_time = 3;

/**
 * The duration, in seconds, of the green channel's fade out
 */
static double green_time = 2;

/**
 * The duration, in seconds, of the blue channel's fade out
 */
static double blue_time = 1;

/**
 * The luminosity of red channel after the fade out
 */
static double red_target = 0.5;

/**
 * The luminosity of green channel after the fade out
 */
static double green_target = 0;

/**
 * The luminosity of blue channel after the fade out
 */
static double blue_target = 0;

/**
 * Time to fade in?
 */
static volatile sig_atomic_t received_int = 0;



/**
 * Print usage information and exit
 */
void
usage(void)
{
	fprintf(stderr,
	        "usage: %s [-M method] [-S site] [-c crtc]... [-R rule] [-p priority] "
	        "[-r red-fadeout-time] [-g green-fadeout-time] [-b blue-fadeout-time] "
	        "[red-luminosity [green-luminosity [blue-luminosity]]]\n",
	        argv0);
	exit(1);
}


/**
 * Called when a signal is received
 * that tells the program to terminate
 * 
 * @param  signo  The received signal
 */
static void
sig_int(int signo)
{
	received_int = 1;
	(void) signo;
}


/**
 * Handle a command line option
 * 
 * @param   opt  The option, it is a NUL-terminate two-character
 *               string starting with either '-' or '+', if the
 *               argument is not recognised, call `usage`. This
 *               string will not be "-M", "-S", "-c", "-p", or "-R".
 * @param   arg  The argument associated with `opt`,
 *               `NULL` there is no next argument, if this
 *               parameter is `NULL` but needed, call `usage`
 * @return       0 if `arg` was not used,
 *               1 if `arg` was used,
 *               -1 on error
 */
int
handle_opt(char *opt, char *arg)
{
	if (opt[0] == '-') {
		switch (opt[1]) {
		case 'r':
			if (rflag || !(rflag = arg))
				usage();
			return 1;
		case 'g':
			if (gflag || !(gflag = arg))
				usage();
			return 1;
		case 'b':
			if (bflag || !(bflag = arg))
				usage();
			return 1;
		default:
			usage();
		}
	} else {
		usage();
	}
	return 0;
}


/**
 * Parse a non-negative double encoded as a string
 * 
 * @param   out  Output parameter for the value
 * @param   str  The string
 * @return       Zero on success, -1 if the string is invalid
 */
static int
parse_double(double *restrict out, const char *restrict str)
{
	char *end;
	errno = 0;
	*out = strtod(str, &end);
	if (errno || *out < 0 || isinf(*out) || isnan(*out) || *end)
		return -1;
	if (!*str || !strchr("0123456789.", *str))
		return -1;
	return 0;
}


/**
 * This function is called after the last
 * call to `handle_opt`
 * 
 * @param   argc  The number of unparsed arguments
 * @param   argv  `NULL` terminated list of unparsed arguments
 * @param   prio  The argument associated with the "-p" option
 * @return        Zero on success, -1 on error
 */
int
handle_args(int argc, char *argv[], char *prio)
{
	int q = (rflag || gflag || bflag || argc);
	if (q > 1 || argc > 3)
		usage();
	if (rflag && parse_double(&red_time, rflag) < 0)
		usage();
	if (gflag && parse_double(&green_time, gflag) < 0)
		usage();
	if (bflag && parse_double(&blue_time, bflag) < 0)
		usage();
	if (argc >= 1 && parse_double(&red_target, argv[0]) < 0)
		usage();
	if (argc >= 2 && parse_double(&green_target, argv[1]) < 0)
		usage();
	if (argc >= 3 && parse_double(&blue_target, argv[2]) < 0)
		usage();
	if (red_target >= 1)
		red_time = 0;
	if (green_target >= 1)
		green_time = 0;
	if (blue_target >= 1)
		blue_time = 0;
	return 0;
	(void) prio;
}


/**
 * Fill a filter
 * 
 * @param  filter  The filter to fill
 * @param  red     The red brightness
 * @param  green   The green brightness
 * @param  blue    The blue brightness
 */
static void
fill_filter(libcoopgamma_filter_t *restrict filter, double red, double green, double blue)
{
	switch (filter->depth) {
#define X(CONST, MEMBER, MAX, TYPE)\
	case CONST:\
		libclut_start_over(&filter->ramps.MEMBER, MAX, TYPE, 1, 1, 1);\
		libclut_rgb_brightness(&filter->ramps.MEMBER, MAX, TYPE, red, green, blue);\
		break;
	LIST_DEPTHS
#undef X
	default:
		abort();
	}
}


/**
 * Get the current monotonic time as a double
 * 
 * @param   now  Output parameter for the current time (monotonic)
 * @return       Zero on success, -1 on error
 */
static int
double_time(double *restrict now)
{
#ifndef CLOCK_MONOTONIC_RAW
# define CLOCK_MONOTONIC_RAW  CLOCK_MONOTONIC
#endif
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) < 0)
		return -1;
	*now  = (double)(ts.tv_nsec);
	*now /= 1000000000L;
	*now += (double)(ts.tv_sec);
	return 0;
}


/**
 * The main function for the program-specific code
 * 
 * @return  0: Success
 *          -1: Error, `errno` set
 *          -2: Error, `cg.error` set
 *          -3: Error, message already printed
 */
int
start(void)
{
	int r, fade_red, fade_green, fade_blue;
	size_t i, j;
	double t, starttime, red, green, blue, redt, greent, bluet;

	redt   = (red_target   - 1) / red_time;
	greent = (green_target - 1) / green_time;
	bluet  = (blue_target  - 1) / blue_time;
	fade_red   = !isinf(redt)   && !isnan(redt);
	fade_green = !isinf(greent) && !isnan(greent);
	fade_blue  = !isinf(bluet)  && !isnan(bluet);

	for (i = 0; i < filters_n; i++)
		crtc_updates[i].filter.lifespan = LIBCOOPGAMMA_UNTIL_DEATH;

	if ((r = make_slaves()) < 0)
		return r;

	if ((r = double_time(&starttime)) < 0)
		return r;

	red   = red_target   < 0 ? 0 : red_target   > 1 ? 1 : red_target;
	green = green_target < 0 ? 0 : green_target > 1 ? 1 : green_target;
	blue  = blue_target  < 0 ? 0 : blue_target  > 1 ? 1 : blue_target;

	for (;;) {
		if ((r = double_time(&t)) < 0)
			return r;
		t -= starttime;
		if (fade_red) {
			red = 1 + t * redt;
			if (red > 1)
				red = 1;
			else if (red < 0)
				red = 0;
		}
		if (fade_green) {
			green = 1 + t * greent;
			if (green > 1)
				green = 1;
			else if (green < 0)
				green = 0;
		}
		if (fade_blue) {
			blue = 1 + t * bluet;
			if (blue > 1)
				blue = 1;
			else if (blue < 0)
				blue = 0;
		}

		for (i = 0, r = 1; i < filters_n; i++) {
			if (!crtc_updates[i].master || !crtc_info[crtc_updates[i].crtc].supported)
				continue;
			fill_filter(&crtc_updates[i].filter, red, green, blue);
			r = update_filter(i, 0);
			if (r == -2 || (r == -1 && errno != EAGAIN))
				return r;
			if (crtc_updates[i].slaves)
				for (j = 0; crtc_updates[i].slaves[j]; j++) {
					r = update_filter(crtc_updates[i].slaves[j], 0);
					if (r == -2 || (r == -1 && errno != EAGAIN))
						return r;
				}
		}

		while (r != 1)
			if ((r = synchronise(-1)) < 0)
				return r;

		sched_yield();

		if (t >= red_time && t >= green_time && t >= blue_time)
			break;
	}

	if (signal(SIGINT,  sig_int) == SIG_ERR ||
	    signal(SIGTERM, sig_int) == SIG_ERR ||
	    signal(SIGHUP,  sig_int) == SIG_ERR)
		return -1;

	if (libcoopgamma_set_nonblocking(&cg, 0) < 0)
		return -1;
	for (;;) {
		if (libcoopgamma_synchronise(&cg, NULL, 0, &j) < 0) {
			if (received_int)
				goto fade_in;
			switch (errno) {
			case 0:
				break;
			case ENOTRECOVERABLE:
				goto enotrecoverable;
			default:
				return 1;
			}
		}
	}

fade_in:
	if (libcoopgamma_set_nonblocking(&cg, 1) < 0)
		return -1;

	t = red_time;
	t = t > green_time ? t : green_time;
	t = t > blue_time  ? t : blue_time;
	redt   = t - red_time;
	greent = t - green_time;
	bluet  = t - blue_time;
	t = red_time + green_time + blue_time;
	if (red_time > 0)
		t = t < red_time   ? t : red_time;
	if (green_time > 0)
		t = t < green_time ? t : green_time;
	if (blue_time > 0)
		t = t < blue_time  ? t : blue_time;
	red_time   = t + redt;
	green_time = t + greent;
	blue_time  = t + bluet;

	red = green = blue = 1;

	if ((r = double_time(&starttime)) < 0)
		return r;

	for (;;) {
		if ((r = double_time(&t)) < 0)
			return r;
		t -= starttime;
		redt   = t / red_time;
		greent = t / green_time;
		bluet  = t / blue_time;
		if (!isinf(redt) && !isnan(redt)) {
			red = red_target * (1 - redt) + redt;
			if (red > 1)
				red = 1;
			else if (red < 0)
				red = 0;
		}
		if (!isinf(greent) && !isnan(greent)) {
			green = green_target * (1 - greent) + greent;
			if (green > 1)
				green = 1;
			else if (green < 0)
				green = 0;
		}
		if (!isinf(bluet) && !isnan(bluet)) {
			blue = blue_target * (1 - bluet) + bluet;
			if (blue > 1)
				blue = 1;
			else if (blue < 0)
				blue = 0;
		}

		for (i = 0, r = 1; i < filters_n; i++) {
			if (!crtc_updates[i].master || !crtc_info[crtc_updates[i].crtc].supported)
				continue;
			fill_filter(&crtc_updates[i].filter, red, green, blue);
			r = update_filter(i, 0);
			if (r == -2 || (r == -1 && errno != EAGAIN))
				return r;
			if (crtc_updates[i].slaves) {
				for (j = 0; crtc_updates[i].slaves[j]; j++) {
					r = update_filter(crtc_updates[i].slaves[j], 0);
					if (r == -2 || (r == -1 && errno != EAGAIN))
						return r;
				}
			}
		}

		while (r != 1 && (r = synchronise(-1)) < 0)
			return r;

		sched_yield();

		if (t >= red_time && t >= green_time && t >= blue_time)
			break;
	}

	return 0;
enotrecoverable:
	pause();
	return -1;
}
