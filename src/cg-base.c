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

#include <libcoopgamma.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>


const char* argv0;



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


int main(int argc, char* argv[])
{
  argv0 = argv[0];
  
  if (initialise_proc() < 0)
    goto fail;
  
  list_methods();
  
  return 0;
 fail:
  perror(argv0);
  return 1;
}

