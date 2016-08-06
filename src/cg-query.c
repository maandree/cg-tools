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
 * The name of the selected CRTC
 */
static char* crtc = NULL;



/**
 * Print usage information and exit
 */
void usage(void)
{
  fprintf(stderr,
	  "Usage: %s [-M method] [-S site] -c crtc\n",
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
 *          on libcoopgamma error
 */
static int print_info(void)
{
  libcoopgamma_crtc_info_t info;
  char* str;
  int saved_errno;
  
  libcoopgamma_crtc_info_initialise(&info);
  
  if (libcoopgamma_get_gamma_info_sync(crtc, &info, &cg) < 0)
    goto cg_fail;;
  
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
      printf("Monitor's red colour (x, y): %lf, %lf\n",
	     info.red_x / (double)1024, info.red_y / (double)1024);
      
      printf("Monitor's green colour (x, y): %lf, %lf\n",
	     info.green_x / (double)1024, info.green_y / (double)1024);
      
      printf("Monitor's blue colour (x, y): %lf, %lf\n",
	     info.blue_x / (double)1024, info.blue_y / (double)1024);
    }
  
  return 0;
 fail:
  saved_errno = errno;
  libcoopgamma_crtc_info_destroy(&info);
  errno = saved_errno;
  return -1;
 cg_fail:
  saved_errno = errno;
  libcoopgamma_crtc_info_destroy(&info);
  errno = saved_errno;
  return -2;
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
 * @param   argc  The number of command line arguments
 * @param   argv  The command line arguments
 * @return        0 on success, 1 on error
 */
int main(int argc, char* argv[])
{
  int stage = 0;
  int rc = 0;
  char* method = NULL;
  char* site = NULL;
  
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
      if (crtc != NULL)
	usage();
      crtc = EARGF(usage());
      break;
    }
  ARGEND;
  
  if (initialise_proc() < 0)
    goto fail;
  
  if ((method != NULL) && !strcmp(method, "?"))
    {
      if ((site != NULL) || (crtc != NULL))
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
  
  if (!crtc)
    usage();
  
  if (!strcmp(crtc, "?"))
    switch (list_crtcs())
      {
      case 0:
	goto done;
      case -1:
	goto fail;
      default:
	goto cg_fail;
      }
  
  switch (print_info())
    {
    case 0:
      goto done;
    case -1:
      goto fail;
    default:
      goto cg_fail;
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

