/*
 * fallocate - utility to use the fallocate system call
 *
 * Copyright (C) 2008-2009 Red Hat, Inc. All rights reserved.
 * Written by Eric Sandeen <sandeen@redhat.com>
 *            Karel Zak <kzak@redhat.com>
 *
 * cvtnum routine taken from xfsprogs,
 * Copyright (c) 2003-2005 Silicon Graphics, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <sys/stat.h>
#include <sys/types.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <err.h>
#include <limits.h>

#ifndef HAVE_FALLOCATE
# include <sys/syscall.h>
#endif

#include "falloc.h"	/* for FALLOC_FL_* flags */

#include "nls.h"


static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fprintf(out, _("Usage: %s [options] <filename>\n\nOptions:\n"),
			program_invocation_short_name);

	fprintf(out, _(
		" -h, --help          this help\n"
		" -n, --keep-size     don't modify the length of the file\n"
		" -o, --offset <num>  offset of the allocation, in bytes\n"
		" -l, --length <num>  length of the allocation, in bytes\n"));

	fprintf(out, _("\nFor more information see fallocate(1).\n"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

#define EXABYTES(x)     ((x) << 60)
#define PETABYTES(x)    ((x) << 50)
#define TERABYTES(x)    ((x) << 40)
#define GIGABYTES(x)    ((x) << 30)
#define MEGABYTES(x)    ((x) << 20)
#define KILOBYTES(x)    ((x) << 10)

static loff_t cvtnum(char *s)
{
	loff_t	i;
	char	*sp;

	errno = 0;
	i = strtoll(s, &sp, 0);

	if ((errno == ERANGE && (i == LLONG_MAX || i == LLONG_MIN)) ||
	    (errno != 0 && i == 0))
		return -1LL;
	if (i == 0 && sp == s)
		return -1LL;
	if (*sp == '\0')
		return i;
	if (sp[1] != '\0')
		return -1LL;

	switch (tolower(*sp)) {
	case 'k':
		return KILOBYTES(i);
	case 'm':
		return MEGABYTES(i);
	case 'g':
		return GIGABYTES(i);
	case 't':
		return TERABYTES(i);
	case 'p':
		return PETABYTES(i);
	case 'e':
		return EXABYTES(i);
	}

	return -1LL;
}

int main(int argc, char **argv)
{
	char	*fname;
	int	c;
	int	error;
	int	fd;
	int	mode = 0;
	loff_t	length = -2LL;
	loff_t	offset = 0;

	struct option longopts[] = {
	    { "help",      0, 0, 'h' },
	    { "keep-size", 0, 0, 'n' },
	    { "offset",    1, 0, 'o' },
	    { "lenght",    1, 0, 'l' },
	    { NULL,        0, 0, 0 }
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	while ((c = getopt_long(argc, argv, "hnl:o:", longopts, NULL)) != -1) {
		switch(c) {
		case 'h':
			usage(stdout);
			break;
		case 'n':
			mode |= FALLOC_FL_KEEP_SIZE;
			break;
		case 'l':
			length = cvtnum(optarg);
			break;
		case 'o':
			offset = cvtnum(optarg);
			break;
		default:
			usage(stderr);
			break;
		}
	}

	if (length == -2LL)
		errx(EXIT_FAILURE, _("no length argument specified"));
	if (length <= 0)
		errx(EXIT_FAILURE, _("invalid length value specified"));
	if (offset < 0)
		errx(EXIT_FAILURE, _("invalid offset value specified"));
	if (optind == argc)
		errx(EXIT_FAILURE, _("no filename specified."));

	fname = argv[optind++];

	fd = open(fname, O_WRONLY|O_CREAT, 0644);
	if (fd < 0)
		err(EXIT_FAILURE, _("%s: open failed"), fname);

#ifdef HAVE_FALLOCATE
	error = fallocate(fd, mode, offset, length);
#else
	error = syscall(SYS_fallocate, fd, mode, offset, length);
#endif
	/*
	 * EOPNOTSUPP: The FALLOC_FL_KEEP_SIZE is unsupported
	 * ENOSYS: The filesystem does not support sys_fallocate
	 */
	if (error < 0) {
		if ((mode & FALLOC_FL_KEEP_SIZE) && errno == EOPNOTSUPP)
			errx(EXIT_FAILURE,
				_("keep size mode (-n option) unsupported"));
		err(EXIT_FAILURE, _("%s: fallocate failed"), fname);
	}

	close(fd);
	return EXIT_SUCCESS;
}
