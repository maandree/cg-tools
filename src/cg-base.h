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
#include <libcoopgamma.h>



/**
 * The process's name
 */
extern const char* argv0;

/**
 * The libcoopgamma context
 */
extern libcoopgamma_context_t cg;

/**
 * The names of the selected CRTC:s
 */
extern char** crtcs;

/**
 * CRTC and monitor information about
 * each selected CRTC and connect monitor
 */
extern libcoopgamma_crtc_info_t* crtc_info;

/**
 * The number of selected CRTC:s
 */
extern size_t crtcs_n;



/**
 * Print usage information and exit
 */
#if defined(__GNUC__)
__attribute__((__noreturn__))
#endif
extern void usage(void);

/**
 * Handle a command line option
 * 
 * @param   opt  The option, it is a NUL-terminate two-character
 *               string starting with either '-' or '+', if the
 *               argument is not recognised, call `usage`. This
 *               string will not be "-m", "-s", or "-c".
 * @param   arg  The argument associated with `opt`,
 *               `NULL` there is no next argument, if this
 *               parameter is `NULL` but needed, call `usage`
 * @return       0 if `arg` was not used,
 *               1 if `arg` was used,
 *               -1 on error
 */
#if defined(__GNUC__)
__attribute__((__nonnull__(1)))
#endif
extern int handle_opt(char* opt, char* arg);

/**
 * This function is called after the last
 * call to `handle_opt`
 * 
 * @param   argc    The number of unparsed arguments
 * @param   argv    `NULL` terminated list of unparsed arguments
 * @param   method  The argument associated with the "-m" option
 * @param   site    The argument associated with the "-s" option
 * @param   crtcs   The arguments associated with the "-c" options, `NULL`-terminated
 * @return          Zero on success, -1 on error
 */
#if defined(__GNUC__)
__attribute__((__nonnull__(2)))
#endif
extern int handle_args(int argc, char* argv[], char* method, char* site, char** crtcs);

