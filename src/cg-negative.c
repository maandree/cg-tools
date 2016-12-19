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
const int64_t default_priority = ((int64_t)1) << 62;

/**
 * The default class for the program
 */
char default_class[] = PKGNAME "::cg-negative::standard";

/**
 * Class suffixes
 */
const char* const* class_suffixes = (const char* const[]){NULL};



/**
 * -d: keep process alive and remove filter on death
 */
static int dflag = 0;

/**
 * -x: remove filter rather than adding it
 */
static int xflag = 0;

/**
 * +r: do not touch the red channel
 */
static int rplus = 0;

/**
 * +g: do not touch the green channel
 */
static int gplus = 0;

/**
 * +b: do not touch the blue channel
 */
static int bplus = 0;



/**
 * Print usage information and exit
 */
void usage(void)
{
  fprintf(stderr,
	  "Usage: %s [-M method] [-S site] [-c crtc]... [-R rule] (-x | [-p priority] [-d] [+rgb])\n",
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
    switch (opt[1])
      {
      case 'r':
	if (rplus)
	  usage();
	rplus = 1;
	break;
      case 'g':
	if (gplus)
	  usage();
	gplus = 1;
	break;
      case 'b':
	if (bplus)
	  usage();
	bplus = 1;
	break;
      default:
	usage();
      }
  return 0;
  (void) arg;
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
  int q = xflag + (dflag | rplus | gplus | bplus);
  if (argc || (q > 1) || (xflag && (prio != NULL)))
    usage();
  return 0;
  (void) argv;
}


/**
 * Fill a filter
 * 
 * @param  filter  The filter to fill
 */
static void fill_filter(libcoopgamma_filter_t* restrict filter)
{
  switch (filter->depth)
    {
#define X(CONST, MEMBER, MAX, TYPE)\
    case CONST:\
      libclut_negative(&(filter->ramps.MEMBER), MAX, TYPE, !rplus, !gplus, !bplus);\
      break;
LIST_DEPTHS
#undef X
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
    for (i = 0; i < filters_n; i++)
      crtc_updates[i].filter.lifespan = LIBCOOPGAMMA_REMOVE;
  else if (dflag)
    for (i = 0; i < filters_n; i++)
      crtc_updates[i].filter.lifespan = LIBCOOPGAMMA_UNTIL_DEATH;
  else
    for (i = 0; i < filters_n; i++)
      crtc_updates[i].filter.lifespan = LIBCOOPGAMMA_UNTIL_REMOVAL;
  
  if (!xflag)
    if ((r = make_slaves()) < 0)
      return r;
  
  for (i = 0, r = 1; i < filters_n; i++)
    {
      if (!(crtc_updates[i].master) || !(crtc_info[crtc_updates[i].crtc].supported))
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

