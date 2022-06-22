#ifndef BLUE_UNIX_FIFO_H
#define BLUE_UNIX_FIFO_H

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

// This header declares the BlueUnixFifo API for C client, as well as the C
// functions to be wrapped in Bluespec SystemVerilog BDPI calls for use in
// BSV simulator code.

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/stat.h>

// A function pointer to a callback helper turning a raw array of bytes into a
// more useful C type (notionally equivalent to BSV's unpack);
typedef void (*decoder_t)(const uint8_t* rawbytes, void* dest);
// A function pointer to a callback helper turning a C type into a raw array of
// of bytes (notionally equivalent to BSV's pack).
typedef void (*encoder_t)(const void* src, uint8_t* rawbytes);
// A BlueUnixFifo descriptor
typedef struct {
  char* pathname;
  int fd;
  mode_t mode;
  int flags;
  size_t element_byte_size;
  uint8_t* buf;
  ssize_t byte_count;
  encoder_t encoder;
  decoder_t decoder;
} fifo_desc_t;

// BDPI Bluespec SystemVerilog API
////////////////////////////////////////////////////////////////////////////////
#ifdef __cplusplus
extern "C" {
#endif
// Create a unix fifo on the filesystem at `pathname` and for data of size
// `bytesize` bytes, and open it RW. Return a unix fifo descriptor pointer to
// be stored by the simulator for later operations.
// Note: opening RW lets the simulator process persist if client processes pop
// in and out of existance (by maintaining the fifo channel effectively open
// for the lifetime of the simulator)
extern fifo_desc_t* bub_fifo_BDPI_Create (char* pathname, size_t bytesize);
// Read into `retbuf` one element from the fifo described by `desc`, of the
// size specified in `desc`. `retbuf`'s bottom 32 bits contain 0 if the read
// needs to be attempted again, and the bytesize of the element on
// successfull read. If the read succeeded, the upper bits in retbuf contain
// the element that was read.
extern void bub_fifo_BDPI_Read (unsigned int* retbuf, fifo_desc_t* desc);
// Write the element in `d` into the fifo described by `desc`, where the
// element in `d` must be of the size specified in `desc`. Returns 1 upon
// success, and 0 if the write should be re-attempted.
extern unsigned char bub_fifo_BDPI_Write (fifo_desc_t* desc, unsigned int* d);
#ifdef __cplusplus
}
#endif

// rest of the API
////////////////////////////////////////////////////////////////////////////////
#ifdef __cplusplus
extern "C" {
#endif
extern fifo_desc_t* bub_fifo_OpenAsProducer ( char* pathname
                                            , size_t bytesize
                                            , encoder_t encoder );
extern fifo_desc_t* bub_fifo_OpenAsConsumer ( char* pathname
                                            , size_t bytesize
                                            , decoder_t decoder );
extern fifo_desc_t* bub_fifo_OpenAsProducerConsumer ( char* pathname
                                                    , size_t bytesize
                                                    , encoder_t encoder
                                                    , decoder_t decoder );
extern bool bub_fifo_Consume (fifo_desc_t* desc, void* elemdest);
extern bool bub_fifo_Produce (fifo_desc_t* desc, void* elemsrc);
extern void bub_fifo_Close (fifo_desc_t* desc);
#ifdef __cplusplus
}
#endif

//#define DFLT_PIPE_SZ 1024

#endif
