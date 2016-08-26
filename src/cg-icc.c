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

#include <stdio.h>
#include <stdlib.h>



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
 * The default filter priority for the program
 */
const int64_t default_priority = 0;

/**
 * The default class for the program
 */
char* const default_class = PKGNAME "::cg-icc::standard";



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
  int free_fflag = 0, saved_errno;
  int q = xflag + dflag;
  q += (method != NULL) &&  !strcmp(method, "?");
  q += (prio   != NULL) &&  !strcmp(prio, "?");
  q += (rule   != NULL) && (!strcmp(rule, "?") || !strcmp(rule, "??"));
  for (; *crtcs; crtcs++)
    q += !strcmp(*crtcs, "?");
  if ((q > 1) || (xflag && ((argc > 0) || (prio != NULL))))
    usage();
  /* TODO */
  return 0;
 fail:
  saved_errno = errno;
  if (free_fflag)
    free(fflag), fflag = NULL;
  errno = saved_errno;
  return cleanup(-1);
}


uint64_t icc_uint64(const char* restrict content)
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


uint32_t icc_uint32(const char* restrict content)
{
  uint32_t rc;
  rc  = (uint32_t)(unsigned char)(content[0]), rc <<= 8;
  rc |= (uint32_t)(unsigned char)(content[1]), rc <<= 8;
  rc |= (uint32_t)(unsigned char)(content[2]), rc <<= 8;
  rc |= (uint32_t)(unsigned char)(content[3]);
  return rc;
}


uint16_t icc_uint16(const char* restrict content)
{
  uint16_t rc;
  rc  = (uint16_t)(unsigned char)(content[0]), rc <<= 8;
  rc |= (uint16_t)(unsigned char)(content[1]);
  return rc;
}


uint16_t icc_uint8(const char* restrict content)
{
  return (uint8_t)(content[0])
}


double icc_double(const char* restrict content, size_t width)
{
  double ret = 0;
  size_t i;
  for (i = 0; i < width; i++)
    {
      ret /= 256;
      ret += (double)(unsigned char)(content[width - 1 - i]);
    }
  ret /= 255;
  return ret
}


int parse_icc(const char* restrict content, size_t n, libcoopgamma_ramps_t* ramps, signed* depth)
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
	  if (tag_name == VCGT_TAG)
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
	      n_channels = icc_uint32(content + ptr), ptr += 4;
	      n_entries  = icc_uint32(content + ptr), ptr += 4;
	      entry_size = icc_uint32(content + ptr), ptr += 4;
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

