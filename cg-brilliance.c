/* See LICENSE file for copyright and license details. */
#include "cg-base.h"

#include <libclut.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>



/**
 * The default filter priority for the program
 */
const int64_t default_priority = (int64_t)1 << 61;

/**
 * The default class for the program
 */
char default_class[] = PKGNAME "::cg-brilliance::standard";

/**
 * Class suffixes
 */
const char *const *class_suffixes = (const char *const[]){NULL};



/**
 * -d: keep process alive and remove filter on death
 */
static int dflag = 0;

/**
 * -x: remove filter rather than adding it
 */
static int xflag = 0;

/**
 * The brilliance of the red channel
 */
static double rvalue = 0;

/**
 * The brilliance of the green channel
 */
static double gvalue = 0;

/**
 * The brilliance of the blue channel
 */
static double bvalue = 0;



/**
 * Print usage information and exit
 */
void
usage(void)
{
	fprintf(stderr,
	        "usage: %s [-M method] [-S site] [-c crtc]... [-R rule] "
	        "(-x | [-p priority] [-d] (all | red green blue))\n",
	        argv0);
	exit(1);
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
		case 'd':
			if (dflag || xflag)
				usage();
			dflag = 1;
			break;
		case 'x':
			if (xflag || dflag)
				usage();
			xflag = 1;
			break;
		default:
			usage();
		}
	} else {
		usage();
	}
	return 0;
	(void) arg;
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
	char *red = NULL;
	char *green = NULL;
	char *blue = NULL;
	int q = xflag + dflag;
	if (q > 1 || (xflag && (prio || argc)))
		usage();
	if (argc == 1) {
		red = green = blue = argv[0];
	} else if (argc == 3) {
		red   = argv[0];
		green = argv[1];
		blue  = argv[2];
	} else if (argc || !xflag) {
		usage();
	}
	if (argc) {
		if (parse_double(&rvalue, red) < 0)
			usage();
		if (parse_double(&gvalue, blue) < 0)
			usage();
		if (parse_double(&bvalue, green) < 0)
			usage();
	}
	return 0;
}


/**
 * Fill a filter
 * 
 * @param  filter  The filter to fill
 */
static void
fill_filter(libcoopgamma_filter_t *restrict filter)
{
	size_t i;
	switch (filter->depth) {
#define X(CONST, MAX, TYPE, MEMBER)\
	case CONST:\
		for (i = 0; i < filter->ramps.MEMBER.red_size; i++) {\
			double val = (double)(filter->ramps.MEMBER.red[i]);\
			val *= rvalue;\
			filter->ramps.MEMBER.red[i] = val < 0 ? 0 : val > (double)(MAX) ? MAX : (TYPE)val;\
		}\
		for (i = 0; i < filter->ramps.MEMBER.green_size; i++) {\
			double val = (double)(filter->ramps.MEMBER.green[i]);\
			val *= gvalue;\
			filter->ramps.MEMBER.green[i] = val < 0 ? 0 : val > (double)(MAX) ? MAX : (TYPE)val;\
		}\
		for (i = 0; i < filter->ramps.MEMBER.blue_size; i++) {\
			double val = (double)(filter->ramps.MEMBER.blue[i]);\
			val *= bvalue;\
			filter->ramps.MEMBER.blue[i] = val < 0 ? 0 : val > (double)(MAX) ? MAX : (TYPE)val;\
		}\
		break
	X(LIBCOOPGAMMA_UINT8,  UINT8_MAX,  uint8_t,  u8);
	X(LIBCOOPGAMMA_UINT16, UINT16_MAX, uint16_t, u16);
	X(LIBCOOPGAMMA_UINT32, UINT32_MAX, uint32_t, u32);
	X(LIBCOOPGAMMA_UINT64, UINT64_MAX, uint64_t, u64);
#undef X
	case LIBCOOPGAMMA_FLOAT:
		libclut_rgb_brightness(&filter->ramps.f, (float)1, float, rvalue, gvalue, bvalue);
		libclut_clip(&filter->ramps.f, (float)1, float, 1, 1, 1);
		break;
	case LIBCOOPGAMMA_DOUBLE:
		libclut_rgb_brightness(&filter->ramps.d, (double)1, double, rvalue, gvalue, bvalue);
		libclut_clip(&filter->ramps.d, (double)1, double, 1, 1, 1);
		break;
	default:
		abort();
	}
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
	int r;
	size_t i, j;

	if (xflag)
		for (i = 0; i < filters_n; i++)
			crtc_updates[i].filter.lifespan = LIBCOOPGAMMA_REMOVE;
	else if (dflag)
		for (i = 0; i < filters_n; i++)
			crtc_updates[i].filter.lifespan = LIBCOOPGAMMA_UNTIL_DEATH;
	else
		for (i = 0; i < filters_n; i++)
			crtc_updates[i].filter.lifespan = LIBCOOPGAMMA_UNTIL_REMOVAL;

	if (!xflag && (r = make_slaves()) < 0)
		return r;

	for (i = 0, r = 1; i < filters_n; i++) {
		if (!crtc_updates[i].master || !crtc_info[crtc_updates[i].crtc].supported)
			continue;
		if (!xflag)
			fill_filter(&crtc_updates[i].filter);
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

	while (r != 1)
		if ((r = synchronise(-1)) < 0)
			return r;

	if (!dflag)
		return 0;

	if (libcoopgamma_set_nonblocking(&cg, 0) < 0)
		return -1;
	for (;;) {
		if (libcoopgamma_synchronise(&cg, NULL, 0, &j) < 0) {
			switch (errno) {
			case 0:
				break;
			case ENOTRECOVERABLE:
				goto enotrecoverable;
			default:
				return -1;
			}
		}
	}

enotrecoverable:
	pause();
	return -1;
}
