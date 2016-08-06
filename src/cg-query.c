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
#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include "arg.h"

#include <libcoopgamma.h>

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



/**
 * The libcoopgamma context
 */
static libcoopgamma_context_t cg;

/**
 * Filter query
 */
static libcoopgamma_filter_query_t query;

/**
 * The class of the filter to print
 */
static char* class = NULL;



/**
 * Print usage information and exit
 */
void usage(void)
{
  fprintf(stderr,
	  "Usage: %s [-M method] [-S site] [-h high] [-l low] [-f class] -c crtc\n",
	  argv0);
  exit(1);
}


/**
 * Initialise the process, specifically
 * reset the signal mask and signal handlers
 * 
 * @return  Zero on success, -1 on error
 */
static int initialise_proc(void)
{
  sigset_t sigmask;
  int sig;
  
  for (sig = 1; sig < _NSIG; sig++)
    if (signal(sig, SIG_DFL) == SIG_ERR)
      if (sig == SIGCHLD)
	return -1;
  
  if (sigemptyset(&sigmask) < 0)
    return -1;
  if (sigprocmask(SIG_SETMASK, &sigmask, NULL) < 0)
    return -1;
  
  return 0;
}


/**
 * Print, to stdout, a list of all
 * recognised adjustment methods
 * 
 * @return  Zero on success, -1 on error
 */
static int list_methods(void)
{
  char** list;
  size_t i;
  
  list = libcoopgamma_get_methods();
  if (list == NULL)
    return -1;
  for (i = 0; list[i]; i++)
    printf("%s\n", list[i]);
  free(list);
  if (fflush(stdout) < 0)
    return -1;
  
  return 0;
}


/**
 * Print, to stdout, a list of all CRTC:s
 * 
 * A connection to the coopgamma server
 * must have been made
 * 
 * @return  Zero on success, -1 on error, -2
 *          on libcoopgamma error
 */
static int list_crtcs(void)
{
  char** list;
  size_t i;
  
  list = libcoopgamma_get_crtcs_sync(&cg);
  if (list == NULL)
    return -2;
  for (i = 0; list[i]; i++)
    printf("%s\n", list[i]);
  free(list);
  if (fflush(stdout) < 0)
    return -1;
  
  return 0;
}


/**
 * Print, to stdout, information about
 * the selected CRTC
 * 
 * @return  Zero on success, -1 on error, -2
 *          on libcoopgamma error, -3 on error
 *          with error message already printed
 */
static int print_info(void)
{
  libcoopgamma_crtc_info_t info;
  libcoopgamma_filter_table_t table;
  char* str;
  int saved_errno, ret = 0;
  size_t i;
  
  if (libcoopgamma_crtc_info_initialise(&info) < 0)
    return -1;
  if (libcoopgamma_filter_table_initialise(&table) < 0)
    {
      saved_errno = errno;
      libcoopgamma_crtc_info_destroy(&info);
      errno = saved_errno;
      return -1;
    }
  
  if (libcoopgamma_get_gamma_info_sync(query.crtc, &info, &cg) < 0)
    goto cg_fail;
  
  printf("Cooperative gamma server running: %s\n",
	 info.cooperative ? "yes" : "no");
  
  printf("Gamma adjustments supported: %s\n",
	 info.supported == LIBCOOPGAMMA_MAYBE ? "maybe" : info.supported ? "yes" : "no");
  
  printf("Gamma ramps stops (red green blue): %zu %zu %zu\n",
	 info.red_size, info.green_size, info.blue_size);
  
  switch (info.depth)
    {
    case LIBCOOPGAMMA_DOUBLE:  str = "double-precision floating-point";  break;
    case LIBCOOPGAMMA_FLOAT:   str = "single-precision floating-point";  break;
    case LIBCOOPGAMMA_UINT8:   str = "unsigned 8-bit integer";           break;
    case LIBCOOPGAMMA_UINT16:  str = "unsigned 16-bit integer";          break;
    case LIBCOOPGAMMA_UINT32:  str = "unsigned 32-bit integer";          break;
    case LIBCOOPGAMMA_UINT64:  str = "unsigned 64-bit integer";          break;
    default:
      errno = EPROTO;
      goto fail;
    }
  printf("Gamma ramps stops value type: %s\n", str);
  
  if (info.colourspace != LIBCOOPGAMMA_UNKNOWN)
    {
      switch (info.colourspace)
	{
	case LIBCOOPGAMMA_SRGB:     str = "sRGB";                              break;
	case LIBCOOPGAMMA_RGB:      str = "non-standard RGB";                  break;
	case LIBCOOPGAMMA_NON_RGB:  str = "non-RGB multicolour";               break;
	case LIBCOOPGAMMA_GREY:     str = "monochrome or singlecolour scale";  break;
	default:
	  errno = EPROTO;
	  goto fail;
	}
      printf("Monitor's colourspace: %s\n", str);
    }
  
  if (info.have_gamut)
    {
      printf("Monitor's red colour (x y): %lf, %lf\n",
	     info.red_x / (double)1024, info.red_y / (double)1024);
      
      printf("Monitor's green colour (x y): %lf, %lf\n",
	     info.green_x / (double)1024, info.green_y / (double)1024);
      
      printf("Monitor's blue colour (x y): %lf, %lf\n",
	     info.blue_x / (double)1024, info.blue_y / (double)1024);
    }
  
  if (libcoopgamma_get_gamma_sync(&query, &table, &cg) < 0)
    goto cg_fail;
  
  if ((table.red_size != info.red_size) || (table.green_size != info.green_size) ||
      (table.blue_size != info.blue_size) || (table.depth != info.depth))
    {
      fprintf(stderr, "%s: gamma ramp structure changed between queries\n", argv0);
      goto custom_fail;
    }
  
  printf("Filters: %zu\n", table.filter_count);
  for (i = 0; i < table.filter_count; i++)
    {
      printf("  Filter %zu:\n", i);
      printf("    Priority: %" PRIi64 "\n", table.filters[i].priority);
      printf("    Class: %s\n", table.filters[i].class);
    }
  
 done:
  saved_errno = errno;
  libcoopgamma_crtc_info_destroy(&info);
  libcoopgamma_filter_table_destroy(&table);
  errno = saved_errno;
  return ret;
 fail:
  ret = -1;
  goto done;
 cg_fail:
  ret = -2;
  goto done;
 custom_fail:
  ret = -3;
  goto done;
}



/**
 * Print, to stdout, the ramps of the select
 * filter on the select CRTC
 * 
 * @return  Zero on success, -1 on error, -2
 *          on libcoopgamma error, -3 on error
 *          with error message already printed
 */
static int print_filter(void)
{
  libcoopgamma_filter_table_t table;
  libcoopgamma_ramps_t* restrict ramps;
  int saved_errno, ret = 0;
  size_t i, n;
  
  if (libcoopgamma_filter_table_initialise(&table) < 0)
    return -1;
  
  if (libcoopgamma_get_gamma_sync(&query, &table, &cg) < 0)
    goto cg_fail;
  
  if (query.coalesce)
    i = 0;
  else
    for (i = 0; i < table.filter_count; i++)
      if (!strcmp(table.filters[i].class, class))
	break;
  if (i == table.filter_count)
    {
      fprintf(stderr, "%s: selected filter does not exist on selected CRTC\n", argv0);
      goto custom_fail;
    }
  ramps = &(table.filters[i].ramps);
  
  n = table.red_size;
  if (n < table.green_size)
    n = table.green_size;
  if (n < table.blue_size)
    n = table.blue_size;
  
  switch (table.depth)
    {
#define X(CONST, MEMBER, TYPE, FORMAT, DASH)				\
    case CONST:								\
      for (i = 0; i < n; i++)						\
	{								\
	  if (i < ramps->MEMBER.red_size)				\
	    printf("%" FORMAT " ", (TYPE)(ramps->MEMBER.red[i]));	\
	  else								\
	    printf(DASH " ");						\
	  if (i < ramps->MEMBER.green_size)				\
	    printf("%" FORMAT " ", (TYPE)(ramps->MEMBER.green[i]));	\
	  else								\
	    printf(DASH " ");						\
	  if (i < ramps->MEMBER.blue_size)				\
	    printf("%" FORMAT "\n", (TYPE)(ramps->MEMBER.blue[i]));	\
	  else								\
	    printf(DASH "\n");						\
	}								\
      break
    X(LIBCOOPGAMMA_DOUBLE, d,   double,   "lf",         "----");
    X(LIBCOOPGAMMA_FLOAT,  f,   double,   "lf",         "----");
    X(LIBCOOPGAMMA_UINT8,  u8,  uint8_t,  "02"  PRIx8,  "--");
    X(LIBCOOPGAMMA_UINT16, u16, uint16_t, "04"  PRIx16, "----");
    X(LIBCOOPGAMMA_UINT32, u32, uint32_t, "08"  PRIx32, "--------");
    X(LIBCOOPGAMMA_UINT64, u64, uint64_t, "016" PRIx64, "----------------");
#undef X
    default:
      errno = EPROTO;
      goto fail;
    }
  
 done:
  saved_errno = errno;
  libcoopgamma_filter_table_destroy(&table);
  errno = saved_errno;
  return ret;
 fail:
  ret = -1;
  goto done;
 cg_fail:
  ret = -2;
  goto done;
 custom_fail:
  ret = -3;
  goto done;
}


/**
 * -M METHOD
 *     Select adjustment method. If METHOD is "?",
 *     available methods will be printed to stdout.
 * 
 * -S SITE
 *     Select site (display server instance).
 * 
 * -c CRTC
 *     Select CRT controller. If CRTC is "?", CRTC:s
 *     will be printed to stdout.
 * 
 * -h HIGH
 *     Suppress filter with higher priority than HIGH.
 * 
 * -l LOW
 *     Suppress filter with lower priority than LOW.
 * 
 * -f CLASS
 *     Print gamma ramps of the filter with class CLASS
 *     on the selected CRTC. If CLASS is "*" all filters
 *     with a priority in [LOW, HIGH] are coalesced.
 * 
 * @param   argc  The number of command line arguments
 * @param   argv  The command line arguments
 * @return        0 on success, 1 on error
 */
int main(int argc, char* argv[])
{
  int stage = 0, haveh = 0, havel = 0;
  int rc = 0;
  char* method = NULL;
  char* site = NULL;
  
  query.high_priority = INT64_MAX;
  query.low_priority = INT64_MIN;
  query.crtc = NULL;
  query.coalesce = 0;
  
  ARGBEGIN
    {
    case 'M':
      if (method != NULL)
	usage();
      method = EARGF(usage());
      break;
    case 'S':
      if (site != NULL)
	usage();
      site = EARGF(usage());
      break;
    case 'c':
      if (query.crtc != NULL)
	usage();
      query.crtc = EARGF(usage());
      break;
    case 'h':
      if (haveh++)
	usage();
      query.high_priority = (int64_t)atoll(EARGF(usage()));
      break;
    case 'l':
      if (havel++)
	usage();
      query.low_priority = (int64_t)atoll(EARGF(usage()));
      break;
    case 'f':
      if (class != NULL)
	usage();
      class = EARGF(usage());
      if ((class[0] == '*') && (class[1] == '\0'))
	query.coalesce = 1;
      break;
    }
  ARGEND;
  
  if (initialise_proc() < 0)
    goto fail;
  
  if ((method != NULL) && !strcmp(method, "?"))
    {
      if ((site != NULL) || (query.crtc != NULL))
	usage();
      if (list_methods() < 0)
	goto fail;
      return 0;
    }
  
  if (libcoopgamma_context_initialise(&cg) < 0)
    goto fail;
  stage++;
  if (libcoopgamma_connect(method, site, &cg) < 0)
    {
      fprintf(stderr, "%s: server failed to initialise\n", argv0);
      goto custom_fail;
    }
  stage++;
  
  if (!(query.crtc))
    usage();
  
  if (!strcmp(query.crtc, "?"))
    switch (list_crtcs())
      {
      case 0:
	goto done;
      case -1:
	goto fail;
      default:
	goto cg_fail;
      }
  
  switch (class ? print_filter() : print_info())
    {
    case 0:
      goto done;
    case -1:
      goto fail;
    case -2:
      goto cg_fail;
    default:
      goto custom_fail;
    }
  
  fflush(stdout);
  if (ferror(stdout))
    goto fail;
  if (fclose(stdout) < 0)
    goto fail;
  
 done:
  if (stage >= 1)
    libcoopgamma_context_destroy(&cg, stage >= 2);
  return rc;
  
 custom_fail:
  rc = 1;
  goto done;
  
 fail:
  rc = 1;
  perror(argv0);
  goto done;
  
 cg_fail:
  rc = 1;
  {
    const char* side = cg.error.server_side ? "server" : "client";
    if (cg.error.custom)
      {
	if ((cg.error.number != 0) || (cg.error.description != NULL))
	  fprintf(stderr, "%s: %s-side error number %" PRIu64 ": %s\n",
		  argv0, side, cg.error.number, cg.error.description);
	else if (cg.error.number != 0)
	  fprintf(stderr, "%s: %s-side error number %" PRIu64 "\n", argv0, side, cg.error.number);
	else if (cg.error.description != NULL)
	  fprintf(stderr, "%s: %s-side error: %s\n", argv0, side, cg.error.description);
      }
    else if (cg.error.description != NULL)
      fprintf(stderr, "%s: %s-side error: %s\n", argv0, side, cg.error.description);
    else
      fprintf(stderr, "%s: %s-side error: %s\n", argv0, side, strerror(cg.error.number));
  }
  goto done;
}

