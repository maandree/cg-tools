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

#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>



/* Note, that EDID:s are 256 hexadecimals long, and
 * a filename can only be 255 characters long. */



/**
 * Magic number for dual-byte precision lookup table based profiles
 */
#define MLUT_TAG  0x6D4C5554L

/**
 * Magic number for gamma–brightness–contrast based profiles
 * and for variable precision lookup table profiles
 */
#define VCGT_TAG  0x76636774L

/**
 * The filename of the configuration file
 */
#define ICCTAB  "icctab"



/**
 * The default filter priority for the program
 */
const int64_t default_priority = 0;

/**
 * The default class for the program
 */
char* const default_class = PKGNAME "::cg-icc::standard";



/**
 * -d: keep process alive and remove filter on death
 */
static int dflag = 0;

/**
 * -x: remove filter rather than adding it
 */
static int xflag = 0;

/**
 * The panhame of the selected ICC profile
 */
static const char* icc_pathname = NULL;

/**
 * Gamma ramps loaded from `icc_pathname`
 */
static libcoopgamma_ramps_t uniramps;

/**
 * The datatype of the stops in the ramps of `uniramps`
 */
static libcoopgamma_depth_t unidepth = 0;

/**
 * Parsed ICC profiles for each CRTC
 */
static libcoopgamma_ramps_t* rampses = NULL;

/**
 * The datatype of the stops in the ramps of
 * corresponding element in `rampses`
 */
static libcoopgamma_depth_t* depths = NULL;

/**
 * File descriptor for configuration directory
 */
static int confdirfd = -1;

/**
 * List of CRTC:s
 */
static char** crtc_icc_keys = NULL;

/**
 * List of ICC profile pathnames for corresponding
 * CRTC in `crtc_icc_keys`
 */
static char** crtc_icc_values = NULL;



/**
 * Print usage information and exit
 */
void usage(void)
{
  fprintf(stderr,
	  "Usage: %s [-M method] [-S site] [-c crtc]... [-R rule] "
	  "(-x | [-p priority] [-d] [file])\n",
	  argv0);
  exit(1);
}


/**
 * Perform cleanup so valgrind output is clean
 * 
 * @param   ret  The value to return
 * @return       `ret` is returned as is
 */
static int cleanup(int ret)
{
  int saved_errno = errno;
  size_t i;
  libcoopgamma_ramps_destroy(&uniramps);
  if (confdirfd >= 0)
    close(confdirfd);
  if (rampses != NULL)
    for (i = 0; i < crtcs_n; i++)
      libcoopgamma_ramps_destroy(rampses + i);
  free(rampses);
  free(depths);
  if (crtc_icc_keys != NULL)
    for (i = 0; crtc_icc_keys[i] != NULL; i++)
      free(crtc_icc_keys[i]);
  free(crtc_icc_keys);
  if (crtc_icc_values != NULL)
    for (i = 0; crtc_icc_values[i] != NULL; i++)
      free(crtc_icc_values[i]);
  free(crtc_icc_values);
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
  return 0;
}


/**
 * Populate `crtc_icc_keys` and `crtc_icc_value`
 * 
 * @path    fd       File descriptor for the ICC profile table
 * @path    dirname  The dirname of the ICC profile table
 * @return           Zero on success, -1 on error
 */
static int load_icc_table(int fd, const char *dirname)
{
  FILE *fp;
  ssize_t len;
  size_t lineno = 1, size = 0;
  char *p, *q, *line = NULL;
  int saved_errno;
  size_t ptr = 0, siz = 0;
  void *new;
  size_t dirname_len = strlen(dirname);
  fp = fdopen(fd, "rb");
  if (fp == NULL)
    return -1;
  for (; len = getline(&line, &size, fp), len >= 0; lineno++)
    {
      if (len && line[len - 1] == '\n')
	line[--len] = '\0';
      p = line + strspn(line, " \t");
      if (!*p || (*p == '#'))
	continue;
      q = p + strspn(p, "0123456789abcdefABCDEF");
      if ((*q != ' ' && *q != '\t'))
	{
	  fprintf(stderr, "%s: warning: line %zu is malformated in %s/%s\n",
		  argv0, lineno, dirname, ICCTAB);
	  continue;
	}
      *q = '\0';
      if ((size_t)(q - p) != 256)
	fprintf(stderr, "%s: warning: EDID on line %zu in %s/%s looks to be of wrong length: %s\n",
		argv0, lineno, dirname, ICCTAB, p);
      q++;
      q += strspn(p, " \t");
      if (!*q)
	{
	  fprintf(stderr, "%s: warning: line %zu is malformated in %s/%s\n",
		  argv0, lineno, dirname, ICCTAB);
	  continue;
	}
      if (strchr(" \t", strchr(q, '\0')[-1]))
	fprintf(stderr, "%s: warning: filename on line %zu in %s/%s ends with white space: %s\n",
		argv0, lineno, dirname, ICCTAB, q);
      if (ptr == siz)
	{
	  new = realloc(crtc_icc_keys, (siz + 5) * sizeof(*crtc_icc_keys));
	  if (new == NULL)
	    goto fail;
	  crtc_icc_keys = new;
	  new = realloc(crtc_icc_values, (siz + 5) * sizeof(*crtc_icc_values));
	  if (new == NULL)
	    goto fail;
	  crtc_icc_values = new;
	  siz += 4;
	}
      crtc_icc_values[ptr] = malloc((*q == '/' ? 1 : dirname_len + sizeof("/")) + strlen(q));
      if (crtc_icc_values[ptr] == NULL)
	goto fail;
      if (*q == '/')
	strcpy(crtc_icc_values[ptr], q);
      else
	stpcpy(stpcpy(stpcpy(crtc_icc_values[ptr], dirname), "/"), q);
      crtc_icc_keys[ptr] = malloc(strlen(p) + 1);
      if (crtc_icc_keys[ptr] == NULL)
	{
	  ptr++;
	  goto fail;
	}
      strcpy(crtc_icc_keys[ptr], p);
      ptr++;
    }
  if (ferror(fp))
    goto fail;
  if (!ptr)
    {
      crtc_icc_keys = calloc(1, sizeof(*crtc_icc_keys));
      if (crtc_icc_keys == NULL)
	goto fail;
    }
  crtc_icc_keys[ptr] = NULL;
  if (!ptr)
    {
      crtc_icc_values = calloc(1, sizeof(*crtc_icc_values));
      if (crtc_icc_values == NULL)
	goto fail;
    }
  crtc_icc_values[ptr] = NULL;
  fclose(fp);
  free(line);
  return 0;
 fail:
  saved_errno = errno;
  if (crtc_icc_keys != NULL)
    crtc_icc_keys[ptr] = NULL;
  if (crtc_icc_values != NULL)
    crtc_icc_values[ptr] = NULL;
  fclose(fp);
  free(line);
  errno = saved_errno;
  return -1;
}


/**
 * This function is called after the last
 * call to `handle_opt`
 * 
 * @param   argc    The number of unparsed arguments
 * @param   argv    `NULL` terminated list of unparsed arguments
 * @param   method  The argument associated with the "-M" option
 * @param   site    The argument associated with the "-S" option
 * @param   crtcs   The arguments associated with the "-c" options, `NULL`-terminated
 * @param   prio    The argument associated with the "-p" option
 * @param   rule    The argument associated with the "-R" option
 * @return          Zero on success, -1 on error
 */
int handle_args(int argc, char* argv[], char* method, char* site,
		char** crtcs, char* prio, char* rule)
{
  struct passwd* pw;
  char* path = NULL;
  int free_fflag = 0, saved_errno;
  int fd = -1, q = xflag + dflag;
  q += (method != NULL) &&  !strcmp(method, "?");
  q += (prio   != NULL) &&  !strcmp(prio, "?");
  q += (rule   != NULL) && (!strcmp(rule, "?") || !strcmp(rule, "??"));
  for (; *crtcs; crtcs++)
    q += !strcmp(*crtcs, "?");
  if ((q > 1) || (xflag && ((argc > 0) || (prio != NULL))) || (argc > 1))
    usage();
  icc_pathname = *argv;
  memset(&uniramps, 0, sizeof(uniramps));
  if (icc_pathname == NULL)
    {
      pw = getpwuid(getuid());
      if ((pw == NULL) || (pw->pw_dir == NULL))
	goto fail;
      
      path = malloc(strlen(pw->pw_dir) + sizeof("/.config"));
      if (path == NULL)
	goto fail;
      
      sprintf(path, "%s/.config", pw->pw_dir);
      
      if (access(path, F_OK) < 0)
	sprintf(path, "/etc");
      
      confdirfd = open(path, O_DIRECTORY);
      if (confdirfd < 0)
	goto fail;
      
      fd = openat(confdirfd, ICCTAB, O_RDONLY);
      if (fd < 0)
	goto fail;
      
      if (load_icc_table(fd, path) < 0)
	goto fail;
      
      free(path), path = NULL;
      close(fd), fd = -1;
    }
  return 0;
 fail:
  saved_errno = errno;
  free(path), path = NULL;
  if (fd >= 0)
    close(fd);
  errno = saved_errno;
  return cleanup(-1);
}


/**
 * Read an unsigned 64-bit integer
 * 
 * @param   content  The beginning of the encoded integer
 * @return           The integer, decoded
 */
static uint64_t icc_uint64(const char* restrict content)
{
  uint64_t rc;
  rc  = (uint64_t)(unsigned char)(content[0]), rc <<= 8;
  rc |= (uint64_t)(unsigned char)(content[1]), rc <<= 8;
  rc |= (uint64_t)(unsigned char)(content[2]), rc <<= 8;
  rc |= (uint64_t)(unsigned char)(content[3]), rc <<= 8;
  rc |= (uint64_t)(unsigned char)(content[4]), rc <<= 8;
  rc |= (uint64_t)(unsigned char)(content[5]), rc <<= 8;
  rc |= (uint64_t)(unsigned char)(content[6]), rc <<= 8;
  rc |= (uint64_t)(unsigned char)(content[7]);
  return rc;
}


/**
 * Read an unsigned 32-bit integer
 * 
 * @param   content  The beginning of the encoded integer
 * @return           The integer, decoded
 */
static uint32_t icc_uint32(const char* restrict content)
{
  uint32_t rc;
  rc  = (uint32_t)(unsigned char)(content[0]), rc <<= 8;
  rc |= (uint32_t)(unsigned char)(content[1]), rc <<= 8;
  rc |= (uint32_t)(unsigned char)(content[2]), rc <<= 8;
  rc |= (uint32_t)(unsigned char)(content[3]);
  return rc;
}


/**
 * Read an unsigned 16-bit integer
 * 
 * @param   content  The beginning of the encoded integer
 * @return           The integer, decoded
 */
static uint16_t icc_uint16(const char* restrict content)
{
  uint16_t rc;
  rc  = (uint16_t)(unsigned char)(content[0]), rc <<= 8;
  rc |= (uint16_t)(unsigned char)(content[1]);
  return rc;
}


/**
 * Read an unsigned 8-bit integer
 * 
 * @param   content  The beginning of the encoded integer
 * @return           The integer, decoded
 */
static uint16_t icc_uint8(const char* restrict content)
{
  return (uint8_t)(content[0]);
}


/**
 * Read a floating-point value
 * 
 * @param   content  The beginning of the encoded value
 * @param   width    The number of bytes with which the value is encoded
 * @return           The value, decoded
 */
static double icc_double(const char* restrict content, size_t width)
{
  double ret = 0;
  size_t i;
  for (i = 0; i < width; i++)
    {
      ret /= 256;
      ret += (double)(unsigned char)(content[width - 1 - i]);
    }
  ret /= 255;
  return ret;
}


/**
 * Parse an ICC profile
 * 
 * @param   content  The content of the ICC profile file
 * @param   n        The byte-size of `content`
 * @param   ramps    Output parameter for the filter stored in the ICC profile,
 *                   `.red_size`, `.green_size`, `.blue_size` should already be
 *                   set (these values can however be modified.)
 * @param   depth    Output parameter for ramps stop value type
 * @return           Zero on success, -1 on error, -2 if no usable data is
 *                   available in the profile.
 */
static int parse_icc(const char* restrict content, size_t n, libcoopgamma_ramps_t* ramps,
		     libcoopgamma_depth_t* depth)
{
  uint32_t i_tag, n_tags;
  size_t i, ptr = 0, xptr;
  
  /* Skip header */
  if (n - ptr < 128)
    return -2;
  ptr += 128;
  
  /* Get the number of tags */
  if (n - ptr < 4)
    return -2;
  n_tags = icc_uint32(content + ptr), ptr += 4;
  
  for (i_tag = 0, xptr = ptr; i_tag < n_tags; i_tag++, ptr = xptr)
    {
      uint32_t tag_name, tag_offset, tag_size, gamma_type;
      
      /* Get profile encoding type, offset to the profile and the encoding size of its data */
      if (n - ptr < 12)
	return -2;
      tag_name   = icc_uint32(content + ptr), ptr += 4;
      tag_offset = icc_uint32(content + ptr), ptr += 4;
      tag_size   = icc_uint32(content + ptr), ptr += 4;
      xptr = ptr;
      
      /* Jump to the profile data */
      if (tag_offset > INT32_MAX - tag_size)
	return -2;
      if (tag_offset + tag_size > n)
	return -2;
      ptr = tag_offset;
      
      if (tag_name == MLUT_TAG)
	{
	  /* The profile is encododed as an dual-byte precision lookup table */
	  
	  /* Initialise ramps */
	  *depth = LIBCOOPGAMMA_UINT16;
	  ramps->u16.red_size   = 256;
	  ramps->u16.green_size = 256;
	  ramps->u16.blue_size  = 256;
	  if (libcoopgamma_ramps_initialise(&(ramps->u16)) < 0)
	    return -1;
	  
	  /* Get the lookup table */
	  if (n - ptr < 3 * 256 * 2)
	    continue;
	  for (i = 0; i < 256; i++)
	    ramps->u16.red[i]   = icc_uint16(content + ptr), ptr += 2;
	  for (i = 0; i < 256; i++)
	    ramps->u16.green[i] = icc_uint16(content + ptr), ptr += 2;
	  for (i = 0; i < 256; i++)
	    ramps->u16.blue[i]  = icc_uint16(content + ptr), ptr += 2;
	  
	  return 0;
	}
      else if (tag_name == VCGT_TAG)
	{
	  /* The profile is encoded as with gamma, brightness and contrast values
	   * or as a variable precision lookup table profile */
	  
	  /* VCGT profiles starts where their magic number */
	  if (n - ptr < 4)
	    continue;
	  tag_name = icc_uint32(content + ptr), ptr += 4;
	  if (tag_name != VCGT_TAG)
	    continue;
	  
	  /* Skip four bytes */
	  if (n - ptr < 4)
	    continue;
	  ptr += 4;
	  
	  /* Get the actual encoding type */
	  if (n - ptr < 4)
	    continue;
	  gamma_type = icc_uint32(content + ptr), ptr += 4;
	  
	  if (gamma_type == 0)
	    {
	      /* The profile is encoded as a variable precision lookup table */
	      uint16_t n_channels, n_entries, entry_size;
	      
	      /* Get metadata */
	      if (n - ptr < 3 * 4)
		continue;
	      n_channels = icc_uint16(content + ptr), ptr += 2;
	      n_entries  = icc_uint16(content + ptr), ptr += 2;
	      entry_size = icc_uint16(content + ptr), ptr += 2;
	      if (tag_size == 1584)
		n_channels = 3, n_entries = 256, entry_size = 2;
	      if (n_channels != 3)
		/* Assuming sRGB, can only be an correct assumption if there are exactly three channels */
		continue;
	      
	      /* Check data availability */
	      if (n_channels > SIZE_MAX / n_entries)
		continue;
	      if (entry_size > SIZE_MAX / (n_entries * n_channels))
		continue;
	      if (n - ptr < (size_t)n_channels * (size_t)n_entries * (size_t)entry_size)
		continue;
	      
	      /* Initialise ramps */
	      ramps->u8.red_size   = (size_t)n_entries;
	      ramps->u8.green_size = (size_t)n_entries;
	      ramps->u8.blue_size  = (size_t)n_entries;
	      switch (entry_size)
		{
		case 1:
		  *depth = LIBCOOPGAMMA_UINT8;
		  if (libcoopgamma_ramps_initialise(&(ramps->u8)) < 0)
		    return -1;
		  break;
		case 2:
		  *depth = LIBCOOPGAMMA_UINT16;
		  if (libcoopgamma_ramps_initialise(&(ramps->u16)) < 0)
		    return -1;
		  break;
		case 4:
		  *depth = LIBCOOPGAMMA_UINT32;
		  if (libcoopgamma_ramps_initialise(&(ramps->u32)) < 0)
		    return -1;
		  break;
		case 8:
		  *depth = LIBCOOPGAMMA_UINT64;
		  if (libcoopgamma_ramps_initialise(&(ramps->u64)) < 0)
		    return -1;
		  break;
		default:
		  *depth = LIBCOOPGAMMA_DOUBLE;
		  if (libcoopgamma_ramps_initialise(&(ramps->d)) < 0)
		    return -1;
		  break;
		}
	      
	      /* Get the lookup table */
	      switch (*depth)
		{
		case LIBCOOPGAMMA_UINT8:
		  for (i = 0; i < ramps->u8.red_size;   i++)
		    ramps->u8.red[i]   = icc_uint8(content + ptr), ptr += 1;
		  for (i = 0; i < ramps->u8.green_size; i++)
		    ramps->u8.green[i] = icc_uint8(content + ptr), ptr += 1;
		  for (i = 0; i < ramps->u8.blue_size;  i++)
		    ramps->u8.blue[i]  = icc_uint8(content + ptr), ptr += 1;
		  break;
		case LIBCOOPGAMMA_UINT16:
		  for (i = 0; i < ramps->u16.red_size;   i++)
		    ramps->u16.red[i]   = icc_uint16(content + ptr), ptr += 2;
		  for (i = 0; i < ramps->u16.green_size; i++)
		    ramps->u16.green[i] = icc_uint16(content + ptr), ptr += 2;
		  for (i = 0; i < ramps->u16.blue_size;  i++)
		    ramps->u16.blue[i]  = icc_uint16(content + ptr), ptr += 2;
		  break;
		case LIBCOOPGAMMA_UINT32:
		  for (i = 0; i < ramps->u32.red_size;   i++)
		    ramps->u32.red[i]   = icc_uint32(content + ptr), ptr += 4;
		  for (i = 0; i < ramps->u32.green_size; i++)
		    ramps->u32.green[i] = icc_uint32(content + ptr), ptr += 4;
		  for (i = 0; i < ramps->u32.blue_size;  i++)
		    ramps->u32.blue[i]  = icc_uint32(content + ptr), ptr += 4;
		  break;
		case LIBCOOPGAMMA_UINT64:
		  for (i = 0; i < ramps->u64.red_size;   i++)
		    ramps->u64.red[i]   = icc_uint64(content + ptr), ptr += 8;
		  for (i = 0; i < ramps->u64.green_size; i++)
		    ramps->u64.green[i] = icc_uint64(content + ptr), ptr += 8;
		  for (i = 0; i < ramps->u64.blue_size;  i++)
		    ramps->u64.blue[i]  = icc_uint64(content + ptr), ptr += 8;
		  break;
		default:
		  for (i = 0; i < ramps->d.red_size;   i++)
		    ramps->d.red[i]   = icc_double(content + ptr, entry_size), ptr += entry_size;
		  for (i = 0; i < ramps->d.green_size; i++)
		    ramps->d.green[i] = icc_double(content + ptr, entry_size), ptr += entry_size;
		  for (i = 0; i < ramps->d.blue_size;  i++)
		    ramps->d.blue[i]  = icc_double(content + ptr, entry_size), ptr += entry_size;
		  break;
		}
	      
	      return 0;
	    }
	  else if (gamma_type == 1)
	    {
	      /* The profile is encoded with gamma, brightness and contrast values */
	      double r_gamma, r_min, r_max, g_gamma, g_min, g_max, b_gamma, b_min, b_max;
	      
	      /* Get the gamma, brightness and contrast */
	      if (n - ptr < 9 * 4)
		continue;
	      r_gamma = (double)icc_uint32(content + ptr) / 65536L, ptr += 4;
	      r_min   = (double)icc_uint32(content + ptr) / 65536L, ptr += 4;
	      r_max   = (double)icc_uint32(content + ptr) / 65536L, ptr += 4;
	      g_gamma = (double)icc_uint32(content + ptr) / 65536L, ptr += 4;
	      g_min   = (double)icc_uint32(content + ptr) / 65536L, ptr += 4;
	      g_max   = (double)icc_uint32(content + ptr) / 65536L, ptr += 4;
	      b_gamma = (double)icc_uint32(content + ptr) / 65536L, ptr += 4;
	      b_min   = (double)icc_uint32(content + ptr) / 65536L, ptr += 4;
	      b_max   = (double)icc_uint32(content + ptr) / 65536L, ptr += 4;
	      
	      /* Initialise ramps */
	      *depth = LIBCOOPGAMMA_DOUBLE;
	      if (libcoopgamma_ramps_initialise(&(ramps->d)) < 0)
		return -1;
	      
	      /* Set ramps */
	      libclut_start_over(&(ramps->d), (double)1, double, 1, 1, 1);
	      libclut_gamma(&(ramps->d), (double)1, double, r_gamma, g_gamma, b_gamma);
	      libclut_rgb_limits(&(ramps->d), (double)1, double, r_min, r_max, g_min, g_max, b_min, b_max);
	      
	      return 0;
	    }
	}
    }
  
  return -2;
}


/**
 * Load an ICC profile
 * 
 * @param   file   The ICC-profile file
 * @param   ramps  Output parameter for the filter stored in the ICC profile,
 *                 `.red_size`, `.green_size`, `.blue_size` should already be
 *                 set (these values can however be modified.)
 * @param   depth  Output parameter for ramps stop value type
 * @return         Zero on success, -1 on error, -2 if no usable data is
 *                 available in the profile.
 */
static int load_icc(const char* file, libcoopgamma_ramps_t* ramps, libcoopgamma_depth_t* depth)
{
  char* content = NULL;
  size_t ptr = 0, size = 0;
  ssize_t got;
  int fd = -1, r = -1, saved_errno;
  
  fd = open(file, O_RDONLY);
  if (fd < 0)
    {
      if (errno == ENOENT)
	{
	  fprintf(stderr, "%s: %s: %s\n", argv0, strerror(ENOENT), file);
	  errno = 0;
	}
      goto fail;
    }
  
  for (;;)
    {
      if (ptr == size)
	{
	  size_t new_size = size ? (size << 1) : 4098;
	  void* new = realloc(content, new_size);
	  if (new == NULL)
	    goto fail;
	  content = new;
	  size = new_size;
	}
      got = read(fd, content + ptr, size - ptr);
      if (got < 0)
	{
	  if (errno == EINTR)
	    continue;
	  goto fail;
	}
      if (got == 0)
	break;
      ptr += (size_t)got;
    }
  
  close(fd), fd = -1;
  
  r = parse_icc(content, ptr, ramps, depth);
 fail:
  saved_errno = errno;
  if (fd >= 0)
    close(fd);
  free(content);
  errno = saved_errno;
  return r;
}


/**
 * Get the pathname of the ICC profile for a CRTC
 * 
 * @param   crtc  The CRTC name
 * @return        The ICC profile file
 */
static const char* get_icc(const char* crtc)
{
  size_t i;
  for (i = 0; crtc_icc_keys[i] != NULL; i++)
    if (!strcasecmp(crtc, crtc_icc_keys[i]))
      return crtc_icc_values[i];
  return NULL;
}


/**
 * Fill a filter
 * 
 * @param  filter  The filter to fill
 * @param  ramps   The prototype filter
 * @param  depth   The prototype filter's stop datatype
 */
static void fill_filter(libcoopgamma_filter_t* filter, const libcoopgamma_ramps_t* ramps,
			libcoopgamma_depth_t depth)
{
  switch (filter->depth)
    {
#define X(CONST, MEMBER, MAX, TYPE)\
    case CONST:\
      switch (depth)\
	{\
	case LIBCOOPGAMMA_UINT8:\
	  libclut_translate(&(filter->ramps.MEMBER), MAX, TYPE, &(ramps->u8), UINT8_MAX, uint8_t);\
	  break;\
	case LIBCOOPGAMMA_UINT16:\
	  libclut_translate(&(filter->ramps.MEMBER), MAX, TYPE, &(ramps->u16), UINT16_MAX, uint16_t);\
	  break;\
	case LIBCOOPGAMMA_UINT32:\
	  libclut_translate(&(filter->ramps.MEMBER), MAX, TYPE, &(ramps->u32), UINT32_MAX, uint32_t);\
	  break;\
	case LIBCOOPGAMMA_UINT64:\
	  libclut_translate(&(filter->ramps.MEMBER), MAX, TYPE, &(ramps->u64), UINT64_MAX, uint64_t);\
	  break;\
	case LIBCOOPGAMMA_FLOAT:\
	  libclut_translate(&(filter->ramps.MEMBER), MAX, TYPE, &(ramps->f), (float)1, float);\
	  break;\
	case LIBCOOPGAMMA_DOUBLE:\
	  libclut_translate(&(filter->ramps.MEMBER), MAX, TYPE, &(ramps->d), (double)1, double);\
	  break;\
	}\
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
  const char* path;
  
  if (xflag)
    for (i = 0; i < crtcs_n; i++)
      crtc_updates[i].filter.lifespan = LIBCOOPGAMMA_REMOVE;
  else if (dflag)
    for (i = 0; i < crtcs_n; i++)
      crtc_updates[i].filter.lifespan = LIBCOOPGAMMA_UNTIL_DEATH;
  else
    for (i = 0; i < crtcs_n; i++)
      crtc_updates[i].filter.lifespan = LIBCOOPGAMMA_UNTIL_REMOVAL;
  
  if (!xflag && (icc_pathname == NULL))
    if ((r = make_slaves()) < 0)
      return cleanup(r);
  
  if (icc_pathname != NULL)
    {
      uniramps.u8.red_size = uniramps.u8.green_size = uniramps.u8.blue_size = 1;
      for (i = 0; i < crtcs_n; i++)
	{
	  if (uniramps.u8.red_size   < crtc_updates[i].filter.ramps.u8.red_size)
	    uniramps.  u8.red_size   = crtc_updates[i].filter.ramps.u8.red_size;
	  if (uniramps.u8.green_size < crtc_updates[i].filter.ramps.u8.green_size)
	    uniramps.  u8.green_size = crtc_updates[i].filter.ramps.u8.green_size;
	  if (uniramps.u8.blue_size  < crtc_updates[i].filter.ramps.u8.blue_size)
	    uniramps.  u8.blue_size  = crtc_updates[i].filter.ramps.u8.blue_size;
	}
      switch (load_icc(icc_pathname, &uniramps, &unidepth))
	{
	case 0:
	  break;
	case -1:
	  return cleanup(-1);
	case -2:
	  fprintf(stderr, "%s: unusable ICC profile: %s\n", argv0, icc_pathname);
	  return cleanup(-3);
	}
    }
  else
    {
      rampses = calloc(crtcs_n, sizeof(*rampses));
      if (rampses == NULL)
	return cleanup(-1);
      depths = malloc(crtcs_n * sizeof(*depths));
      if (depths == NULL)
	return cleanup(-1);
      for (i = 0; i < crtcs_n; i++)
	{
	  rampses[i].u8.red_size   = crtc_updates[i].filter.ramps.u8.red_size;
	  rampses[i].u8.green_size = crtc_updates[i].filter.ramps.u8.green_size;
	  rampses[i].u8.blue_size  = crtc_updates[i].filter.ramps.u8.blue_size;
	  path = get_icc(crtc_updates[i].filter.crtc);
	  if (path == NULL)
	    {
	      /* TODO remove CRTC */
	    }
	  else
	    switch (load_icc(path, rampses + i, depths + i))
	      {
	      case 0:
		break;
	      case -1:
		return cleanup(-1);
	      case -2:
		fprintf(stderr, "%s: unusable ICC profile: %s\n", argv0, path);
		return cleanup(-3);
	      }
	}
    }
  
  for (i = 0, r = 1; i < crtcs_n; i++)
    {
      if (!(crtc_updates[i].master) || !(crtc_info[i].supported))
	continue;
      if (!xflag)
	{
	  if (icc_pathname != NULL)
	    fill_filter(&(crtc_updates[i].filter), &uniramps, unidepth);
	  else
	    fill_filter(&(crtc_updates[i].filter), rampses + i, depths[i]);
	}
      r = update_filter(i, 0);
      if ((r == -2) || ((r == -1) && (errno != EAGAIN)))
	return cleanup(r);
      if (crtc_updates[i].slaves != NULL)
	for (j = 0; crtc_updates[i].slaves[j] != 0; j++)
	  {
	    r = update_filter(crtc_updates[i].slaves[j], 0);
	    if ((r == -2) || ((r == -1) && (errno != EAGAIN)))
	      return cleanup(r);
	  }
    }
  
  while (r != 1)
    if ((r = synchronise(-1)) < 0)
      return cleanup(r);
  
  if (!dflag)
    return cleanup(0);
  
  if (libcoopgamma_set_nonblocking(&cg, 0) < 0)
    return cleanup(-1);
  for (;;)
    if (libcoopgamma_synchronise(&cg, NULL, 0, &j) < 0)
      switch (errno)
	{
	case 0:
	  break;
	case ENOTRECOVERABLE:
	  goto enotrecoverable;
	default:
	  return cleanup(-1);
	}
  
 enotrecoverable:
  for (;;)
    if (pause() < 0)
      return cleanup(-1);
}

