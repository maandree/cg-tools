/**
 * cg-tools -- Cooperative gamma-enabled tools
 * Copyright (C) 2016  Mattias Andrée (maandree@kth.se)
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "cg-base.h"

#include <libclut.h>

#include <errno.h>
#include <math.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>



/**
 * The default filter priority for the program
 */
const int64_t default_priority = ((int64_t)1) << 60;

/**
 * The default class for the program
 */
char default_class[] = PKGNAME "::cg-rainbow::standard";

/**
 * Class suffixes
 */
const char* const* class_suffixes = (const char* const[]){NULL};



/**
 * -s: rainbow-frequency in Hz
 */
static char* sflag = NULL;

/**
 * -l: base luminosity
 */
static char* lflag = NULL;

/**
 * The rainbow-frequency multiplied by 3
 */
double rainbows_per_third_second = 1;

/**
 * The base luminosity
 */
double luminosity = (double)1 / 3;



/**
 * Print usage information and exit
 */
void usage(void)
{
  fprintf(stderr,
	  "Usage: %s [-M method] [-S site] [-c crtc]... [-R rule] [-p priority]"
	  " [-l luminosity] [-s rainbowhz]\n",
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
int handle_opt(char* opt, char* arg)
{
  if (opt[0] == '-')
    switch (opt[1])
      {
      case 'l':
	if (lflag || !(lflag = arg))
	  usage();
	return 1;
      case 's':
	if (sflag || !(sflag = arg))
	  usage();
	return 1;
      default:
	usage();
      }
  else
    usage();
  return 0;
}


/**
 * Parse a non-negative double encoded as a string
 * 
 * @param   out  Output parameter for the value
 * @param   str  The string
 * @retunr       Zero on success, -1 if the string is invalid
 */
static int parse_double(double* restrict out, const char* restrict str)
{
  char* end;
  errno = 0;
  *out = strtod(str, &end);
  if (errno || (*out < 0) || isinf(*out) || isnan(*out) || *end)
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
int handle_args(int argc, char* argv[], char* prio)
{
  int q = (lflag || sflag);
  if ((q > 1) || argc)
    usage();
  if (sflag != NULL)
    {
      if (parse_double(&rainbows_per_third_second, sflag) < 0)
	usage();
      rainbows_per_third_second *= 3;
    }
  if (lflag != NULL)
    {
      if (parse_double(&luminosity, lflag) < 0)
	usage();
    }
  return 0;
  (void) argv;
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
static void fill_filter(libcoopgamma_filter_t* restrict filter, double red, double green, double blue)
{
  switch (filter->depth)
    {
#define X(CONST, MEMBER, MAX, TYPE)\
    case CONST:\
      libclut_start_over(&(filter->ramps.MEMBER), MAX, TYPE, 1, 1, 1);\
      libclut_rgb_brightness(&(filter->ramps.MEMBER), MAX, TYPE, red, green, blue);\
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
static int double_time(double* restrict now)
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
int start(void)
{
  int r;
  size_t i, j;
  double pal[3];
  double t, starttime;
  
  for (i = 0; i < filters_n; i++)
    crtc_updates[i].filter.lifespan = LIBCOOPGAMMA_UNTIL_DEATH;
  
  if ((r = make_slaves()) < 0)
    return r;
  
  if ((r = double_time(&starttime)) < 0)
    return r;
  
  for (;;)
    {
      if ((r = double_time(&t)) < 0)
	return r;
      t -= starttime;
      t *= rainbows_per_third_second;
      pal[0] = pal[1] = pal[2] = luminosity;
      pal[((long)t) % 3] += 1 - fmod(t, 1);
      pal[((long)t + 1) % 3] += fmod(t, 1);
      if (pal[0] > 1)  pal[0] = 1;
      if (pal[1] > 1)  pal[1] = 1;
      if (pal[2] > 1)  pal[2] = 1;
      
      for (i = 0, r = 1; i < filters_n; i++)
	{
	  if (!(crtc_updates[i].master) || !(crtc_info[crtc_updates[i].crtc].supported))
	    continue;
	  fill_filter(&(crtc_updates[i].filter), pal[0], pal[1], pal[2]);
	  r = update_filter(i, 0);
	  if ((r == -2) || ((r == -1) && (errno != EAGAIN)))
	    return r;
	  if (crtc_updates[i].slaves != NULL)
	    for (j = 0; crtc_updates[i].slaves[j] != 0; j++)
	      {
		r = update_filter(crtc_updates[i].slaves[j], 0);
		if ((r == -2) || ((r == -1) && (errno != EAGAIN)))
		  return r;
	      }
	}
      
      while (r != 1)
	if ((r = synchronise(-1)) < 0)
	  return r;
      
      sched_yield();
    }
}

