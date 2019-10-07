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
const int64_t default_priority = -((int64_t)1 << 62);

/**
 * The default class for the program
 */
char default_class[] = PKGNAME "::cg-limits::standard";

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
 * -B: brightness listing file
 */
static char *Bflag = NULL;

/**
 * -C: constrat listing file
 */
static char *Cflag = NULL;

/**
 * The brightness of the red channel
 */
static double rbrightness = 0;

/**
 * The brightness of the green channel
 */
static double gbrightness = 0;

/**
 * The brightness of the blue channel
 */
static double bbrightness = 0;

/**
 * The contrast of the red channel
 */
static double rcontrast = 1;

/**
 * The contrast of the green channel
 */
static double gcontrast = 1;

/**
 * The contrast of the blue channel
 */
static double bcontrast = 1;

/**
 * `NULL`-terminated list of output names
 * listed in the brightness configuration file
 */
static char **brightness_names = NULL;

/**
 * The brightness of the red channel on monitor
 * with same index in `brightness_names`
 */
static double *rbrightnesses = NULL;

/**
 * The brightness of the green channel on monitor
 * with same index in `brightness_names`
 */
static double *gbrightnesses = NULL;

/**
 * The brightness of the blue channel on monitor
 * with same index in `brightness_names`
 */
static double *bbrightnesses = NULL;

/**
 * `NULL`-terminated list of output names
 * listed in the contrast configuration file
 */
static char **contrast_names = NULL;

/**
 * The contrast of the red channel on monitor
 * with same index in `contrast_names`
 */
static double *rcontrasts = NULL;

/**
 * The contrast of the green channel on monitor
 * with same index in `contrast_names`
 */
static double *gcontrasts = NULL;

/**
 * The contrast of the blue channel on monitor
 * with same index in `contrast_names`
 */
static double *bcontrasts = NULL;



/**
 * Print usage information and exit
 */
void
usage(void)
{
	fprintf(stderr,
	        "usage: %s [-M method] [-S site] [-c crtc]... [-R rule] (-x | [-p priority] [-d] "
	        "([-B brightness-file] [-C contrast-file] | brightness-all:contrast-all | "
	        "brightness-red:contrast-red brightness-green:contrast-green brightness-blue:contrast-blue))\n",
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
	char **p;
	if (brightness_names)
		for (p = brightness_names; *p; p++)
			free(*p);
	free(brightness_names);
	free(rbrightnesses);
	free(gbrightnesses);
	free(bbrightnesses);
	if (contrast_names)
		for (p = contrast_names; *p; p++)
			free(*p);
	free(contrast_names);
	free(rcontrasts);
	free(gcontrasts);
	free(bcontrasts);
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
		case 'B':
			if (Bflag || !(Bflag = arg))
				usage();
			return 1;
		case 'C':
			if (Cflag || !(Cflag = arg))
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
 * Parse a double encoded as a string
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
	if (errno || isinf(*out) || isnan(*out) || *end)
		return -1;
	if (!*str || !strchr("-0123456789.", *str))
		return -1;
	return 0;
}


/**
 * Parse two doubles encoded as a "%lf:%lf" string
 * 
 * @param   left   Output parameter for the first value
 * @param   right  Output parameter for the second value
 * @param   str    The string
 * @return         Zero on success, -1 if the string is invalid
 */
static int
parse_twidouble(double *restrict left, double *restrict right, const char *restrict str)
{
	char *p = strchr(str, ':');
	int r;
	if (!p)
		return -1;
	*p = '\0';
	r = -(parse_double(left, str) < 0 || parse_double(right, &p[1]) < 0);
	*p = ':';
	return r;
}


/**
 * Parse configuration file
 * 
 * @param   pathname  The pathname of the file
 * @param   names     Reference to the list of names
 * @param   rs        Reference to the list of red values
 * @param   gs        Reference to the list of green values
 * @param   bs        Reference to the list of blue values
 * @return            Zero on success, -1 on error
 */
static int
parse_conf_file(const char *restrict pathname, char ***restrict names,
                double **restrict rs, double **restrict gs, double **restrict bs)
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
			for (;q[1] == ' ' || q[1] == '\t'; q++);
			if (!*q)
				break;
			r = g, g = b, b = q;
		}

		*r++ = '\0';
		*g++ = '\0';
		*b++ = '\0';

		q = strpbrk(r, " \t");
		if (!q)
			*q = '\0';
		q = strpbrk(g, " \t");
		if (!q)
			*q = '\0';
		q = strpbrk(b, " \t");
		if (!q)
			*q = '\0';

		q = strchr(p, '\0');
		while (q != p && (q[-1] == ' ' || q[-1] == '\t'))
			q--;
		*q = '\0';

		if (ptr == alloc) {
			new_size= alloc ? (alloc << 1) : 4;

			new = realloc(*rs, new_size * sizeof(**rs));
			if (!new)
				goto fail;
			*rs = new;

			new = realloc(*gs, new_size * sizeof(**gs));
			if (!new)
				goto fail;
			*gs = new;

			new = realloc(*bs, new_size * sizeof(**bs));
			if (!new)
				goto fail;
			*bs = new;

			new = realloc(*names, (new_size + 1) * sizeof(**names));
			if (!new)
				goto fail;
			*names = new;
			memset(*names + alloc, 0, (new_size + 1 - alloc) * sizeof(**names));

			alloc = new_size;
		}

		if (parse_double((*rs) + ptr, r) < 0 ||
		    parse_double((*gs) + ptr, g) < 0 ||
		    parse_double((*bs) + ptr, b) < 0)
			goto bad;
		(*names)[ptr] = malloc(strlen(p) + 1);
		if (!(*names)[ptr])
			goto fail;
		strcpy((*names)[ptr], p);
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
 * Parse brightness configuration file
 * 
 * @param   pathname  The pathname of the file
 * @return            Zero on success, -1 on error
 */
static int
parse_brightness_file(const char *restrict pathname)
{
	return parse_conf_file(pathname, &brightness_names, &rbrightnesses, &gbrightnesses, &bbrightnesses);
}


/**
 * Parse contrast configuration file
 * 
 * @param   pathname  The pathname of the file
 * @return            Zero on success, -1 on error
 */
static int
parse_contrast_file(const char *restrict pathname)
{
	return parse_conf_file(pathname, &contrast_names, &rcontrasts, &gcontrasts, &bcontrasts);
}


/**
 * This function is called after the last
 * call to `handle_opt`
 * 
 * @param   argc  The number of unparsed arguments
 * @param   argv  `NULL` terminated list of unparsed arguments
 * @param   prio  The argument associated with the "-p" option
 * @return          Zero on success, -1 on error
 */
int
handle_args(int argc, char *argv[], char *prio)
{
	int free_Bflag = 0, free_Cflag = 0, saved_errno;
	int q = xflag + dflag;
	if (q > 1 || (xflag && (Bflag || Cflag || argc > 0 || prio)))
		usage();
	if ((Bflag || Cflag) && argc)
		usage();

	if (argc == 1) {
		if (parse_twidouble(&rbrightness, &rcontrast, argv[0]) < 0)
			usage();
		bbrightness = gbrightness = rbrightness;
		bcontrast = gcontrast = rcontrast;
	} else if (argc == 3) {
		if (parse_twidouble(&rbrightness, &rcontrast, argv[0]) < 0)
			usage();
		if (parse_twidouble(&gbrightness, &gcontrast, argv[1]) < 0)
			usage();
		if (parse_twidouble(&bbrightness, &bcontrast, argv[2]) < 0)
			usage();
	} else if (argc) {
		usage();
	}

	if (!argc && !Bflag && !xflag) {
		Bflag = get_conf_file("brightness");
		if (!Bflag)
			return -1;
		free_Bflag = 1;
	}
	if (Bflag && parse_brightness_file(Bflag) < 0)
			goto fail;
	if (free_Bflag) {
		free(Bflag);
		Bflag = NULL;
	}

	if (!argc && !Cflag && !xflag) {
		Cflag = get_conf_file("contrast");
		if (!Cflag)
			return -1;
		free_Cflag = 1;
	}
	if (Cflag && parse_contrast_file(Cflag) < 0)
		goto fail;
	if (free_Cflag) {
		free(Cflag);
		Cflag = NULL;
	}

	return 0;
fail:
	saved_errno = errno;
	if (free_Bflag) {
		free(Bflag);
		Bflag = NULL;
	}
	if (free_Cflag) {
		free(Cflag);
		Cflag = NULL;
	}
	errno = saved_errno;
	return cleanup(-1);
}


/**
 * Fill a filter
 * 
 * @param   filter  The filter to fill
 * @param   rb      The red brightness
 * @param   rc      The red contrast
 * @param   gb      The green brightness
 * @param   gc      The green contrast
 * @param   bb      The blue brightness
 * @param   bc      The blue contrast
 * @return          Zero on success, -1 on error
 */
static int
fill_filter(libcoopgamma_filter_t *restrict filter, double rb, double rc, double gb, double gc, double bb, double bc)
{
	union libcoopgamma_ramps dramps;
	size_t size;
  
	if (filter->depth == LIBCOOPGAMMA_DOUBLE) {
		libclut_rgb_limits(&filter->ramps.d, (double)1, double, rb, rc, gb, gc, bb, bc);
		libclut_clip(&filter->ramps.d, (double)1, double, 1, 1, 1);
		return 0;
	}
	if (filter->depth == LIBCOOPGAMMA_FLOAT) {
		libclut_rgb_limits(&filter->ramps.f, (float)1, float, rb, rc, gb, gc, bb, bc);
		libclut_clip(&filter->ramps.f, (float)1, float, 1, 1, 1);
		return 0;
	}

	size  = dramps.d.red_size   = filter->ramps.d.red_size;
	size += dramps.d.green_size = filter->ramps.d.green_size;
	size += dramps.d.blue_size  = filter->ramps.d.blue_size;
	dramps.d.red = calloc(size, sizeof(double));
	if (!dramps.d.red)
		return -1;
	dramps.d.green = dramps.d.red   + dramps.d.red_size;
	dramps.d.blue  = dramps.d.green + dramps.d.green_size;

	libclut_start_over(&dramps.d, (double)1, double, 1, 1, 1);
	libclut_rgb_limits(&dramps.d, (double)1, double, rb, rc, gb, gc, bb, bc);
	libclut_clip(&dramps.d, (double)1, double, 1, 1, 1);

	switch (filter->depth) {
	case LIBCOOPGAMMA_UINT8:
		libclut_translate(&filter->ramps.u8, UINT8_MAX, uint8_t, &dramps.d, (double)1, double);
		break;
	case LIBCOOPGAMMA_UINT16:
		libclut_translate(&filter->ramps.u16, UINT16_MAX, uint16_t, &dramps.d, (double)1, double);
		break;
	case LIBCOOPGAMMA_UINT32:
		libclut_translate(&filter->ramps.u32, UINT32_MAX, uint32_t, &dramps.d, (double)1, double);
		break;
	case LIBCOOPGAMMA_UINT64:
		libclut_translate(&filter->ramps.u64, UINT64_MAX, uint64_t, &dramps.d, (double)1, double);
		break;
	case LIBCOOPGAMMA_FLOAT:
	case LIBCOOPGAMMA_DOUBLE:
	default:
		abort();
	}

	free(dramps.d.red);
	return 0;
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
	size_t i, j, k;
	char *empty = NULL;
	char **bnames, **cnames;
	double rb, gb, bb, rc, bc, gc;

	if (xflag)
		for (i = 0; i < filters_n; i++)
			crtc_updates[i].filter.lifespan = LIBCOOPGAMMA_REMOVE;
	else if (dflag)
		for (i = 0; i < filters_n; i++)
			crtc_updates[i].filter.lifespan = LIBCOOPGAMMA_UNTIL_DEATH;
	else
		for (i = 0; i < filters_n; i++)
			crtc_updates[i].filter.lifespan = LIBCOOPGAMMA_UNTIL_REMOVAL;

	if (!xflag && (brightness_names || contrast_names))
		if ((r = make_slaves()) < 0)
			return cleanup(r);

	if (!brightness_names && !contrast_names) {
		for (i = 0, r = 1; i < filters_n; i++) {
			if (!crtc_updates[i].master || !crtc_info[crtc_updates[i].crtc].supported)
				continue;
			if (!xflag)
				if ((r = fill_filter(&crtc_updates[i].filter, rbrightness, rcontrast,
				                     gbrightness, gcontrast, bbrightness, bcontrast)) < 0)
					return cleanup(r);
			r = update_filter(i, 0);
			if (r == -2 || (r == -1 && errno != EAGAIN))
				return cleanup(r);
			if (!crtc_updates[i].slaves) {
				for (j = 0; crtc_updates[i].slaves[j]; j++) {
					r = update_filter(crtc_updates[i].slaves[j], 0);
					if (r == -2 || (r == -1 && errno != EAGAIN))
						return cleanup(r);
				}
			}
		}
	} else {
		bnames = brightness_names ? brightness_names : &empty;
		cnames = contrast_names ? contrast_names : &empty;
		for (i = 0, r = 1; i < filters_n; i++) {
			if (!crtc_info[crtc_updates[i].crtc].supported)
				continue;
			for (j = 0; bnames[j]; j++)
				if (!strcasecmp(crtc_updates[i].filter.crtc, bnames[j]))
					break;
			for (k = 0; cnames[k]; k++)
				if (!strcasecmp(crtc_updates[i].filter.crtc, cnames[k]))
					break;
			if (bnames[j] || cnames[k]) {
				rb = gb = bb = 0;
				rc = bc = gc = 1;
				if (bnames[j]) {
					rb = rbrightnesses[j];
					gb = gbrightnesses[j];
					bb = bbrightnesses[j];
				}
				if (cnames[j]) {
					rc = rcontrasts[j];
					gc = gcontrasts[j];
					bc = bcontrasts[j];
				}
				if ((r = fill_filter(&crtc_updates[i].filter, rb, rc, gb, gc, bb, bc)) < 0)
					return cleanup(r);
				r = update_filter(i, 0);
				if (r == -2 || (r == -1 && errno != EAGAIN))
					return cleanup(r);
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
