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

#ifndef XFSR_H
# define XFSR_H

#define _FILE_OFFSET_BITS 64
#define _LARGEFILE64_SOURCE

# include <libxfs.h>
# include <sys/stat.h>
# include <stdint.h>
# include <stdio.h>
# include <errno.h>
//# include <byteswap.h>

/* Note that iadr/blkadr, the inode/block "address" is not the inode/block's
physical address on disk, but rather address divided by inodesize/blocksize. */

extern xfs_sb_t g_sb;
extern int g_verbose;
extern const char *g_logfile;

#undef NDEBUG

# if 0
#  define GET16(x) (x)
#  define GET32(x) (x)
#  define GET64(x) (x)
#else
#  define GET16(x) __swab16(x)
#  define GET32(x) __swab32(x)
#  define GET64(x) __swab64(x)
#endif

#define GET64P(x) GET64( *((uint64_t*)(x)) )
#define GET32P(x) GET32( *((uint32_t*)(x)) )
#define GET16P(x) GET16( *((uint16_t*)(x)) )

//#define sanitychk(x) do { if(!(x)) { eprintf(ERR, "Sanity check failed:"); exit(90); } } while(0);
enum errtype_e {ERR, WARN, INFO};
void eprintf(enum errtype_e t, const char *fmt, ...);
//#define eprintf(t,fmt,...) eprintf(__func__ , __VA_ARGS__)

#define INO_DATA_FORK_OFFSET 0x64

static inline uint64_t ino_to_iadr(uint64_t ino)
{
	unsigned inobits = g_sb.sb_agblklog + g_sb.sb_inopblog;
	uint64_t ag = ino >> inobits;
	return (ag*GET32(g_sb.sb_agblocks) << g_sb.sb_inopblog) + (XFS_MASK64LO(inobits) & ino);
}

//FIXME possible overflow
static inline uint64_t iadr_to_ino(uint64_t iadr)
{
	unsigned inobits = g_sb.sb_agblklog + g_sb.sb_inopblog;
	uint64_t adr = iadr << g_sb.sb_inodelog;
	uint64_t blkadr = adr >> g_sb.sb_blocklog;
	uint64_t ag = blkadr / GET32(g_sb.sb_agblocks);
	uint64_t ag_adr = ag*GET32(g_sb.sb_agblocks) << g_sb.sb_blocklog;
	uint64_t r_adr = adr - ag_adr;
	uint64_t ino = (r_adr >> g_sb.sb_inodelog) | (ag << inobits);
	assert(iadr == ino_to_iadr(ino));
	return ino;
}

static inline uint64_t blkno_to_blkadr(uint64_t blkno)
{
	unsigned blkbits = g_sb.sb_agblklog;
	uint64_t ag = blkno >> blkbits;
	return GET32(g_sb.sb_agblocks)*ag + (XFS_MASK64LO(blkbits) & blkno);
}


static inline void seek_ag(FILE *fp, int ag)
{
	fseeko(fp, GET32(g_sb.sb_agblocks)*ag << g_sb.sb_blocklog, SEEK_SET);
}

static inline void seek_blkadr(FILE *fp, uint64_t blkadr)
{
	fseeko(fp, blkadr << g_sb.sb_blocklog, SEEK_SET);
}

static inline void seek_blkno(FILE *fp, uint64_t blkno)
{
	seek_blkadr(fp, blkno_to_blkadr(blkno));
}

static inline void seek_iadr(FILE *fp, uint64_t iadr)
{
	fseeko(fp, iadr << g_sb.sb_inodelog, SEEK_SET);
}

static inline void seek_ino(FILE *fp, uint64_t ino)
{
	seek_iadr(fp, ino_to_iadr(ino));
}

static inline int read_sb(FILE *fp)
{
	off_t off = ftello(fp);
	fread(&g_sb, sizeof(xfs_sb_t), 1, fp);
	fseeko(fp, off, SEEK_SET);
	if(g_sb.sb_magicnum == XFS_SB_MAGIC) return -1;
	return 0;
}

static inline int dinode_isdir(xfs_dinode_t *dinode)
{
	uint16_t mode = GET16(dinode->di_core.di_mode);
	if(!S_ISDIR(mode) || dinode->di_core.di_version != 2) return 0;
	if(dinode->di_core.di_format <1 || dinode->di_core.di_format>3) return 0;
	return dinode->di_core.di_format;
}

void dinode_di_core_print(xfs_dinode_t *dinode);
void sb_print();
int read_inode(FILE *fp, xfs_dinode_t *dinode, uint64_t iadr);

void __xfs_bmbt_get_all(__uint64_t l0, __uint64_t l1, xfs_bmbt_irec_t *s);
void xfs_bmbt_get_all(xfs_bmbt_rec_64_t *r, xfs_bmbt_irec_t *s);
void xfs_bmbt_disk_get_all(xfs_bmbt_rec_64_t *r, xfs_bmbt_irec_t *s);


#endif
