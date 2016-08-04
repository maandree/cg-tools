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

#include "cg-base.h"

#include <alloca.h>
#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



/**
 * The process's name
 */
const char* argv0 = NULL;

/**
 * The libcoopgamma context
 */
libcoopgamma_context_t cg;

/**
 * The names of the selected CRTC:s
 */
char** crtcs = NULL;

/**
 * Gamma ramp updates for each CRTC
 */
filter_update_t* crtc_updates = NULL;

/**
 * CRTC and monitor information about
 * each selected CRTC and connect monitor
 */
libcoopgamma_crtc_info_t* crtc_info = NULL;

/**
 * The number of selected CRTC:s
 */
size_t crtcs_n = 0;


/**
 * Contexts for asynchronous ramp updates
 */
static libcoopgamma_async_context_t* asyncs = NULL;

/**
 * The number of pending sends
 */
static size_t pending_sends = 0;

/**
 * The number of pending receives
 */
static size_t pending_recvs = 0;



/**
 * Data used to sort CRTC:s
 */
struct crtc_sort_data
{
  /**
   * The gamma ramp type
   */
  libcoopgamma_depth_t depth;
  
  /**
   * Should be 0
   */
  int __padding;
  
  /**
   * The size of the red gamma ramp
   */
  size_t red_size;
  
  /**
   * The size of the green gamma ramp
   */
  size_t green_size;
  
  /**
   * The size of the blue gamma ramp
   */
  size_t blue_size;
  
  /**
   * The index of the CRTC
   */
  size_t index;
};



/**
 * Compare two instances of `crtc_sort_data`
 * 
 * @param   a_  Return -1 if this one is lower
 * @param   b_  Return +1 if this one is higher
 * @return      See `a_` and `b_`, only -1 or +1 can be returned
 */
static int crtc_sort_data_cmp(const void* a_, const void* b_)
{
  const struct crtc_sort_data* a = a_;
  const struct crtc_sort_data* b = b_;
  int cmp = memcmp(a, b, sizeof(*a) - sizeof(a->index));
  return cmp ? cmp : a->index < b->index ? -1 : +1;
}


/**
 * Make elements in `crtc_updates` slaves where appropriate
 * 
 * @return  Zero on success, -1 on error
 */
int make_slaves(void)
{
  struct crtc_sort_data* data;
  size_t i, j, master = 0, master_i;
  
  data = alloca(crtcs_n * sizeof(*data));
  memset(data, 0, crtcs_n * sizeof(*data));
  for (i = 0; i < crtcs_n; i++)
    {
      data[i].depth      = crtc_updates[i].filter.depth;
      data[i].red_size   = crtc_updates[i].filter.ramps.u8.red_size;
      data[i].green_size = crtc_updates[i].filter.ramps.u8.green_size;
      data[i].blue_size  = crtc_updates[i].filter.ramps.u8.blue_size;
      data[i].index      = i;
    }
  
  qsort(data, crtcs_n, sizeof(*data), crtc_sort_data_cmp);
  
  for (i = 1; i < crtcs_n; i++)
    if (memcmp(data + i, data + master, sizeof(*data) - sizeof(data->index)))
      {
	if (master + 1 < i)
	  {
	    crtc_updates[master_i].slaves = calloc(i - master, sizeof(size_t));
	    if (crtc_updates[master_i].slaves == NULL)
	      return -1;
	    for (j = 0; master + j < i; j++)
	      crtc_updates[master_i].slaves[j] = data[master + j].index;
	  }
	master = i;
	master_i = data[master].index;
      }
    else
      {
	libcoopgamma_ramps_destroy(&(crtc_updates[data[i].index].filter.ramps.u8));
	crtc_updates[data[i].index].master = 0;
	crtc_updates[data[i].index].filter.ramps.u8 = crtc_updates[master_i].filter.ramps.u8;
      }
  
  if (master + 1 < i)
    {
      crtc_updates[master_i].slaves = calloc(i - master, sizeof(size_t));
      if (crtc_updates[master_i].slaves == NULL)
	return -1;
      for (j = 0; master + j < i; j++)
	crtc_updates[master_i].slaves[j] = data[master + j].index;
    }
  
  return 0;
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
 * Fill the list of CRTC information
 * 
 * @return  Zero on success, -1 on error, -2
 *          on libcoopgamma error
 */
static int get_crtc_info(void)
{
  size_t i, unsynced = 0, selected;
  char* synced;
  int saved_errno, need_flush = 0, fail_rc = -1;
  struct pollfd pollfd;
  
  synced = alloca(crtcs_n * sizeof(*synced));
  memset(synced, 0, crtcs_n * sizeof(*synced));
  
  i = 0;
  pollfd.fd = cg.fd;
  pollfd.events = POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI;
  
  for (;;)
    {
    wait:
      if (i < crtcs_n)
	pollfd.events |= POLLOUT;
      else
	pollfd.events &= ~POLLOUT;
      
      if (poll(&pollfd, (nfds_t)1, -1) < 0)
	goto fail;
      
      if (pollfd.revents & (POLLOUT | POLLERR | POLLHUP | POLLNVAL))
	{
	  if (need_flush && (libcoopgamma_flush(&cg) < 0))
	    goto send_fail;
	  need_flush = 0;
	  for (; i < crtcs_n; i++)
	    if (unsynced++, libcoopgamma_get_gamma_info_send(crtcs[i], &cg, asyncs + i) < 0)
	      goto send_fail;
	  goto send_done;
	send_fail:
	  switch (errno)
	    {
	    case EINTR:
	    case EAGAIN:
#if EAGAIN != EWOULDBLOCK
	    case EWOULDBLOCK:
#endif
	      i++;
	      need_flush = 1;
	      break;
	    default:
	      goto fail;
	    }
	}
    send_done:
      
      if (unsynced == 0)
	break;
      
      if (pollfd.revents & (POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI))
	while (unsynced > 0)
	  switch (libcoopgamma_synchronise(&cg, asyncs, i, &selected))
	    {
	    case 0:
	      if (synced[selected])
		break;
	      synced[selected] = 1;
	      unsynced -= 1;
	      if (libcoopgamma_get_gamma_info_recv(crtc_info + selected, &cg, asyncs + selected) < 0)
		goto cg_fail;
	      break;
	    case -1:
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
  
  return 0;
 cg_fail:
  fail_rc = -2;
 fail:
  return fail_rc;
}


/**
 * -m METHOD
 *     Select adjustment method. If METHOD is "?",
 *     available methods will be printed to stdout.
 * 
 * -s SITE
 *     Select site (display server instance).
 * 
 * -c CRTC
 *     Select CRT controller. If CRTC is "?", CRTC:s
 *     will be printed to stdout.
 *     
 *     This option can be used multiple times. If it
 *     is not used at all, all CRTC:s will be selected.
 * 
 * -p PRIORITY
 *     Select the priority for the filter, this should
 *     be a signed two's-complement integer. If
 *     PRIORITY is "?", the default priority for the
 *     program is printed to stdout.
 * 
 * @param   argc  The number of command line arguments
 * @param   argv  The command line arguments
 * @return        0 on success, 1 on error
 */
int main(int argc, char* argv[])
{
  int stage = 0;
  int dealloc_crtcs = 0;
  int rc = 0;
  char* method = NULL;
  char* site = NULL;
  size_t crtcs_i = 0;
  int64_t priority = default_priority;
  char* prio = NULL;
  
  argv0 = *argv++, argc--;
  
  if (initialise_proc() < 0)
    goto fail;
  
  crtcs = alloca(argc * sizeof(*crtcs));
  
  for (; *argv; argv++, argc--)
    {
      char* args = *argv;
      char opt[3];
      if (!strcmp(args, "--"))
	{
	  argv++, argc--;
	  break;
	}
      opt[0] = *args++;
      opt[2] = '\0';
      if ((*opt != '-') && (*opt != '+'))
	break;
      while (*args)
	{
	  char* arg;
	  int at_end;
	  opt[1] = *args++;
	  arg = args + 1;
	  if ((at_end = !*arg))
	    arg = argv[1];
	  if (!strcmp(opt, "-m"))
	    {
	      if ((method != NULL) || ((method = arg) == NULL))
		usage();
	    }
	  else if (!strcmp(opt, "-s"))
	    {
	      if ((site != NULL) || ((site = arg) == NULL))
		usage();
	    }
	  else if (!strcmp(opt, "-c"))
	    {
	      if (arg == NULL)
		usage();
	      crtcs[crtcs_i++] = arg;
	    }
	  else if (!strcmp(opt, "-p"))
	    {
	      if ((prio != NULL) || ((prio = arg) == NULL))
		usage();
	    }
	  else
	    switch (handle_opt(opt, arg))
	      {
	      case 0:
		goto next_arg;
	      case 1:
		break;
	      default:
		goto fail;
	      }
	  argv += at_end;
	  argc -= at_end;
	  goto next_arg;
	}
    next_arg:;
    }
  
  crtcs_n = crtcs_i;
  crtcs[crtcs_i] = NULL;
  if (handle_args(argc, argv, method, site, crtcs) < 0)
    goto fail;
  
  if ((prio != NULL) && !strcmp(prio, "?"))
    {
      printf("%" PRIi64 "\n", priority);
      return 0;
    }
  else if (prio != NULL)
    priority = (int64_t)atoll(prio);
  
  if ((method != NULL) && !strcmp(method, "?"))
    {
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
  
  while (crtcs_i--)
    if (!strcmp(crtcs[crtcs_i], "?"))
      switch (list_crtcs())
	{
	case 0:
	  goto done;
	case -1:
	  goto fail;
	default:
	  goto cg_fail;
	}
  
  if (crtcs_n == 0)
    {
      crtcs = libcoopgamma_get_crtcs_sync(&cg);
      if (crtcs == NULL)
	goto cg_fail;
      dealloc_crtcs = 1;
      for (; crtcs[crtcs_n] != NULL; crtcs_n++);
    }
  
  if (crtcs_n == 0)
    {
      fprintf(stderr, "%s: no CRTC:s are available\n", argv0);
      goto custom_fail;
    }
  
  crtc_info = alloca(crtcs_n * sizeof(*crtc_info));
  memset(crtc_info, 0, crtcs_n * sizeof(*crtc_info));
  for (crtcs_i = 0; crtcs_i < crtcs_n; crtcs_i++)
    if (libcoopgamma_crtc_info_initialise(crtc_info + crtcs_i) < 0)
      goto cg_fail;
  
  if (libcoopgamma_set_nonblocking(&cg, 1) < 0)
    goto fail;
  
  asyncs = alloca(crtcs_n * sizeof(*asyncs));
  memset(asyncs, 0, crtcs_n * sizeof(*asyncs));
  for (crtcs_i = 0; crtcs_i < crtcs_n; crtcs_i++)
    if (libcoopgamma_async_context_initialise(asyncs + crtcs_i) < 0)
      goto fail;
  
  crtc_updates = alloca(crtcs_n * sizeof(*crtc_updates));
  memset(crtc_updates, 0, crtcs_n * sizeof(*crtc_updates));
  for (crtcs_i = 0; crtcs_i < crtcs_n; crtcs_i++)
    {
      if (libcoopgamma_filter_initialise(&(crtc_updates[crtcs_i].filter)) < 0)
	goto fail;
      if (libcoopgamma_error_initialise(&(crtc_updates[crtcs_i].error)) < 0)
	goto fail;
      crtc_updates[crtcs_i].synced = 1;
      crtc_updates[crtcs_i].failed = 0;
      crtc_updates[crtcs_i].master = 1;
      crtc_updates[crtcs_i].slaves = NULL;
      crtc_updates[crtcs_i].filter.crtc                = crtcs[crtcs_i];
      crtc_updates[crtcs_i].filter.priority            = priority;
      crtc_updates[crtcs_i].filter.depth               = crtc_info[crtcs_i].depth;
      crtc_updates[crtcs_i].filter.ramps.u8.red_size   = crtc_info[crtcs_i].red_size;
      crtc_updates[crtcs_i].filter.ramps.u8.green_size = crtc_info[crtcs_i].green_size;
      crtc_updates[crtcs_i].filter.ramps.u8.blue_size  = crtc_info[crtcs_i].blue_size;
      switch (crtc_updates[crtcs_i].filter.depth)
	{
	case LIBCOOPGAMMA_UINT8:
	  libcoopgamma_ramps_initialise(&(crtc_updates[crtcs_i].filter.ramps.u8));
	  break;
	case LIBCOOPGAMMA_UINT16:
	  libcoopgamma_ramps_initialise(&(crtc_updates[crtcs_i].filter.ramps.u16));
	  break;
	case LIBCOOPGAMMA_UINT32:
	  libcoopgamma_ramps_initialise(&(crtc_updates[crtcs_i].filter.ramps.u32));
	  break;
	case LIBCOOPGAMMA_UINT64:
	  libcoopgamma_ramps_initialise(&(crtc_updates[crtcs_i].filter.ramps.u64));
	  break;
	case LIBCOOPGAMMA_FLOAT:
	  libcoopgamma_ramps_initialise(&(crtc_updates[crtcs_i].filter.ramps.f));
	  break;
	case LIBCOOPGAMMA_DOUBLE:
	  libcoopgamma_ramps_initialise(&(crtc_updates[crtcs_i].filter.ramps.d));
	  break;
	default:
	  fprintf(stderr, "%s: internal error: gamma ramp type is unrecognised\n", argv0);
	  goto custom_fail;
	}
    }
  
  switch (get_crtc_info())
    {
    case 0:
      break;
    case -1:
      goto fail;
    case -2:
      goto cg_fail;
    }
  
  switch (start())
    {
    case 0:
      break;
    case -1:
      goto fail;
    case -2:
      goto cg_fail;
    case -3:
      goto custom_fail;
    }
  
 done:
  if (dealloc_crtcs)
    free(crtcs);
  if (crtc_info != NULL)
    for (crtcs_i = 0; crtcs_i < crtcs_n; crtcs_i++)
      libcoopgamma_crtc_info_destroy(crtc_info + crtcs_i);
  if (asyncs != NULL)
    for (crtcs_i = 0; crtcs_i < crtcs_n; crtcs_i++)
      libcoopgamma_async_context_destroy(asyncs + crtcs_i);
  if (stage >= 1)
    libcoopgamma_context_destroy(&cg, stage >= 2);
  if (crtc_updates != NULL)
    for (crtcs_i = 0; crtcs_i < crtcs_n; crtcs_i++)
      {
	if (crtc_updates[crtcs_i].master == 0)
	  memset(&(crtc_updates[crtcs_i].filter.ramps.u8), 0, sizeof(crtc_updates[crtcs_i].filter.ramps.u8));
	crtc_updates[crtcs_i].filter.crtc = NULL;
	libcoopgamma_filter_destroy(&(crtc_updates[crtcs_i].filter));
	libcoopgamma_error_destroy(&(crtc_updates[crtcs_i].error));
	free(crtc_updates[crtcs_i].slaves);
      }
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

