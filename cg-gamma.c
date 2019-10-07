/* See LICENSE file for copyright and license details. */
#include "cg-base.h"

#include <libclut.h>

#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>



/**
 * The default filter priority for the program
 */
const int64_t default_priority = 0;

/**
 * The default class for the program
 */
char default_class[] = PKGNAME "::cg-gamma::standard";

/**
 * Class suffixes
 */
const char *const *class_suffixes = (const char *const[]){NULL};



/**
 * -d: keep process alive and remove filter on death
 */
static int dflag = 0;

/**
 * -x: remove filter rather than adding it
 */
static int xflag = 0;

/**
 * -f: gamma listing file
 */
static char *fflag = NULL;

/**
 * The gamma of the red channel
 */
static double rgamma = 1;

/**
 * The gamma of the green channel
 */
static double ggamma = 1;

/**
 * The gamma of the blue channel
 */
static double bgamma = 1;

/**
 * `NULL`-terminated list of output
 * names listed in the configuration file
 */
static char **names = NULL;

/**
 * The gamma of the red channel on monitor
 * with same index in `names`
 */
static double *rgammas = NULL;

/**
 * The gamma of the green channel on monitor
 * with same index in `names`
 */
static double *ggammas = NULL;

/**
 * The gamma of the blue channel on monitor
 * with same index in `names`
 */
static double *bgammas = NULL;



/**
 * Print usage information and exit
 */
void
usage(void)
{
	fprintf(stderr,
	        "usage: %s [-M method] [-S site] [-c crtc]... [-R rule] "
	        "(-x | [-p priority] [-d] [-f file | all | red green blue])\n",
	        argv0);
	exit(1);
}


/**
 * Perform cleanup so valgrind output is clean
 * 
 * @param   ret  The value to return
 * @return       `ret` is returned as is
 */
static int
cleanup(int ret)
{
	int saved_errno = errno;
	if (names) {
		char **p = names;
		while (*p)
			free(*p++);
	}
	free(names);
	free(rgammas);
	free(ggammas);
	free(bgammas);
	errno = saved_errno;
	return ret;
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
int
handle_opt(char *opt, char *arg)
{
	if (opt[0] == '-') {
		switch (opt[1]) {
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
		case 'f':
			if (fflag || !(fflag = arg))
				usage();
			return 1;
		default:
			usage();
		}
	}
	return 0;
}


/**
 * Get the pathname of a configuration file
 * 
 * @param   confname   The filename (excluding directory) of the configuration file
 * @return             The full pathname of the configuration file, `NULL` on error
 */
static char *
get_conf_file(const char *restrict confname)
{
	struct passwd *pw;
	char *path;

	pw = getpwuid(getuid());
	if (!pw || !pw->pw_dir)
		return NULL;

	path = malloc(strlen(pw->pw_dir) + strlen(confname) + sizeof("/.config/"));
	if (!path)
		return NULL;

	sprintf(path, "%s/.config/%s", pw->pw_dir, confname);

	if (access(path, F_OK) < 0)
		sprintf(path, "/etc/%s", confname);

	return path;
}


/**
 * Parse a non-negative double encoded as a string
 * 
 * @param   out  Output parameter for the value
 * @param   str  The string
 * @return       Zero on success, -1 if the string is invalid
 */
static int
parse_double(double *restrict out, const char *restrict str)
{
	char *end;
	errno = 0;
	*out = strtod(str, &end);
	if (errno || *out < 0 || isinf(*out) || isnan(*out) || *end)
		return -1;
	if (!*str || !strchr("0123456789.", *str))
		return -1;
	return 0;
}


/**
 * Parse gamma configuration file
 * 
 * @param   pathname  The pathname of the file
 * @return            Zero on success, -1 on error
 */
static int
parse_gamma_file(const char *restrict pathname)
{
	int fd, saved_errno;
	char *line = NULL;
	size_t size = 0, lineno = 0, ptr = 0, alloc = 0;
	ssize_t n;
	FILE *f = NULL;
	char *p, *q;
	char *r, *g, *b;
	void *new;
	size_t new_size;

	fd = open(pathname, O_RDONLY);
	if (fd < 0)
		return -1;

	f = fdopen(fd, "rb");
	if (!f)
		goto fail;

	while ((n = getline(&line, &size, f)) >= 0) {
		lineno += 1;

		if (n > 0 && line[n - 1] == '\n')
			line[n - 1] = '\0';
		for (p = line; *p == ' ' || *p == '\t'; p++);
		if (!*p || *p == '#')
			continue;

		r = strpbrk(line, " \t");
		if (!r)
			goto bad;
		for (; r[1] == ' ' || r[1] == '\t'; r++);
		g = strpbrk(r + 1, " \t");
		if (!g)
			goto bad;
		for (; g[1] == ' ' || g[1] == '\t'; g++);
		b = strpbrk(g + 1, " \t");
		if (!b)
			goto bad;
		for (; b[1] == ' ' || b[1] == '\t'; b++);

		for (;;) {
			q = strpbrk(b + 1, " \t");
			if (!q)
				break;
			for (; q[1] == ' ' || q[1] == '\t'; q++);
			if (!*q)
				break;
			r = g, g = b, b = q;
		}

		*r++ = '\0';
		*g++ = '\0';
		*b++ = '\0';

		if ((q = strpbrk(r, " \t")))
			*q = '\0';
		if ((q = strpbrk(g, " \t")))
			*q = '\0';
		if ((q = strpbrk(b, " \t")))
			*q = '\0';

		q = strchr(p, '\0');
		while ((q != p) && ((q[-1] == ' ') || (q[-1] == '\t')))
			q--;
		*q = '\0';

		if (ptr == alloc) {
			new_size = alloc ? (alloc << 1) : 4;

			new = realloc(rgammas, new_size * sizeof(*rgammas));
			if (!new)
				goto fail;
			rgammas = new;

			new = realloc(ggammas, new_size * sizeof(*ggammas));
			if (!new)
				goto fail;
			ggammas = new;

			new = realloc(bgammas, new_size * sizeof(*bgammas));
			if (!new)
				goto fail;
			bgammas = new;

			new = realloc(names, (new_size + 1) * sizeof(*names));
			if (!new)
				goto fail;
			names = new;
			memset(names + alloc, 0, (new_size + 1 - alloc) * sizeof(*names));

			alloc = new_size;
		}

		if (parse_double(rgammas + ptr, r) < 0 ||
		    parse_double(ggammas + ptr, g) < 0 ||
		    parse_double(bgammas + ptr, b) < 0)
			goto bad;
		names[ptr] = malloc(strlen(p) + 1);
		if (!names[ptr])
			goto fail;
		strcpy(names[ptr], p);
		ptr++;

		continue;
	bad:
		fprintf(stderr, "%s: ignoring malformatted line in %s: %zu\n", argv0, pathname, lineno);
	}

	if (fclose(f) < 0) {
		f = NULL;
		goto fail;
	}
	close(fd);
	free(line);
	return 0;
fail:
	saved_errno = errno;
	free(line);
	if (f)
		fclose(f);
	if (fd >= 0)
		close(fd);
	errno = saved_errno;
	return -1;
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
int
handle_args(int argc, char *argv[], char *prio)
{
	int free_fflag = 0, saved_errno;
	int q = xflag + dflag;
	if (q > 1 || (fflag && argc) || (xflag && (fflag || argc > 0 || prio)))
		usage();
	if (argc == 1) {
		if (parse_double(&rgamma, argv[0]) < 0)
			usage();
		bgamma = ggamma = rgamma;
	} else if (argc == 3) {
		if (parse_double(&rgamma, argv[0]) < 0)
			usage();
		if (parse_double(&ggamma, argv[1]) < 0)
			usage();
		if (parse_double(&bgamma, argv[2]) < 0)
			usage();
	} else if (argc) {
		usage();
	}
	if (!argc && !fflag && !xflag) {
		fflag = get_conf_file("gamma");
		if (!fflag)
			return -1;
		free_fflag = 1;
	}
	if (fflag && parse_gamma_file(fflag) < 0)
		goto fail;
	if (free_fflag) {
		free(fflag);
		fflag = NULL;
	}
	return 0;
fail:
	saved_errno = errno;
	if (free_fflag) {
		free(fflag);
		fflag = NULL;
	}
	errno = saved_errno;
	return cleanup(-1);
}


/**
 * Fill a filter
 * 
 * @param  filter  The filter to fill
 * @param  r       The red gamma
 * @param  g       The green gamma
 * @param  b       The blue gamma
 */
static void
fill_filter(libcoopgamma_filter_t *restrict filter, double r, double g, double b)
{
	switch (filter->depth) {
#define X(CONST, MEMBER, MAX, TYPE)\
		case CONST:\
			libclut_gamma(&filter->ramps.MEMBER, MAX, TYPE, r, g, b);\
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
int
start(void)
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

	if (!xflag && names && (r = make_slaves()) < 0)
		return cleanup(r);

	if (!names) {
		for (i = 0, r = 1; i < filters_n; i++) {
			if (!crtc_updates[i].master || !crtc_info[crtc_updates[i].crtc].supported)
				continue;
			if (!xflag)
				fill_filter(&crtc_updates[i].filter, rgamma, ggamma, bgamma);
			r = update_filter(i, 0);
			if (r == -2 || (r == -1 && errno != EAGAIN))
				return cleanup(r);
			if (crtc_updates[i].slaves) {
				for (j = 0; crtc_updates[i].slaves[j]; j++) {
					r = update_filter(crtc_updates[i].slaves[j], 0);
					if (r == -2 || (r == -1 && errno != EAGAIN))
						return cleanup(r);
				}
			}
		}
	} else {
		for (i = 0, r = 1; i < filters_n; i++) {
			if (!crtc_info[crtc_updates[i].crtc].supported)
				continue;
			for (j = 0; names[j]; j++) {
				if (!strcasecmp(crtc_updates[i].filter.crtc, names[j])) {
					fill_filter(&crtc_updates[i].filter, rgammas[j], ggammas[j], bgammas[j]);
					r = update_filter(i, 0);
					if (r == -2 || (r == -1 && errno != EAGAIN))
						return cleanup(r);
					break;
				}
			}
		}
	}

	while (r != 1)
		if ((r = synchronise(-1)) < 0)
			return cleanup(r);

	if (!dflag)
		return cleanup(0);

	if (libcoopgamma_set_nonblocking(&cg, 0) < 0)
		return cleanup(-1);
	for (;;) {
		if (libcoopgamma_synchronise(&cg, NULL, 0, &j) < 0) {
			switch (errno) {
			case 0:
				break;
			case ENOTRECOVERABLE:
				goto enotrecoverable;
			default:
				return cleanup(-1);
			}
		}
	}

enotrecoverable:
	pause();
	return cleanup(-1);
}
