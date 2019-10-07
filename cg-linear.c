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
const int64_t default_priority = NO_DEFAULT_PRIORITY;

/**
 * The default class base for the program
 */
char default_class[] = PKGNAME "::cg-linear::standard";

/**
 * Class suffixes
 */
const char* const* class_suffixes = (const char* const[]){":start", ":stop", NULL};



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
 * The priority of the linearisation filter
 */
static int64_t start_priority;

/**
 * The priority of the delinearisation filter
 */
static int64_t stop_priority;



/**
 * Print usage information and exit
 */
void usage(void)
{
  fprintf(stderr,
	  "Usage: %s [-M method] [-S site] [-c crtc]... [-R rule-base] "
	  "(-x | -p start-priority:stop-priority [-d] [+rgb])\n",
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
  char *p, *end;
  if (argc || (q > 1) || (xflag && (prio != NULL)))
    usage();
  if (!xflag && (prio == NULL))
    usage();
  if (prio != NULL)
    {
      p = strchr(prio, ':');
      if (!p)
	usage();
      *p++ = '\0';
      errno = 0;
      start_priority = (size_t)strtoul(prio, &end, 10);
      if (errno || *end || !*prio)
	usage();
      stop_priority = (size_t)strtoul(p, &end, 10);
      if (errno || *end || !*prio)
	usage();
      p[-1] = ':';
    }
  return 0;
  (void) argv;
}


/**
 * Fill a filter
 * 
 * @param  filter    The filter to fill
 * @param  is_start  If the fitler is a linearisation filter
 */
static void fill_filter(libcoopgamma_filter_t* restrict filter, int is_start)
{
  switch (filter->depth)
    {
#define X(CONST, MEMBER, MAX, TYPE)\
    case CONST:\
      if (is_start)\
        libclut_linearise(&(filter->ramps.MEMBER), MAX, TYPE, !rplus, !gplus, !bplus);\
      else\
        libclut_standardise(&(filter->ramps.MEMBER), MAX, TYPE, !rplus, !gplus, !bplus);\
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
  
  for (i = 0, r = 1; i < filters_n; i++)
    {
      if (!(crtc_info[crtc_updates[i].crtc].supported))
	continue;
      if (!xflag) {
	int is_start = strchr(crtc_updates[i].filter.class, '\0')[-1] == 't';
	fill_filter(&(crtc_updates[i].filter), is_start);
	crtc_updates[i].filter.priority = is_start ? start_priority : stop_priority;
      }
      r = update_filter(i, 0);
      if ((r == -2) || ((r == -1) && (errno != EAGAIN)))
	return r;
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
