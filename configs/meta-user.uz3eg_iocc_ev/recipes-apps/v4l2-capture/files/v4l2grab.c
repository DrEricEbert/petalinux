/*
 * Video Pipeline Evaluation Tool
 *
 * Copyright (C) 2016 Avnet, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

//#define NO_DEBUG

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <asm/types.h>
#include <linux/videodev2.h>
#include <jpeglib.h>
#include "huffman.h"


/* -----------------------------------------------------------------------------
 * Marco Definitions
 */

#ifdef DEBUG
#define debug_printf(...) printf(__VA_ARGS__)
#define debug_buf(...)
#define debug_event(...)
#else
#define debug_printf(...)
#define debug_buf(...) 
#define debug_event(...)
#endif

#define err_printf(...) printf(__VA_ARGS__)


#define CLEAR(x) memset (&(x), 0, sizeof (x))
#define ARRAY_SIZE(a)  (sizeof(a)/sizeof((a)[0]))		

/* -----------------------------------------------------------------------------
 * Type Definitions
 */ 
struct buffer {
	void 	*start;
	size_t	length;
};

enum {
	CAP_YUYV = 0,
	CAP_MJPEG
};

enum {
	STORE_YUYV = 0,
	STORE_MJPEG,
	STORE_NOTHING
};

struct cap_dev {
	int fd;
	char dev_name[64];
	char jpeg_name[64];
	int width;
	int height;
	int bpp;
	int req_cap_count;
	struct buffer *buffers;
	int n_buffers;
	int file_num;
	int cap_mode; 	// 0-YUYV, 1-MJPEG
	int store_mode; // 0-YUYV, 1-MJPEG, -1 NO STORE
	unsigned char jpeg_quality;
	
	
};


/* -----------------------------------------------------------------------------
 * Definitions
 */ 

/* -----------------------------------------------------------------------------
 * Global Variables
 */

static const char optstr [] = "d:o:w:h:c:m:s:";

/* -----------------------------------------------------------------------------
 * Functions 
 */

/*
 * Check Huffman Table Present or Not
 */ 
int is_huffman(unsigned char *buf)
{
    unsigned char *ptbuf;
    int i = 0;
    ptbuf = buf;
    while(((ptbuf[0] << 8) | ptbuf[1]) != 0xffda) {
        if(i++ > 2048)
            return 0;
        if(((ptbuf[0] << 8) | ptbuf[1]) == 0xffc4)
            return 1;
        ptbuf++;
    }
    return 0;
}

/*
 * Save JPEG Image To File
 */
int write_jpeg_file(unsigned char *buf, int size, char *jpeg_name, int file_num)
{
    unsigned char *ptdeb, *ptlimit, *ptcur = buf;
    int sizein;
	char out_file_name[64];
	char out_file_num[8];

	strcpy(out_file_name, jpeg_name);
	sprintf(out_file_num, "%d", file_num);
	strcat(out_file_name, out_file_num);
	strcat(out_file_name, ".jpg");
	FILE *outfile = fopen( out_file_name, "wb" );

	// try to open file for saving
	if (!outfile) {
		return -1;
	}
	if(!is_huffman(buf)) {
	//if(0){
		printf("HW mjpeg, no huffman\n");
        ptdeb = ptcur = buf;
        ptlimit = buf + size;
        while((((ptcur[0] << 8) | ptcur[1]) != 0xffc0) && (ptcur < ptlimit))
            ptcur++;
        if(ptcur >= ptlimit)
            goto exit;
        sizein = ptcur - ptdeb;
		fwrite(ptdeb, 1, sizein, outfile);
		fwrite(dht_data, 1, sizeof(dht_data), outfile);
		fwrite(ptcur, 1, size - sizein, outfile);
    } else {
		//printf("HW mjpeg, with huffman\n");
		fwrite(buf, 1, size, outfile);
		fflush(outfile);
    }
	
exit:		
	// close output file
	fclose(outfile);

    return 0;
}

/*
 * Image Colorspace Conversion
 */
static void YUV422toRGB888(int width, int height, unsigned char *src, unsigned char *dst)
{
  int line, column;
  unsigned char *py, *pu, *pv;
  unsigned char *tmp = dst;

  /* In this format each four bytes is two pixels. Each four bytes is two Y's, a Cb and a Cr. 
     Each Y goes to one of the pixels, and the Cb and Cr belong to both pixels. */
  py = src;
  pu = src + 1;
  pv = src + 3;

  #define CLIP(x) ( (x)>=0xFF ? 0xFF : ( (x) <= 0x00 ? 0x00 : (x) ) )

  for (line = 0; line < height; ++line) {
    for (column = 0; column < width; ++column) {
      *tmp++ = CLIP((double)*py + 1.402*((double)*pv-128.0));
      *tmp++ = CLIP((double)*py - 0.344*((double)*pu-128.0) - 0.714*((double)*pv-128.0));      
      *tmp++ = CLIP((double)*py + 1.772*((double)*pu-128.0));

      // increase py every time
      py += 2;
      // increase pu,pv every second time
      if ((column & 1)==1) {
        pu += 4;
        pv += 4;
      }
    }
  }
}

/*
 * JPEG Compression By Software
 * Note: Check TODO in this function
 */
static int jpeg_write(unsigned char* img, int width, int height, char *file_name, int file_num, unsigned int quality)
{
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	JSAMPROW row_pointer[1];
	char out_file_name[64];
	char out_file_num[8];
	
	strcpy(out_file_name, file_name);
	sprintf(out_file_num, "%d", file_num);
	strcat(out_file_name, out_file_num);
	strcat(out_file_name, ".jpg");
  
	FILE *outfile = fopen( out_file_name, "wb" );

	// try to open file for saving
	if (!outfile) {
		return -1;
	}

	// create jpeg data
	cinfo.err = jpeg_std_error( &jerr );
	jpeg_create_compress(&cinfo);
	jpeg_stdio_dest(&cinfo, outfile);

	// set image parameters
	cinfo.image_width = width;	
	cinfo.image_height = height;
	cinfo.input_components = 3; // TODO: Make it dynamic assign
	cinfo.in_color_space = JCS_RGB;

	// set jpeg compression parameters to default
	jpeg_set_defaults(&cinfo);
	// and then adjust quality setting
	jpeg_set_quality(&cinfo, quality, TRUE);

	// start compress 
	jpeg_start_compress(&cinfo, TRUE);

	// feed data
	while (cinfo.next_scanline < cinfo.image_height) {
		row_pointer[0] = &img[cinfo.next_scanline * cinfo.image_width *  cinfo.input_components];
		jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}

	// finish compression
	jpeg_finish_compress(&cinfo);

	// destroy jpeg data
	jpeg_destroy_compress(&cinfo);

	// close output file
	fclose(outfile);
	
	return 0;
}

/*
 * Save Raw Image To JPEG File
 */
int compress_write_jpeg_file(void *p, int width, int height, int bpp, char *file_name, int file_num, int quality)
{
	unsigned char* src = (unsigned char*)p;
	unsigned char* dst = malloc(width*height*bpp*sizeof(char));
	
	if(!dst){
		return -1;
	}
	
	if (!file_name) {
		return -1;
	}
	
	/// Convert from YUV422 to RGB888
	YUV422toRGB888(width, height, src, dst);

	/// Compress and write jpeg file
	jpeg_write(dst, width, height, file_name, file_num, quality);
	
	free(dst);
	return 0;
}

static void errno_exit(const char* s)
{
  fprintf(stderr, "%s error %d, %s\n", s, errno, strerror (errno));
  exit(EXIT_FAILURE);
}

static int xioctl(int fd, int request, void* argp)
{
  int r;

  do r = ioctl(fd, request, argp);
  while (-1 == r && EINTR == errno);

  return r;
}

/*
 * Process Image
 */
static void image_process(const void* p)
{
	/// TODO: Add something
}

/*
 * Read Single Frame
 */
static int frame_read(struct cap_dev *dev)
{
	struct v4l2_buffer buf;
	int ret = -1;

	CLEAR (buf);
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	if (xioctl(dev->fd, VIDIOC_DQBUF, &buf) == -1) {
		switch (errno) {
			case EAGAIN:
				goto exit0;
			case EIO:
			 // Could ignore EIO, see spec
			 // fall through
			default:
				err_printf("VIDIOC_DQBUF failed\n");
				goto exit0;
		}
	}

	assert (buf.index < dev->n_buffers);

	//printf("jpegenc buf.bytesused = %d\n", buf.bytesused);

	if(dev->store_mode != STORE_NOTHING){

		if(buf.bytesused == 0){ // Due to capture error
			printf("Empty buffer\n");
			goto no_save;
		}
	
		if(dev->cap_mode == CAP_MJPEG && dev->store_mode == STORE_MJPEG){
			write_jpeg_file(dev->buffers[buf.index].start, buf.bytesused, dev->jpeg_name, dev->file_num++);
			////////// testing
			/*
			memset(dev->buffers[buf.index].start, 0, buf.length); 
			printf("buf[%d] %p length %d\n", buf.index, dev->buffers[buf.index].start, buf.length);
			*/
			//////////
		}
		
		else if(dev->cap_mode == CAP_YUYV && dev->store_mode == STORE_MJPEG){
			compress_write_jpeg_file(dev->buffers[buf.index].start, dev->width, dev->height, dev->bpp, dev->jpeg_name, dev->file_num++, dev->jpeg_quality);
		}
		else{
			//printf("No store\n");
		}
	}
	else{
		//printf("No store\n");
	}

no_save:
	if (xioctl(dev->fd, VIDIOC_QBUF, &buf) == -1){
		err_printf("VIDIOC_QBUF failed\n");
		goto exit0;
	}	

	ret = 0;
exit0:		
	return ret;
}

/*
 *  Open device
 */
static int device_open(struct cap_dev *dev)
{
	struct stat st;
	int ret = -1;
	
	// stat file
	if (-1 == stat(dev->dev_name, &st)) {
		fprintf(stderr, "Cannot identify '%s': %d, %s\n", dev->dev_name, errno, strerror (errno));
		goto exit0;
	}

	// check if its device
	if (!S_ISCHR (st.st_mode)) {
		fprintf(stderr, "%s is no device\n", dev->dev_name);
		goto exit0;
	}

	// open device
	dev->fd = open(dev->dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);

	// check if opening was successfull
	if (-1 == dev->fd) {
		fprintf(stderr, "Cannot open '%s': %d, %s\n", dev->dev_name, errno, strerror (errno));
		goto exit0;
	}

	ret = 0;
	
exit0:	
	return ret;
}

/*
 * Close device
 */
static void device_close(struct cap_dev *dev)
{
	if (close (dev->fd) == -1){
		err_printf("Close device failed\n");
	}	
	dev->fd = -1;
}

/*
 * Map buffer
 */
#define REQ_BUF_NUM		1
#define REQ_BUF_MIN		1
static int mmap_init(struct cap_dev *dev)
{
	struct v4l2_requestbuffers req;
	int ret = -1;

	CLEAR (req);

	//req.count = 4;
	req.count = REQ_BUF_NUM;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	if (xioctl(dev->fd, VIDIOC_REQBUFS, &req) == -1) {
		if (EINVAL == errno) {
			err_printf("%s does not support memory mapping\n", dev->dev_name);
		} else {
			err_printf("VIDIOC_REQBUFS failed\n");
		}
		goto exit0;
	}

	//if (req.count < 2) {
	if(req.count < REQ_BUF_MIN) {
		err_printf("Insufficient buffer memory on %s\n", dev->dev_name);
		goto exit0;
	}

	dev->buffers = calloc(req.count, sizeof(*dev->buffers));
	if (!dev->buffers) {
		err_printf("Out of memory\n");
		goto exit0;
	}

	for (dev->n_buffers = 0; dev->n_buffers < req.count; ++dev->n_buffers) {
		struct v4l2_buffer buf;

		CLEAR (buf);
		buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory      = V4L2_MEMORY_MMAP;
		buf.index       = dev->n_buffers;
		if(xioctl(dev->fd, VIDIOC_QUERYBUF, &buf) == -1){
			err_printf("VIDIOC_QUERYBUF failed\n");
			goto exit0;
		}	

		dev->buffers[dev->n_buffers].length = buf.length;
		dev->buffers[dev->n_buffers].start = mmap (NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, dev->fd, buf.m.offset);
		if(dev->buffers[dev->n_buffers].start == MAP_FAILED){
			err_printf("mmap failed\n");
			goto exit0;
		}	
	}

	ret = 0;
	
exit0:
	return ret;
}

/*
 * Initialize device
 */
static int device_init(struct cap_dev *dev)
{
	struct v4l2_capability cap;
	struct v4l2_cropcap cropcap;
	struct v4l2_crop crop;
	struct v4l2_format fmt;
	struct v4l2_fmtdesc fmtdesc;
	unsigned int min;
	int ret = -1;
	int width = dev->width;
	int height = dev->height;

	if(xioctl(dev->fd, VIDIOC_QUERYCAP, &cap) == -1) {
		if (EINVAL == errno) {
			err_printf("%s is no V4L2 device\n",dev->dev_name);
		} else {
			err_printf("VIDIOC_QUERYCAP failed\n");
		}
		goto exit0;
	}
	else{
	/*
		printf("driver:\t\t%s\n",cap.driver);
		printf("card:\t\t%s\n",cap.card);
		printf("bus_info:\t%s\n",cap.bus_info);
		printf("version:\t%d\n",cap.version);
		printf("capabilities:\t%x\n",cap.capabilities);
	*/	
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		err_printf("%s is no video capture device\n",dev->dev_name);
		goto exit0;
	}

	// Enumerate all support format
	fmtdesc.index = 0;
	fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	while(ioctl(dev->fd,VIDIOC_ENUM_FMT, &fmtdesc) != -1){
		printf("\t%d.%s\n",fmtdesc.index+1,fmtdesc.description);
		fmtdesc.index++;
	}

	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		err_printf("%s does not support streaming i/o\n",dev->dev_name);
		goto exit0;
	}

	/* Select video input, video standard and tune here. */
	CLEAR(cropcap);
	cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if(xioctl(dev->fd, VIDIOC_CROPCAP, &cropcap) == 0) {
		crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		crop.c = cropcap.defrect; /* reset to default */

		if (xioctl(dev->fd, VIDIOC_S_CROP, &crop) == -1) {
			switch (errno) {
				case EINVAL:
				/* Cropping not supported. */
				break;
				default:
				/* Errors ignored. */
				break;
			}
		}
	} else {        
		/* Errors ignored. */
	}

	CLEAR (fmt);

	// v4l2_format
	fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width       = width; 
	fmt.fmt.pix.height      = height;
	if(dev->cap_mode == 0){
		#ifndef NO_DEBUG
		printf("YUYV mode\n");
		#endif
		//fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
		fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_SRGGB8;
	}
	else{
		printf("MJPEG mode\n");
		fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
	}
	fmt.fmt.pix.field       = V4L2_FIELD_ANY;
	fmt.fmt.pix.colorspace 	= V4L2_COLORSPACE_SRGB;
	fmt.fmt.pix.bytesperline = width * 2; // TODO: bpp should not be fixed here
	if (xioctl(dev->fd, VIDIOC_S_FMT, &fmt) == -1){
		err_printf("VIDIOC_S_FMT failed\n");
		goto exit0;
	}	

	/* Note VIDIOC_S_FMT may change width and height. */
	if (width != fmt.fmt.pix.width) {
		width = fmt.fmt.pix.width;
		fprintf(stderr,"Image width set to %i by device %s.\n",width,dev->dev_name);
	}
	if (height != fmt.fmt.pix.height) {
		height = fmt.fmt.pix.height;
		fprintf(stderr,"Image height set to %i by device %s.\n",height,dev->dev_name);
	}

	/* Buggy driver paranoia. */
	min = fmt.fmt.pix.width * 2;
	if (fmt.fmt.pix.bytesperline < min){
		fmt.fmt.pix.bytesperline = min;
	}	
	min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
	if (fmt.fmt.pix.sizeimage < min){
		fmt.fmt.pix.sizeimage = min;
	}	

	return mmap_init(dev);
	
exit0:
	return ret;
}

/*
 * Un-Initialize device
 */
static int device_uninit(struct cap_dev *dev)
{
	int i;
	int ret = -1;
	
	for (i = 0; i < dev->n_buffers; ++i){
		if (munmap (dev->buffers[i].start, dev->buffers[i].length) == -1)
			goto exit0;
	}		
	ret = 0;
	
exit0:	
	if(dev->buffers)
		free(dev->buffers);
		
	return ret;	
}

/*
 * Start capturing
 */
static int capture_start(struct cap_dev *dev)
{
	unsigned int i;
	enum v4l2_buf_type type;
	int ret = -1;

	for (i = 0; i < dev->n_buffers; ++i) {
		struct v4l2_buffer buf;

		CLEAR (buf);

		buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory      = V4L2_MEMORY_MMAP;
		buf.index       = i;
		if (xioctl(dev->fd, VIDIOC_QBUF, &buf) == -1){
			err_printf("VIDIOC_QBUF failed");
			goto exit0;
		}	
	}
				
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (xioctl(dev->fd, VIDIOC_STREAMON, &type) == -1){
		err_printf("VIDIOC_STREAMON failed\n");	
		goto exit0;
	}
	ret = 0;
	
exit0:
	return ret;
}

/*
 *  Stop capturing
 */
static int capture_stop(struct cap_dev *dev)
{
	enum v4l2_buf_type type;
	int ret = -1;

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (xioctl(dev->fd, VIDIOC_STREAMOFF, &type) == -1){
		err_printf("VIDIOC_STREAMOFF failed\n");
		goto exit0;
	}	
	ret = 0;

exit0:
	return ret;
}

/* 
 * Capture Loop
 */
static void capture_loop(struct cap_dev *dev)
{
	unsigned int count;
	unsigned int frame_count = 0;

	count = dev->req_cap_count;

	while (count-- > 0) {
		for (;;) {
			fd_set fds;
			struct timeval tv;
			int r;

			FD_ZERO(&fds);
			FD_SET(dev->fd, &fds);

			/* Timeout. */
			tv.tv_sec = 10;
			tv.tv_usec = 0;

			r = select(dev->fd + 1, &fds, NULL, NULL, &tv);

			if (-1 == r) {
				if (EINTR == errno)
				  continue;

				errno_exit("select");
			}

			if (0 == r) {
				fprintf (stderr, "select timeout\n");
				exit(EXIT_FAILURE);
			}

			frame_count++;

			//printf("frame_count %d\n", frame_count);
			
			if (frame_read(dev) == 0)
				break;
			
			/* EAGAIN - continue select loop. */
		}
	}
}

static void usage()
{
	debug_printf("Usage: Please check the code\n"); // dummy
	exit(0);
}

/* 
 * Main
 */
int main(int argc, char **argv)
{
	struct cap_dev cap_dev_inst; 
	
	unsigned int c;
	while ((c = getopt(argc, argv, optstr)) != -1) {
		switch (c) {
			case 0:
				break;

			case 'd':
				// Set capture device name
				strcpy(cap_dev_inst.dev_name, optarg);
				break;

			case 'o':
				// Set jpeg filename
				strcpy(cap_dev_inst.jpeg_name, optarg);
				break;

			case 'w':
				// Set width
				cap_dev_inst.width = atoi(optarg);
				break;

			case 'h':
				// Set height
				cap_dev_inst.height = atoi(optarg);
				break;
			
			case 'c':
				// Request capture count
				cap_dev_inst.req_cap_count = atoi(optarg);
				printf("cap count %d\n", cap_dev_inst.req_cap_count);
				break;

			case 'm':
				cap_dev_inst.cap_mode = atoi(optarg); // 0-YUYV, 1-MJPEG
				break;
				
			case 's':
				cap_dev_inst.store_mode = atoi(optarg); // 0-no store, 1-MJPEG, 2-SW MJPEG
				break;
			
			default:
				usage(stderr, argc, argv);
				exit(EXIT_FAILURE);
		}
	}
	
	cap_dev_inst.jpeg_quality = 70;
	cap_dev_inst.bpp = 3; // TODO: Dynamic assign
	
	/// Open and initialize device
	if(device_open(&cap_dev_inst) != 0){
		goto exit0;
	}
	
	if(device_init(&cap_dev_inst) != 0){
		goto exit1;
	}
	
	/// Start capturing
	if(capture_start(&cap_dev_inst) != 0){
		goto exit2;
	}

	/// Process frames
	capture_loop(&cap_dev_inst);
	
	/// Stop capturing
	if(capture_stop(&cap_dev_inst) != 0){
		goto exit2;
	}
	
exit2:	
	device_uninit(&cap_dev_inst);
exit1:	
	device_close(&cap_dev_inst);
exit0:
	return 0;
} 


