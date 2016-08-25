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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>



/**
 * The default filter priority for the program
 */
const int64_t default_priority = ((int64_t)1) << 61;

/**
 * The default class for the program
 */
char* const default_class = PKGNAME "::cg-brilliance::standard";



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
void usage(void)
{
  fprintf(stderr,
	  "Usage: %s [-M method] [-S site] [-c crtc]... [-R rule] "
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
int handle_opt(char* opt, char* arg)
{
  if (opt[0] == '-')
    switch (opt[1])
      {
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
  if (errno || (out < 0) || isinf(*out) || isnan(*out) || *end)
    return -1;
  if (!strchr("0123456789.", *str))
    return -1;
  return 0;
}


/**
 * This function is called after the last
 * call to `handle_opt`
 * 
 * @param   argc    The number of unparsed arguments
 * @param   argv    `NULL` terminated list of unparsed arguments
 * @param   method  The argument associated with the "-M" option
 * @param   site    The argument associated with the "-S" option
 * @param   crtcs   The arguments associated with the "-c" options, `NULL`-terminated
 * @param   prio    The argument associated with the "-p" option
 * @param   rule    The argument associated with the "-R" option
 * @return          Zero on success, -1 on error
 */
int handle_args(int argc, char* argv[], char* method, char* site,
		char** crtcs, char* prio, char* rule)
{
  char* red;
  char* green;
  char* blue;
  int q = xflag + dflag;
  q += (method != NULL) &&  !strcmp(method, "?");
  q += (prio   != NULL) &&  !strcmp(prio, "?");
  q += (rule   != NULL) && (!strcmp(rule, "?") || !strcmp(rule, "??"));
  for (; *crtcs; crtcs++)
    q += !strcmp(*crtcs, "?");
  if ((q > 1) || (xflag && (prio != NULL || argc)))
    usage();
  if (argc == 1)
    red = green = blue = argv[0];
  else if (argc == 3)
    {
      red   = argv[0];
      green = argv[1];
      blue  = argv[2];
    }
  else if (argc || !xflag)
    usage();
  if (argc)
    {
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
static void fill_filter(libcoopgamma_filter_t* restrict filter)
{
  size_t i;
  switch (filter->depth)
    {
#define X(CONST, MAX, TYPE, MEMBER)							\
      case CONST:									\
      for (i = 0; i < filter->ramps.MEMBER.red_size; i++)				\
	{										\
	  double val = (double)(filter->ramps.MEMBER.red[i]);				\
	  val *= rvalue;								\
	  if      (val < 0)              filter->ramps.MEMBER.red[i] = 0;		\
	  else if (val > (double)(MAX))  filter->ramps.MEMBER.red[i] = MAX;		\
	  else                           filter->ramps.MEMBER.red[i] = (TYPE)val;	\
	}										\
      for (i = 0; i < filter->ramps.MEMBER.green_size; i++)				\
	{										\
	  double val = (double)(filter->ramps.MEMBER.green[i]);				\
	  val *= gvalue;								\
	  if      (val < 0)              filter->ramps.MEMBER.green[i] = 0;		\
	  else if (val > (double)(MAX))  filter->ramps.MEMBER.green[i] = MAX;		\
	  else                           filter->ramps.MEMBER.green[i] = (TYPE)val;	\
	}										\
      for (i = 0; i < filter->ramps.MEMBER.blue_size; i++)				\
	{										\
	  double val = (double)(filter->ramps.MEMBER.blue[i]);				\
	  val *= bvalue;								\
	  if      (val < 0)              filter->ramps.MEMBER.blue[i] = 0;		\
	  else if (val > (double)(MAX))  filter->ramps.MEMBER.blue[i] = MAX;		\
	  else                           filter->ramps.MEMBER.blue[i] = (TYPE)val;	\
	}										\
      break
    X(LIBCOOPGAMMA_UINT8,  UINT8_MAX,  uint8_t,  u8);
    X(LIBCOOPGAMMA_UINT16, UINT16_MAX, uint16_t, u16);
    X(LIBCOOPGAMMA_UINT32, UINT32_MAX, uint32_t, u32);
    X(LIBCOOPGAMMA_UINT64, UINT64_MAX, uint64_t, u64);
#undef X
    case LIBCOOPGAMMA_FLOAT:
       libclut_rgb_brightness(&(filter->ramps.f), (float)1, float, rvalue, gvalue, bvalue);
       libclut_clip(&(filter->ramps.f), (float)1, float, 1, 1, 1);
      break;
    case LIBCOOPGAMMA_DOUBLE:
       libclut_rgb_brightness(&(filter->ramps.d), (double)1, double, rvalue, gvalue, bvalue);
       libclut_clip(&(filter->ramps.d), (double)1, double, 1, 1, 1);
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
int start(void)
{
  int r;
  size_t i, j;
  
  if (xflag)
    for (i = 0; i < crtcs_n; i++)
      crtc_updates[i].filter.lifespan = LIBCOOPGAMMA_REMOVE;
  else if (dflag)
    for (i = 0; i < crtcs_n; i++)
      crtc_updates[i].filter.lifespan = LIBCOOPGAMMA_UNTIL_DEATH;
  else
    for (i = 0; i < crtcs_n; i++)
      crtc_updates[i].filter.lifespan = LIBCOOPGAMMA_UNTIL_REMOVAL;
  
  if (!xflag)
    if ((r = make_slaves()) < 0)
      return r;
  
  for (i = 0, r = 1; i < crtcs_n; i++)
    {
      if (!(crtc_updates[i].master) || !(crtc_info[i].supported))
	continue;
      if (!xflag)
	fill_filter(&(crtc_updates[i].filter));
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
  
  if (!dflag)
    return 0;
  
  if (libcoopgamma_set_nonblocking(&cg, 0) < 0)
    return -1;
  for (;;)
    if (libcoopgamma_synchronise(&cg, NULL, 0, &j) < 0)
      switch (errno)
	{
	case 0:
	  break;
	case ENOTRECOVERABLE:
	  goto enotrecoverable;
	default:
	  return -1;
	}
  
 enotrecoverable:
  for (;;)
    if (pause() < 0)
      return -1;
}

