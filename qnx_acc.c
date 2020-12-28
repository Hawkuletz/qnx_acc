/* qnx_acc.c - QNX (1.2) filesystem access functions
 *
 * Copyright 2020 Mihai Gaitos, mihaig@hawk.ro
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and its documentation for any purpose, provided that the
 * above copyright notice and the following two paragraphs appear in
 * all copies of this software.
 *
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN
 * IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 */

#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "qnx_acc.h"


/* disk image access */
int qd_close(qnx_disk *qd)
{
	close(qd->fd);
	qd->fd = -1;
	return 0;
}

int qd_open(qnx_disk *qd, char *path, uint32_t ioff)
{
	struct stat s;
	qd->fd=open(path,O_RDONLY);
	if(qd->fd == -1)
		func_abort("%s open error",path);
	if(fstat(qd->fd,&s) == -1)
	{
		qd_close(qd);
		func_abort("fstat error on %s",path);
	}
	if(s.st_size > 0x7fffffff)	/* we have to draw the line somewhere, 2GB should be enough.. :) */
	{
		qd_close(qd);
		func_abort("image too big (%lu bytes)",(unsigned long)s.st_size);
	}
	qd->isize=s.st_size;
	qd->ioff=ioff;
	return 0;
}

/* read sector from disk. Even though this function is mostly called from
 * qd_read, it's better to have this separation for the unlikely scenario of
 * using a block device or an IMD file */
int qd_read_sector(qnx_disk *qd, uint32_t sn, void *buf)
{
	uint8_t *dbuf=(uint8_t *)buf;
	int br=Q_BLOCKSIZE;
	int roff=qd->ioff+Q_BLOCKSIZE*sn;
	int rr;
	if(roff+Q_BLOCKSIZE > qd->isize)
		func_abort("Trying to read beyond end of image (sector %u, offset %u)",sn,roff);
	if(lseek(qd->fd,roff,SEEK_SET)!=roff)
		func_abort("Seek error (sector %u, offset %u)\n",sn,roff);

	while(br)
	{
		rr=read(qd->fd,dbuf,br);
		if(rr<=0)
			func_abort("Error reading from image file (sector %u, offset %u)\n",sn,roff);
		br-=rr;
		dbuf+=rr;
	}
	return 0;
}

/* absolute read from disk image */
int32_t qd_read(qnx_disk *qd, void *buf, uint32_t offset, uint32_t count)
{
	uint8_t *dbuf=(uint8_t *)buf;
	uint32_t br=count;
	uint32_t sn;
	uint32_t rs;	/* read size */
	uint32_t soff;	/* offset in sector - only for first read */

	while(br)
	{
		/* this could be optimized to bit operations */
		sn=offset/Q_BLOCKSIZE;
		soff=offset%Q_BLOCKSIZE;
		rs=MIN(Q_BLOCKSIZE-soff,br);
		if(qd_read_sector(qd,sn,qd->sbuf))
			return -1;
		memcpy(dbuf,qd->sbuf+soff,rs);
		dbuf+=rs;
		br-=rs;
		offset+=rs;
	}
	return 0;
}

/* read count bytes of data from extent starting at block bn
 * start reading at offset bytes into extent */
/* return bytes read or -1 on failure */
int32_t qnx_read_xtnt_data(qnx_disk *qd,uint32_t bn,uint32_t offset,void *buf,uint32_t count)
{
	struct q_xtnt_header h;

	if(qnx_read_xh(qd,bn,&h))
		func_abort("unable to read extent %u",bn);

	if(offset > h.size_xtnt)
		func_abort("offset %u outside extent %u",offset,bn);

	/* limit read to what fits into extent */
	if(offset+count > h.size_xtnt)
		count = h.size_xtnt - offset;

	/* just in case! */
	if(!count) return 0;

	/* qnx blocks are 1-based, so absolute offset (in bytes) on disk is:
	 * (bn-1) * BLOCKSIZE + header_size + offset */

	if(qd_read(qd,buf,(bn-1)*Q_BLOCKSIZE+sizeof(struct q_xtnt_header)+offset,count))
		return -1;

	return count;
}

/* read extent header at block number bn into h */
int qnx_read_xh(qnx_disk *qd,uint32_t bn,struct q_xtnt_header *h)
{
	uint32_t bpos;

	if(!bn) return -1;	/* block numbers are 1-based */

	bpos=(bn-1)*Q_BLOCKSIZE;
	return qd_read(qd,h,bpos,sizeof(struct q_xtnt_header));
}

/* helper function: sets fd fields to xtnt at bn */
int qnx_set_fd_xtnt(qnx_file *fd, uint32_t bn)
{
	struct q_xtnt_header h;
	if(qnx_read_xh(fd->qd,bn,&h)) return 1;
	fd->crtx = bn;
	fd->nxtx = h.next_xtnt;
	fd->prvx = h.prev_xtnt;
	fd->xsize = h.size_xtnt;
	return 0;
}


/* advance fd so that xpos is *inside* extent (except at EOF) */
int qnx_advance_xtnt(qnx_file *fd)
{
	/* for seek operations multiple extents might be loaded
	 * their lengths substracted from xpos at each step */
	while(fd->xpos >= fd->xsize)
	{
		if(!fd->nxtx)	/* don't try to go beyond last extent */
			break;

		fd->xpos -= fd->xsize;
		if(qnx_set_fd_xtnt(fd,fd->nxtx))
		{
			fd->iflags |= QIF_ERR;
			func_abort("Can't read extent %d",fd->nxtx);
		}
	}

	if(fd->fpos > fd->fsize)
	{
		fd->fpos = fd->fsize;
		fd->iflags |= QIF_ATEOF;
	}
	else if(fd->fpos == fd->fsize)
		fd->iflags |= QIF_ATEOF;

	return 0;
}

int32_t qnx_seek(qnx_file *fd, int32_t offset)
{
	if(fd->iflags & QIF_ERR)
		func_abort("internal file state bad");
	if(qnx_set_fd_xtnt(fd,fd->firstx))
	{
		fd->iflags |= QIF_ERR;
		func_abort("error reading first xtnt (%u)\n",fd->firstx);
	}
	fd->iflags &= ~QIF_ATEOF;
	fd->xpos=offset;
	fd->fpos=offset;
	if(qnx_advance_xtnt(fd))
		return -1;
	else
		return fd->fpos;
}


int32_t qnx_read(qnx_file *fd, void *buf, uint32_t count)
{
	uint8_t *dbuf=(uint8_t *) buf;
	uint32_t rb;	/* remaining bytes */
	int32_t rr;		/* read result */

	if(fd->iflags & QIF_ERR)
		return -1;
	if(fd->iflags & QIF_ATEOF)
		return 0;

	/* truncate count to file size */
	if(count > (fd->fsize - fd->fpos))
		count=fd->fsize - fd->fpos;

	rb=count;
	do
	{
		rr=qnx_read_xtnt_data(fd->qd,fd->crtx,fd->xpos,dbuf,rb);
		if(rr<0)	/* something went wrong */
		{
			if(rb!=count)	/* did we read anything? */
				return count-rb;
			else
				return -1;
		}
		dbuf+=rr;
		fd->xpos+=rr;
		fd->fpos+=rr;
		rb-=rr;
		qnx_advance_xtnt(fd);
		if(fd->iflags & QIF_ATEOF) break;
	} while (rb);
	if(rb!=0)	/* shouldn't happen, but just in case */
		fprintf(stderr,"Read finished early, rb=%u, xpos=%u, xsize=%u, nx=%u\n",rb,fd->xpos,fd->xsize,fd->nxtx);
	return count-rb;
}

/* calculate file size */
int32_t qnx_filesize(qnx_disk *qd, struct q_dir_entry *de)
{
	int32_t l=0;
	uint32_t cbn;
	struct q_xtnt_header h;

	cbn=de->ffirst_xtnt;
	
	while(cbn)
	{
		if(qnx_read_xh(qd,cbn,&h))
			func_abort("Can't read extent %u",cbn);
		cbn=h.next_xtnt;
		l+=h.size_xtnt;
	}
	return l;
}

/* open helper */
int qnx_de2fd(qnx_disk *qd,struct q_dir_entry *de,qnx_file *fd)
{
	struct q_xtnt_header h;
	memset(fd,0,sizeof(qnx_file));

	

	fd->qd=qd;
	fd->attrs=de->fattr;
	/* commented fd->fsize line below would probably work on QNX 2.x */
/*	fd->fsize=(1+de->fnum_blks)*Q_BLOCKSIZE - de->fnum_chars_free; */
	if(qnx_read_xh(qd,de->ffirst_xtnt,&h))
	{
		func_abort("failed to read extent");
	}
	fd->firstx = de->ffirst_xtnt;
	fd->crtx = de->ffirst_xtnt;
	fd->nxtx = h.next_xtnt;
	fd->xsize = h.size_xtnt;
	fd->fsize = qnx_filesize(qd,de);	/* TODO: check error status */
	return 0;
}

int qnx_open_root(qnx_disk *qd, qnx_file *fd)
{
	struct q_block1 sb;
	qd_read(qd,&sb,0,sizeof(struct q_block1));	/* TODO: check error status */
	return qnx_de2fd(qd,&sb.root_dir,fd);
}

int qnx_dir_init(qnx_file *fd)
{
	return !(qnx_seek(fd,sizeof(struct q_dir_cont))==sizeof(struct q_dir_cont));
}

int qnx_dir_nextentry(qnx_file *fd, struct q_dir_entry *d)
{
	if(qnx_read(fd,d,sizeof(struct q_dir_entry))==sizeof(struct q_dir_entry))
		return 0;
	return 1;
}

/* search directory */
int qnx_search_dir(qnx_file *fd, char *name, struct q_dir_entry *dde)
{
	struct q_dir_entry de;

	/* don't do anything if not a directory or if internal flag QIF_ERR is SET */
	if(!(fd->attrs & QFA_DIRECTORY) || (fd->iflags & (QIF_ERR|QIF_ATEOF)))
		return -1;

	qnx_dir_init(fd);
	while(qnx_dir_nextentry(fd,&de)==0)
	{
		if(strncmp((char *)de.fname,name,sizeof(de.fname))==0)
		{
			memcpy(dde,&de,sizeof(struct q_dir_entry));
			return 0;
		}
	}
	return 1;
}

/* open file from path */
int q_open_file(qnx_disk *qd, char *path, qnx_file *fd)
{
	char *tp;
	char *crtt;
	int r=-1;
	struct q_dir_entry de;
	qnx_file tfd;

	tp=strdup(path);
	if(tp==NULL)
		func_abort("alloc error!");

	if(qnx_open_root(qd,&tfd))
	{
		func_msg("Error opening root directory");
		goto eofunc;
	}
	
	crtt=strtok(tp,"/");

	while(crtt!=NULL)
	{
		if(qnx_search_dir(&tfd,crtt,&de))
		{
			func_msg("Path component %s not found\n",crtt);
			goto eofunc;
		}
		if(qnx_de2fd(qd,&de,&tfd))
		{
			fprintf(stderr,"Unable to open path component %s\n",crtt);
			goto eofunc;
		}
		crtt=strtok(NULL,"/");
		if(crtt!=NULL && !(tfd.attrs & QFA_DIRECTORY))
		{
			fprintf(stderr,"%s: not a directory\n",de.fname);
			goto eofunc;
		}
	}
	memcpy(fd,&tfd,sizeof(qnx_file));
	r=0;

eofunc:
	free(tp);
	return r;
}
