/*
 *      XFS Rescue - tools for saving files from damaged XFS partitions
 *
 *      Copyright 2008 Utkan Gungordu <salviati@freeconsole.org>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */

#include "xfsr.h"
#include <stdarg.h>
#include <string.h>

int g_verbose=ERR;
xfs_sb_t g_sb;
const char *g_logfile;
static FILE *g_logfp = NULL;
static const char *errtype_s[] = { "ERR", "WARN", "INFO" };
static const char *inode_fmt_s[] = {"dev", "local", "extents", "btree", "uuid"};

#define IN_INTERVAL(x,low,high)  ((x)>=(low) && (x)<=(high))

/* Hail thee, o patriarch Kernighan */
void eprintf(enum errtype_e t, const char *fmt, ...)
{
	if(g_verbose < t) return;
	assert(t>=ERR && t<=INFO);

	if(g_logfp == NULL) {
		if(g_logfile) {
			g_logfp = fopen(g_logfile, "w+");
			if(g_logfp != NULL) {
				fprintf(stderr, "[!!!] Failed to open %s as log file: %s\n", g_logfile, strerror(errno));
			}
		} else {
			g_logfp = stderr;
		}
	}

	fflush(stdout);
	fprintf(g_logfp, "[%4s] ", errtype_s[t]);
	va_list args;
	va_start(args,fmt);
	vfprintf(g_logfp, fmt, args);
	if(t != INFO && fmt[0] != '\0' && fmt[strlen(fmt)-1] == ':')
		fprintf(g_logfp, " %s", strerror(errno));
	fprintf(g_logfp, "\n");
}

void dinode_di_core_print(xfs_dinode_t *dinode)
{
	assert(dinode != NULL);
	eprintf(INFO, "inode core info: ");
	eprintf(INFO, "magic = 0x%x", GET16(dinode->di_core.di_magic));
	eprintf(INFO, "mode = 0%o", GET16(dinode->di_core.di_mode));
	eprintf(INFO, "version = %u", dinode->di_core.di_version);
	eprintf(INFO, "format = %u (%s)", dinode->di_core.di_format,
		IN_INTERVAL(dinode->di_core.di_format,0,4) ? inode_fmt_s[dinode->di_core.di_format] : "unknown");
	eprintf(INFO, "uid = %u", GET32(dinode->di_core.di_uid));
	eprintf(INFO, "gid = %u", GET32(dinode->di_core.di_gid));
	eprintf(INFO, "size = 0x%llx", GET64(dinode->di_core.di_size));
	eprintf(INFO, "nblocks = 0x%llx", GET64(dinode->di_core.di_nblocks));
	eprintf(INFO, "extsize = 0x%x", GET32(dinode->di_core.di_extsize));
	eprintf(INFO, "nextents = 0x%x", GET32(dinode->di_core.di_nextents));
	eprintf(INFO, "aformat = %u (%s)", dinode->di_core.di_aformat,
		IN_INTERVAL(dinode->di_core.di_aformat,0,4) ? inode_fmt_s[dinode->di_core.di_aformat] : "unknown");
}

/* Reads inode from disk into dinode, without any swap operation.
   If dinode is NULL, only inode magic will be checked.
   File position is preserved. */
int read_inode(FILE *fp, xfs_dinode_t *dinode, uint64_t iadr)
{
	off_t off = ftello(fp);
	fseeko(fp, iadr << g_sb.sb_inodelog , SEEK_SET);

	if(dinode) {
		fread(dinode, sizeof(xfs_dinode_t), 1, fp);
		if(GET16(dinode->di_core.di_magic) != XFS_DINODE_MAGIC)
			return -1;
	} else {
		uint16_t magic;
		fread(&magic,2,1,fp);
		if(GET16(magic) != XFS_DINODE_MAGIC)
			return -1;
	}
	fseeko(fp, off, SEEK_SET);
	return 0;
}

/* Following 3 functions are borrowed from xfsprogs/libxfs sources */

/*
 * Convert a compressed bmap extent record to an uncompressed form.
 * This code must be in sync with the routines xfs_bmbt_get_startoff,
 * xfs_bmbt_get_startblock, xfs_bmbt_get_blockcount and xfs_bmbt_get_state.
 */

void sb_print()
{
	eprintf(INFO, "Superblock info: ");
 	eprintf(INFO, "blocklog = %u", g_sb.sb_blocklog);
 	eprintf(INFO, "inodelog = %u", g_sb.sb_inodelog);
}

void
__xfs_bmbt_get_all(
		__uint64_t l0,
		__uint64_t l1,
		xfs_bmbt_irec_t *s)
{
	int	ext_flag;
	xfs_exntst_t st;

	ext_flag = (int)(l0 >> (64 - BMBT_EXNTFLAG_BITLEN));
	s->br_startoff = ((xfs_fileoff_t)l0 &
			   XFS_MASK64LO(64 - BMBT_EXNTFLAG_BITLEN)) >> 9;

	s->br_startblock = (xfs_fsblock_t)(((xfs_dfsbno_t)l1) >> 21);

	s->br_blockcount = (xfs_filblks_t)(l1 & XFS_MASK64LO(21));
	/* This is xfs_extent_state() in-line */
	if (ext_flag) {
		ASSERT(s->br_blockcount != 0);	/* saved for DMIG */
		st = XFS_EXT_UNWRITTEN;
	} else
		st = XFS_EXT_NORM;
	s->br_state = st;
}

void
xfs_bmbt_get_all(
	xfs_bmbt_rec_64_t *r,
	xfs_bmbt_irec_t *s)
{
	__xfs_bmbt_get_all(r->l0, r->l1, s);
}

void
xfs_bmbt_disk_get_all(
	xfs_bmbt_rec_64_t	*r,
	xfs_bmbt_irec_t *s)
{
	__uint64_t	l0, l1;

	l0 = INT_GET(r->l0, ARCH_CONVERT);
	l1 = INT_GET(r->l1, ARCH_CONVERT);

	__xfs_bmbt_get_all(l0, l1, s);
}
