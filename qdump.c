/* qnx_acc.c - QNX (1.2) simple filesystem extract tool
 * uses qnx_acc library
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
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#include "qnx_acc.h"

/* ops */
#define OP_DIR 1
#define OP_EXTRACT 2
#define OP_DUMP 3


/* options */
#define OPT_ASCII 1

/* "local" (file) helper */
/* write buf to file, passing of and cm to open(2) 
 * returns 0 on success */
int buf2file(void *buf, char* fn, int l, int of, int cm)
{
	int r;
	size_t rb=l;
	char *dbuf=(char *)buf;
	if(fn==NULL)
	{
		fprintf(stderr,"File name is NULL in buf2file!\n");
		return -1;
	}
	int fd=open(fn,of,cm);
	if(fd<1)
	{
		fprintf(stderr,"Unable to open or create %s\n",fn);
		return -1;
	}
	while(rb)
	{
		r=write(fd,dbuf,l);
		if(r<0)
		{
			fprintf(stderr,"Write error for %s\n",fn);
			close(fd);
			return -1;
		}

		rb-=r;
		dbuf+=r;
	}
	close(fd);
	return 0;
}

/* qnx helpers */
void disp_qdir(qnx_disk *qd,struct q_dir_entry *d)
{
	char name[18];
	char isdir=' ';
	int32_t fsize;

	fsize=qnx_filesize(qd,d);
	if(!d->fname[0]) return;	/* do not display entries with no name */
	if(d->fattr & QFA_DIRECTORY)
		isdir='+';
	name[17]=0;
	memcpy(name,d->fname,17);
	printf("%c%-17s% 12d\n",isdir,name,fsize);
/*
	printf("\tX0: %u\tXL: %u\tXN: %u\n",d->ffirst_xtnt,d->flast_xtnt,d->fnum_xtnt);
	printf("\tBC: %u\tCF: %u\n",d->fnum_blks,d->fnum_chars_free); */
}

void disp_qnxdir(qnx_file *fd)
{
	struct q_dir_entry de;
	qnx_dir_init(fd);

	while(!qnx_dir_nextentry(fd,&de))
		disp_qdir(fd->qd,&de);
}


void disp_xtnt_hdr(struct q_xtnt_header *h)
{
	printf("Extent: p=%u, n=%u, s=%u, b=%u\n",h->prev_xtnt,h->next_xtnt,h->size_xtnt,h->bound_xtnt);
}

void q_convrs(uint8_t *buf, uint32_t l)
{
	while(l--)
	{
		if(*buf==0x1e) *buf=0x0a;
		buf++;
	}
}

int32_t q_file2lbuf(qnx_file *fd, uint8_t **dbuf)
{
	uint8_t *buf;
	int32_t br;
	buf=malloc(fd->fsize+1);
	if(buf==NULL)
		func_abort("malloc error");
	if(fd->fpos)
		qnx_seek(fd,0);

	/* in the unlikely event that qnx_read doesn't read everyhting in one go
	 * it's useless to retry :) so we're happy with what it gives us */
	if((br=qnx_read(fd,buf,fd->fsize))<1)	/* unless it's an error */
		func_abort("read error");
	/* add nul-terminator for optional processing as a string */
	buf[br]=0;
	*dbuf=buf;
	return br;
}

int disp_qnxfile(qnx_file *fd, int optrs)
{
	uint8_t *buf;
	int32_t l;
	l=q_file2lbuf(fd,&buf);
	if(l<=0)
		return 1;
	if(optrs)
		q_convrs(buf,l);
	fwrite(buf,1,l,stdout);
	free(buf);
	return 0;
}

/* extract (already opened) qnx file fd to dpath
 * spath is needed because fd does not contain filename */
int extract_qnxfile(qnx_file *fd, char *spath, char *dpath, int optrs)
{
	uint8_t *buf;
	int32_t l;
	char *dfn;
	char *dempty="";
	char *fn=strrchr(spath,'/');
	size_t dpl,spl;
	int rv=0;

	if(fn==NULL)
		fn=spath;
	else
		fn++;	/* next char after '/' */
	if(fn[0]==0)	/* shouldn't happen, but better safe */
		return -1;

	if(dpath==NULL) dpath=dempty;

	/* (destination) path preparation */
	dpl=strlen(dpath);
	spl=strlen(spath);
	dfn=malloc(dpl+spl+2);
	strcpy(dfn,dpath);
	if(dpl && dpath[dpl-1]!='/')
		strcat(dfn,"/");
	strcat(dfn,fn);

	/* actual reading */
	l=q_file2lbuf(fd,&buf);
	if(l<=0)
	{
		func_msg("Unable to extract %s",spath);
		rv=-1;
		goto eofunc;
	}

	/* optional conversion */
	if(optrs)
		q_convrs(buf,l);

	/* write */
	printf("%s\n",dfn);
	rv=buf2file(buf,dfn,l,O_CREAT | O_EXCL | O_WRONLY,0644);

eofunc:
	free(dfn);
	return rv;
}

int extract_qnxdir(qnx_file *dfd, char *dpath, int optrs)
{
	qnx_file fd;
	struct q_dir_entry de;
	char *npath;
	size_t dpl;

	qnx_dir_init(dfd);

	while(!qnx_dir_nextentry(dfd,&de))
	{
		if(!de.fname[0]) continue;
		if(qnx_de2fd(dfd->qd,&de,&fd))
		{
			func_msg("unable to open qnx file %s",de.fname);
			continue;
		}
		if(fd.attrs & QFA_DIRECTORY)
		{
			/* sanity check - in case of disk image corruption */
			if(strlen((char *)de.fname)>QNX_MAXFNLEN)
			{
				func_msg("filename %s longer than expected",de.fname);
				continue;
			}

			/* create new path and directory */
			dpl=strlen(dpath);
			npath=malloc(dpl+strlen((char *)de.fname)+2);
			strcpy(npath,dpath);
			if(dpl && dpath[dpl-1]!='/')
				strcat(npath,"/");
			strcat(npath,(char *)de.fname);
			if(mkdir(npath,0755))
			{
				func_msg("can't create directory %s",npath);
				free(npath);
				continue;
			}
			
			/* process it and then free npath */
			extract_qnxdir(&fd,npath,optrs);
			free(npath);
		}
		else
		{
			extract_qnxfile(&fd,(char *)de.fname,dpath,optrs);
		}
	}

	return 0;
}


/* general */
void exit_usage(char *pn, int rv)
{
	printf("Usage: %s <disk_image> {-d|-x|-r} path [-a] [-o offset] [-l local_path]\n",pn);
	printf("\t-d\tlist directory at path (must be directory)\n");
	printf("\t-x\textract file (or directory contents, recursive) from path\n");
	printf("\t-r\tread (dump) file to stdout\n");
	printf("\t-a\tASCII file (convert RS to LF)\n");
	printf("\t-o\tOffset (in bytes) into image file (e.g. for partition)\n");
	printf("\t-l\tLocal destination for -x (file(s) extracted to local_path)\n");
	printf("\nNotes:\n\t if multiple -r/d/x options are given, only last one is used\n");
	printf("\t option -a affects all files (binary ones would be mangled!)\n");
	exit(rv);
}

int main(int argc, char *argv[])
{
	const char optstr[]="ar:d:x:o:l:";

	char *dpath=NULL;
	char *spath=NULL;
	int or,e=0;
	int op=0;
	int rv=0;

	int oflags=0;
	int ioff=0;

	qnx_disk qd;
	qnx_file qfd;

	while((or=getopt(argc,argv,optstr))!=-1)
	{
		switch(or)
		{
			case 'a':
				oflags |= OPT_ASCII;
				break;
			case 'd':
				op=OP_DIR;
				spath=optarg;
				break;
			case 'r':
				op=OP_DUMP;
				spath=optarg;
				break;
			case 'x':
				op=OP_EXTRACT;
				spath=optarg;
				break;
			case 'o':
				ioff=atoi(optarg);
				break;
			case 'l':
				dpath=optarg;
				break;
			case '?':
				e=1;
				break;
			default:
				printf("Unrecognized option\n");
				e=1;
				break;
		}
	}
	if(e || !op || optind>=argc)
		exit_usage(argv[0],EXIT_FAILURE);

	if(qd_open(&qd,argv[optind],ioff))
	{
		fprintf(stderr,"Unable to open image file %s\n",argv[optind]);
		return 1;
	}
	
	if(q_open_file(&qd,spath,&qfd))
	{
		fprintf(stderr,"Unable to open %s inside image\n",spath);
		rv=1;
		goto eofunc;
	}

	switch(op)
	{
		case OP_EXTRACT:
			if(qfd.attrs & QFA_DIRECTORY)
				extract_qnxdir(&qfd,dpath,oflags & OPT_ASCII);
			else
				extract_qnxfile(&qfd,spath,dpath,oflags & OPT_ASCII);
			break;
		case OP_DIR:
			if(qfd.attrs & QFA_DIRECTORY)
				disp_qnxdir(&qfd);
			else
				fprintf(stderr,"%s is not a directory\n",spath);
			break;
		case OP_DUMP:
			if(qfd.attrs & QFA_DIRECTORY)
			{
				fprintf(stderr,"%s is a directory\n",spath);
				rv=1;
			}
			else
				disp_qnxfile(&qfd,oflags & OPT_ASCII);
			break;
	}

eofunc:
	qd_close(&qd);
	return rv;
}
