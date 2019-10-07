/* See LICENSE file for copyright and license details. */
#include "arg.h"

#include <libcoopgamma.h>

#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



/**
 * The libcoopgamma context
 */
static libcoopgamma_context_t cg;



/**
 * Print usage information and exit
 */
static void usage(void)
{
  fprintf(stderr,
	  "Usage: %s [-M method] [-S site] [-c crtc]... class...\n",
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
 * Remove selected filters from selected CRTC:s
 * 
 * @param   crtcs    `NULL`-terminated list of CRTC names
 * @param   classes  `NULL`-terminated list of filter classes
 * @return           Zero on success, -1 on error, -2 on
 *                   libcoopgamma error
 */
static int remove_filters(char* const* restrict crtcs, char* const* restrict classes)
{
  size_t n = 0, unsynced = 0, selected, i, j;
  char* synced = NULL;
  libcoopgamma_async_context_t* asyncs = NULL;
  int saved_errno, need_flush = 0, ret = 0;
  struct pollfd pollfd;
  libcoopgamma_filter_t command;
  
  for (i = 0; crtcs[i] != NULL; i++);
  for (j = 0; classes[j] != NULL; j++);
  synced = calloc(i, j * sizeof(*synced));
  if (synced == NULL)
    goto fail;
  asyncs = calloc(i, j * sizeof(*asyncs));
  if (asyncs == NULL)
    goto fail;
  
  i = j = 0;
  command.lifespan = LIBCOOPGAMMA_REMOVE;
  pollfd.fd = cg.fd;
  pollfd.events = POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI;
  
  while ((unsynced > 0) || (crtcs[i] != NULL))
    {
    wait:
      if (crtcs[i] != NULL)
	pollfd.events |= POLLOUT;
      else
	pollfd.events &= ~POLLOUT;
      
      pollfd.revents = 0;
      if (poll(&pollfd, (nfds_t)1, -1) < 0)
	goto fail;
      
      if (pollfd.revents & (POLLOUT | POLLERR | POLLHUP | POLLNVAL))
	{
	  if (need_flush && (libcoopgamma_flush(&cg) < 0))
	    goto send_fail;
	  need_flush = 0;
	  for (; crtcs[i] != NULL; i++, j = 0)
	    {
	      command.crtc = crtcs[i];
	      while (classes[j] != NULL)
		{
		  command.class = classes[j++];
		  if (unsynced++, libcoopgamma_set_gamma_send(&command, &cg, asyncs + n++) < 0)
		    goto send_fail;
		}
	    }
	  goto send_done;
	send_fail:
	  switch (errno)
	    {
	    case EINTR:
	    case EAGAIN:
#if EAGAIN != EWOULDBLOCK
	    case EWOULDBLOCK:
#endif
	      need_flush = 1;
	      if (classes[j] == NULL)
		i++, j = 0;
	      break;
	    default:
	      goto fail;
	    }
	}
    send_done:
      
      if ((unsynced == 0) && (crtcs[i] == NULL))
	break;
      
      if (pollfd.revents & (POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI))
	while (unsynced > 0)
	  switch (libcoopgamma_synchronise(&cg, asyncs, n, &selected))
	    {
	    case 0:
	      if (synced[selected])
		{
		  libcoopgamma_skip_message(&cg);
		  break;
		}
	      synced[selected] = 1;
	      unsynced -= 1;
	      if (libcoopgamma_set_gamma_recv(&cg, asyncs + selected) < 0)
		goto cg_fail;
	      break;
	    default:
	      switch (errno)
		{
		case 0:
		  break;
		case EINTR:
		case EAGAIN:
#if EAGAIN != EWOULDBLOCK
		case EWOULDBLOCK:
#endif
		  goto wait;
		default:
		  goto fail;
		}
	      break;
	    }
    }
  
 done:
  saved_errno = errno;
  free(synced);
  free(asyncs);
  errno = saved_errno;
  return ret;
 fail:
  ret = -1;
  goto done;
 cg_fail:
  ret = -2;
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
 *     Can be used multiple times. If not used, all
 *     CRTC:s are selected.
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
  char** crtcs_ = NULL;
  char** crtcs = alloca(argc * sizeof(char*));
  size_t i, crtcs_n = 0;
  
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
      crtcs[crtcs_n++] = EARGF(usage());
      break;
    default:
      usage();
    }
  ARGEND;
  
  if (initialise_proc() < 0)
    goto fail;
  
  if ((method != NULL) && !strcmp(method, "?"))
    {
      if ((site != NULL) || (crtcs_n > 0) || (argc > 0))
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
  
  for (i = 0; i < crtcs_n; i++)
    if (!strcmp(crtcs[i], "?"))
      {
	if (argc > 0)
	  usage();
	switch (list_crtcs())
	  {
	  case 0:
	    goto done;
	  case -1:
	    goto fail;
	  default:
	    goto cg_fail;
	  }
      }
  
  if (argc == 0)
    usage();
  
  if (crtcs_n == 0)
    {
      crtcs = crtcs_ = libcoopgamma_get_crtcs_sync(&cg);
      if (crtcs == NULL)
	goto cg_fail;
    }
  else
    crtcs[crtcs_n] = NULL;
  
  if (libcoopgamma_set_nonblocking(&cg, 1) < 0)
    goto fail;
  
  switch (remove_filters(crtcs, argv))
    {
    case 0:
      break;
    case -1:
      goto fail;
    default:
      goto cg_fail;
    }
  
 done:
  if (stage >= 1)
    libcoopgamma_context_destroy(&cg, stage >= 2);
  free(crtcs_);
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
