/*-
 * Copyright (c) 2022 Alexandre Joannou
 * All rights reserved.
 *
 * This material is based upon work supported by the DoD Information Analysis
 * Center Program Management Office (DoD IAC PMO), sponsored by the Defense
 * Technical Information Center (DTIC) under Contract No. FA807518D0004.  Any
 * opinions, findings and conclusions or recommendations expressed in this
 * material are those of the author(s) and do not necessarily reflect the views
 * of the Air Force Installation Contracting Agency (AFICA).
 *
 * @BERI_LICENSE_HEADER_START@
 *
 * Licensed to BERI Open Systems C.I.C. (BERI) under one or more contributor
 * license agreements.  See the NOTICE file distributed with this work for
 * additional information regarding copyright ownership.  BERI licenses this
 * file to you under the BERI Hardware-Software License, Version 1.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at:
 *
 *   http://www.beri-open-systems.org/legal/license-1-0.txt
 *
 * Unless required by applicable law or agreed to in writing, Work distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations under the License.
 *
 * @BERI_LICENSE_HEADER_END@
 */

#include "BlueUnixFifo.h"

#include <fcntl.h>
//#include <poll.h>
#include <libgen.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <dirent.h>

// local helpers
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// create path
//////////////
int create_dir_path (const char* path, mode_t mode) {
  int res = -1; // initialise return status to error
  DIR* dir = opendir (path);
  int errsv = errno;
  if (dir) { // check if the dir path is already existing, and end recursion
    closedir (dir);
    res = 0; // no error encountered, success
  } else if (ENOENT == errsv) { // otherwise, create it and its parent
    // copy path for string manipulation
    size_t len = strlen (path) + 1;
    char* parentpath = (char*) malloc (len * sizeof (char));
    char* basepath = (char*) malloc (len * sizeof (char));
    strcpy (parentpath, path);
    strcpy (basepath, path);
    // try to create the parent hierarchy
    res = create_dir_path (dirname (parentpath), mode | 0111);
    // if it succeeded, try to create the directory itself
    if (res == 0) res = mkdir (basename (basepath), mode | 0111);
  }
  // return error status
  return res;
}

// create a blue unix fifo descriptor
/////////////////////////////////////
fifo_desc_t* create_fifo_desc ( char* pathname
                              , mode_t mode
                              , int flags
                              , size_t element_byte_size
                              , encoder_t encoder
                              , decoder_t decoder ) {
  fifo_desc_t* desc = (fifo_desc_t*) malloc (sizeof (fifo_desc_t));
  // copy pathname
  size_t n = strlen (pathname);
  desc->pathname = (char*) malloc (n + 1);
  memcpy (desc->pathname, pathname, n + 1);
  // file descriptor, initially set to -1
  desc->fd = -1;
  // the desired mode for file creation (unix permissions rwx.rwx.rwx)
  desc->mode = mode;
  // flags to open the fifo with (O_RDONLY, O_WRONLY, O_RDWR, O_NONBLOCK...)
  desc->flags = flags;
  // the size of the element being exchanged in bytes (for types with bitwidth
  // non multiple of 8, this size should be rounded up)
  desc->element_byte_size = element_byte_size;
  // element buffer
  desc->buf = (uint8_t*) malloc (element_byte_size);
  // running byte count to account for currently read/written bytes, initially
  // set to 0
  desc->byte_count = 0;
  // an encoder callback, called by blueUnixFifo_Produce to transform the rich
  // typed value being sent into a byte array
  desc->encoder = encoder;
  // an decoder callback, called by blueUnixFifo_Consume to transform the byte
  // array being received into a rich typed value
  desc->decoder = decoder;
  // return the initialized blue unix fifo descriptor
  return desc;
}

// destroy a blue unix fifo descriptor (free dynamically allocated memory)
//////////////////////////////////////////////////////////////////////////
void destroy_fifo_desc (fifo_desc_t* desc) {
  free (desc->pathname);
  free (desc->buf);
  free (desc);
}

// print a blue unix fifo descriptor (debug)
////////////////////////////////////////////
void print_fifo_desc (fifo_desc_t* desc) {
  printf ( "fifo_desc_t @ %p:\n"
           ".pathname: %s\n"
           ".fd: %d\n"
           ".mode: 0%o\n"
           ".flags: 0x%0x\n"
           ".element_byte_size: %lu\n"
           ".buf: %p\n"
           ".byte_count: %ld\n"
           ".encoder@: %p\n"
           ".decoder@: %p\n"
         , desc
         , desc->pathname
         , desc->fd
         , desc->mode
         , desc->flags
         , desc->element_byte_size
         , desc->buf
         , desc->byte_count
         , desc->encoder
         , desc->decoder );
}

// open file descriptor associated with a blue unix fifo descriptor
///////////////////////////////////////////////////////////////////
void open_fifo (fifo_desc_t* desc) {
  int fd = open (desc->pathname, desc->flags);
  if (fd == -1) {
    int errsv = errno;
    switch (errsv) {
      case ENXIO: break;
      default:
        print_fifo_desc (desc);
        printf ( "%s:l%d: %s: %s (errno: %d)\n"
               , __FILE__, __LINE__, __func__, strerror (errsv), errsv);
        exit (EXIT_FAILURE);
    }
  }
  desc->fd = fd;
}

// create a fifo on the filesystem for other processes to open
//////////////////////////////////////////////////////////////
void create_fifo (fifo_desc_t* desc) {
  // create the parent dir hierarchy (copy path for string manipulation)
  size_t len = strlen (desc->pathname) + 1;
  char* pathcpy = (char*) malloc (len * sizeof (char));
  strcpy (pathcpy, desc->pathname);
  create_dir_path (dirname (pathcpy), desc->mode);
  // create the unix fifo
  if (mkfifo (desc->pathname, desc->mode) == -1) {
    int errsv = errno;
    switch (errsv) {
      case EEXIST: break;
      default:
        print_fifo_desc (desc);
        printf ( "%s:l%d: %s: %s (errno: %d)\n"
               , __FILE__, __LINE__, __func__, strerror (errsv), errsv);
        exit (EXIT_FAILURE);
    }
  }
  // open the unix fifo
  open_fifo (desc);
  // set the fifo size
  //fcntl (desc->fd, F_SETPIPE_SZ, DFLT_PIPE_SZ);
}

// close an opened blue unix fifo file descriptor
/////////////////////////////////////////////////
void close_fifo (fifo_desc_t* desc) {
  if (close (desc->fd) == -1) {
    int errsv = errno;
    switch (errsv) {
      default:
        print_fifo_desc (desc);
        printf ( "%s:l%d: %s: %s (errno: %d)\n"
               , __FILE__, __LINE__, __func__, strerror (errsv), errsv);
        exit (EXIT_FAILURE);
    }
  }
}

// TODO
///////
void unlink_fifo (fifo_desc_t* desc) {
  if (unlink (desc->pathname) == -1) {
    int errsv = errno;
    switch (errsv) {
      default:
        print_fifo_desc (desc);
        printf ( "%s:l%d: %s: %s (errno: %d)\n"
               , __FILE__, __LINE__, __func__, strerror (errsv), errsv);
        exit (EXIT_FAILURE);
    }
  }
}

// TODO
///////
void destroy_fifo (fifo_desc_t* desc) {
  close_fifo (desc);
  unlink_fifo (desc);
  destroy_fifo_desc (desc);
}

// read bytes from the fifo and aggregate them in the descriptor buffer
///////////////////////////////////////////////////////////////////////
void read_fifo (fifo_desc_t* desc) {
  // check for presence of producer
  if (desc->fd == -1) open_fifo (desc);
  if (desc->fd == -1) return; // immediate return if no producer is present
  // read for appropriate amount of bytes and accumulate in buf
  size_t remaining = desc->element_byte_size - desc->byte_count;
  ssize_t res = read (desc->fd, desc->buf + desc->byte_count, remaining);
  // handle possible errors
  if (res == -1) {
    int errsv = errno;
    switch (errsv) {
      case EAGAIN: return;
      default:
        print_fifo_desc (desc);
        printf ( "%s:l%d: %s: %s (errno: %d)\n"
               , __FILE__, __LINE__, __func__, strerror (errsv), errsv);
        exit (EXIT_FAILURE);
    }
  }
  // on read success, accumulate bytes read
  desc->byte_count += res;
}

// write bytes into the fifo and keep track of how many in the descriptor
/////////////////////////////////////////////////////////////////////////
void write_fifo (fifo_desc_t* desc, const uint8_t* data) {
  // check for presence of consumer
  if (desc->fd == -1) open_fifo (desc);
  if (desc->fd == -1) return;
  // write remaining bytes to write
  size_t remaining = desc->element_byte_size - desc->byte_count;
  int res = write (desc->fd, data + desc->byte_count, remaining);
  // handle possible errors
  if (res == -1) {
    int errsv = errno;
    switch (errsv) {
      case EBADF:
      case EPIPE:
      case EAGAIN:
        return;
      default:
        print_fifo_desc (desc);
        printf ( "%s:l%d: %s: %s (errno: %d)\n"
               , __FILE__, __LINE__, __func__, strerror (errsv), errsv);
        exit (EXIT_FAILURE);
    }
  }
  // on write success, accumulate bytes written
  desc->byte_count += res;
}

// wrap relevant functions for Bluespec SystemVerilog BDPI API
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

fifo_desc_t* bub_fifo_BDPI_Create (char* pathname, size_t bytesize) {
  // initialise a fifo descriptor
  fifo_desc_t* desc = create_fifo_desc ( pathname
                                       , 0666
                                       , O_RDWR | O_NONBLOCK
                                       , bytesize
                                       , NULL
                                       , NULL );
  // create the fifo
  create_fifo (desc);
  // return the fifo descriptor
  return desc;
}

void bub_fifo_BDPI_Read (unsigned int* retbufptr, fifo_desc_t* desc) {
  read_fifo (desc);
  // byte handle on the return buffer
  uint8_t* retbuf = (uint8_t*) retbufptr;
  // return no bytes read by default
  memset (retbuf, 0, 4);
  // if full data has been read, copy internal buf into retbuf with read size,
  // and reset byte count
  if (desc->byte_count == desc->element_byte_size) {
    desc->byte_count = 0;
    (*retbuf) = (uint32_t) desc->element_byte_size;
    memcpy (retbuf + 4, desc->buf, desc->element_byte_size);
  }
}

unsigned char bub_fifo_BDPI_Write (fifo_desc_t* desc, unsigned int* data) {
  write_fifo (desc, (const uint8_t*) data);
  // if full data has been written, reset byte count and return success
  if (desc->byte_count == desc->element_byte_size) {
    desc->byte_count = 0;
    return 1;
  }
  return 0;
}

// wrap relevant functions for the rest of the API
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

fifo_desc_t* bub_fifo_OpenAsProducer ( char* pathname
                                     , size_t bytesize
                                     , encoder_t encoder ) {
  // initialise a fifo descriptor
  fifo_desc_t* desc = create_fifo_desc ( pathname
                                       , 0000
                                       , O_WRONLY | O_NONBLOCK
                                       , bytesize
                                       , encoder
                                       , NULL );
  // open the unix fifo
  open_fifo (desc);
  // return the descriptor
  return desc;
}

fifo_desc_t* bub_fifo_OpenAsConsumer ( char* pathname
                                     , size_t bytesize
                                     , decoder_t decoder ) {
  // initialise a fifo descriptor
  fifo_desc_t* desc = create_fifo_desc ( pathname
                                       , 0000
                                       , O_RDONLY | O_NONBLOCK
                                       , bytesize
                                       , NULL
                                       , decoder );
  // open the unix fifo
  open_fifo (desc);
  // return the descriptor
  return desc;
}

fifo_desc_t* bub_fifo_OpenAsProducerConsumer ( char* pathname
                                             , size_t bytesize
                                             , encoder_t encoder
                                             , decoder_t decoder ) {
  // initialise a fifo descriptor
  fifo_desc_t* desc = create_fifo_desc ( pathname
                                       , 0000
                                       , O_RDWR | O_NONBLOCK
                                       , bytesize
                                       , encoder
                                       , decoder );
  // open the unix fifo
  open_fifo (desc);
  // return the descriptor
  return desc;
}

bool bub_fifo_Consume (fifo_desc_t* desc, void* elemdest) {
  read_fifo (desc);
  if (desc->byte_count == desc->element_byte_size) {
    desc->byte_count = 0;
    desc->decoder (desc->buf, elemdest);
    return true;
  }
  return false;
}

bool bub_fifo_Produce (fifo_desc_t* desc, void* elemsrc) {
  // encode on first attempt
  if (desc->byte_count == 0) desc->encoder (elemsrc, desc->buf);
  write_fifo (desc, desc->buf);
  // if full data has been written, reset byte count and return success
  if (desc->byte_count == desc->element_byte_size) {
    desc->byte_count = 0;
    return true;
  }
  return false;
}

void bub_fifo_Close (fifo_desc_t* desc) {
  close_fifo (desc);
}
