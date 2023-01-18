/*-
 * Copyright (c) 2022-2023 Alexandre Joannou
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

package BlueUnixFifo;

import FIFOF :: *;
import Vector :: *;
import SourceSink :: *;
import Printf :: *;

// C imports
////////////////////////////////////////////////////////////////////////////////
typedef Bit #(64) BlueUnixFifoHandle;

typedef Bit #(8) ByteReadStatus;
ByteReadStatus    lastByteRead = 8'h00;
ByteReadStatus   validByteRead = 8'h01;
ByteReadStatus invalidByteRead = 8'h02;

typedef Bit #(8) ByteWriteStatus;
ByteWriteStatus lastByteWritten = 8'h00;
ByteWriteStatus     byteWritten = 8'h01;
ByteWriteStatus  byteNotWritten = 8'h02;

import "BDPI" bub_fifo_DPI_C_Create =
  function ActionValue #(BlueUnixFifoHandle) createFF ( String pathname
                                                      , Bit #(32) byte_size );
import "BDPI" bub_fifo_DPI_C_ReadByte =
  function ActionValue #(Tuple2 #(ByteReadStatus, Bit #(8)))
             consumeByteFF (BlueUnixFifoHandle handle);
import "BDPI" bub_fifo_DPI_C_WriteByte =
  function ActionValue #(ByteWriteStatus)
             produceByteFF (BlueUnixFifoHandle handle, Bit #(8) data);
import "BDPI" bub_fifo_DPI_C_ResetCount =
  function Action resetCountFF (BlueUnixFifoHandle handle);

module mkFFConsumer #(Maybe #(BlueUnixFifoHandle) handle) (Source #(t))
  provisos ( NumAlias #(nBytes, TDiv #(n, 8))
           , NumAlias #(cntSz, TLog #(TAdd #(n, 1)))
           , Bits #(t, n)
           , Add #(_a, n, TMul #(nBytes, 8))
           );

  Vector #(nBytes, Reg #(Bit #(8))) val <- replicateM (mkRegU);
  Vector #(nBytes, Reg #(Bool)) valTodo <- replicateM (mkReg (True));

  function isFalse (x) = !x;

  rule consume_bytes (isValid (handle) && !all(isFalse, readVReg (valTodo)));
    Bool active = True;
    for (Integer i = 0; i < valueOf (nBytes); i = i + 1) begin
      if (active && valTodo[i]) begin
        //match {.readStatus, .readByte} <- consumeByteFF (handle.Valid);
        match {.readByte, .readStatus} <- consumeByteFF (handle.Valid);
        case (readStatus)
          invalidByteRead: active = False;
          validByteRead: begin
            val[i] <= readByte;
            valTodo[i] <= False;
          end
          lastByteRead: begin
            val[i] <= readByte;
            valTodo[i] <= False;
            if (i != (valueOf (nBytes) - 1)) begin
              $display("finished draining a fifo but expecting more bytes");
              $finish(-1);
            end
          end
        endcase
      end
    end
  endrule

  Bool valCanPeek = isValid (handle) && all (isFalse, readVReg (valTodo));

  method canPeek = valCanPeek;
  method peek if (valCanPeek) = unpack (truncate (pack (readVReg (val))));
  method drop if (valCanPeek) = action
    writeVReg (valTodo, replicate (True));
    resetCountFF (handle.Valid);
  endaction;

endmodule


module mkFFProducer #(Maybe #(BlueUnixFifoHandle) handle) (Sink #(t))
  provisos ( NumAlias #(nBytes, TDiv #(n, 8))
           , NumAlias #(cntSz, TLog #(TAdd #(n, 1)))
           , Bits #(t, n)
           , Add #(_a, n, TMul #(nBytes, 8))
           );

  Vector #(nBytes, Reg #(Bit #(8))) val <- replicateM (mkRegU);
  Vector #(nBytes, Reg #(Bool)) valTodo <- replicateM (mkReg (True));

  function isFalse (x) = !x;

  rule produce_bytes (isValid (handle) && !all(isFalse, readVReg (valTodo)));
    Bool active = True;
    for (Integer i = 0; i < valueOf (nBytes); i = i + 1) begin
      if (active && valTodo[i]) begin
        let writeStatus <- produceByteFF (handle.Valid, val[i]);
        case (writeStatus)
          byteNotWritten: active = False;
          byteWritten: valTodo[i] <= False;
          lastByteWritten: begin
            valTodo[i] <= False;
            if (i != (valueOf (nBytes) - 1)) begin
              $display("finished putting to a fifo but expecting more bytes");
              $finish(-1);
            end
          end
        endcase
      end
    end
  endrule

  Bool valCanPut = isValid (handle) && all (isFalse, readVReg (valTodo));

  method canPut = valCanPut;
  method put (x) if (valCanPut) = action
    writeVReg (val, unpack (zeroExtend (pack (x))));
    writeVReg (valTodo, replicate (True));
    resetCountFF (handle.Valid);
  endaction;

endmodule

////////////////////////////////////////////////////////////////////////////////
module mkUnixFifoSource #(String pathname) (Source #(t))
  // constraints
  provisos ( Bits #(t, t_bits_sz)
           , Add #(1, _dummy0, t_bits_sz) // t_bits_sz > 0
           , NumAlias #(t_bytes_sz, TDiv #(t_bits_sz, 8))
           , Add #(_a, t_bits_sz, TMul #(t_bytes_sz, 8))
           );

  // prepare unix fifo descriptor
  Reg #(Maybe #(BlueUnixFifoHandle)) blueUnixFifoHandle <- mkReg (Invalid);
  rule create (!isValid (blueUnixFifoHandle));
    let handle <- createFF (pathname, fromInteger (valueOf (t_bytes_sz)));
    blueUnixFifoHandle <= Valid (handle);
    $display ( "using unix fifo \"", pathname
             , "\", %0d-bit payload (%0d byte(s)), opened as a source"
             , valueOf (t_bits_sz), valueOf (t_bytes_sz) );
  endrule

  // wrap the unix fifo consumer and export as source
  Source #(t) src <- mkFFConsumer (blueUnixFifoHandle);
  return src;

endmodule

////////////////////////////////////////////////////////////////////////////////
module mkUnixFifoSink #(String pathname) (Sink #(t))
  // constraints
  provisos ( Bits #(t, t_bits_sz)
           , Add #(1, _dummy0, t_bits_sz) // t_bits_sz > 0
           , NumAlias #(t_bytes_sz, TDiv #(t_bits_sz, 8))
           , Add #(_a, t_bits_sz, TMul #(t_bytes_sz, 8))
           );

  // prepare unix fifo descriptor
  Reg #(Maybe #(BlueUnixFifoHandle)) blueUnixFifoHandle <- mkReg (Invalid);
  rule create (!isValid (blueUnixFifoHandle));
    let handle <- createFF (pathname, fromInteger (valueOf (t_bytes_sz)));
    blueUnixFifoHandle <= Valid (handle);
    $display ( "using unix fifo ", pathname
             , ", %0d-bit payload (%0d byte(s)), opened as a sink"
             , valueOf (t_bits_sz), valueOf (t_bytes_sz) );
  endrule

  // wrap the unix fifo producer and export as sink
  Sink #(t) snk <- mkFFProducer (blueUnixFifoHandle);
  return snk;

endmodule

/*

import "BDPI" bub_fifo_BDPI_Create =
  function ActionValue #(BlueUnixFifoHandle) createFF ( String pathname
                                                      , Bit #(32) byte_size );
import "BDPI" bub_fifo_BDPI_Read =
  function ActionValue #(Bit #(n)) consumeFF (BlueUnixFifoHandle handle);
import "BDPI" bub_fifo_BDPI_Write =
  function ActionValue #(Bool) produceFF ( BlueUnixFifoHandle handle
                                         , Bit #(n) data );

// local helpers
////////////////////////////////////////////////////////////////////////////////

function Bit #(m) truncateOrZeroExtend (Bit #(n) x);
  Bit #(m) y = 0;
  for (Integer i = 0; i < valueOf (TMin #(m, n)); i = i + 1) y[i] = x[i];
  return y;
endfunction

////////////////////////////////////////////////////////////////////////////////
module mkUnixFifoSource #(String pathname) (Source #(t))
  // constraints
  provisos ( Bits #(t, t_bits_sz)
           , Add #(1, _dummy0, t_bits_sz) // t_bits_sz > 0
           , NumAlias #(t_bytes_sz, TDiv #(t_bits_sz, 8))
           , NumAlias #(t_log2_bytes_sz, TLog #(t_bytes_sz)) );
           //, Add #(_dummy1, t_log2_bytes_sz, 32)
           //, Add #(_dummy2, t_bits_sz, TMul #(TDiv #(t_bits_sz, 8), 8)) );

  // static assertion to replace commented out provisos
  if (valueOf (t_log2_bytes_sz) > 32)
    error (sprintf ( "t_log2_bytes_sz: %0d should be less than 32"
                   , valueOf (t_log2_bytes_sz) ));

  // prepare unix fifo descriptor
  Reg #(Maybe #(BlueUnixFifoHandle)) blueUnixFifoHandle <- mkReg (Invalid);
  rule create (!isValid (blueUnixFifoHandle));
    let handle <- createFF (pathname, fromInteger (valueOf (t_bytes_sz)));
    blueUnixFifoHandle <= Valid (handle);
    $display ( "using unix fifo \"", pathname
             , "\", %0d-bit payload (%0d byte(s)), opened as a source"
             , valueOf (t_bits_sz), valueOf (t_bytes_sz) );
  endrule

  // production fifo
  FIFOF #(t) ff <- mkUGFIFOF;

  // receive bytes from the unix fifo and aggregate them in the receive buffer
  // until the full `t` is received
  rule receiveBytes (isValid (blueUnixFifoHandle) && ff.notFull);
    // receive
    Bit #(TAdd #(TMul #(t_bytes_sz, 8), 32))
      raw <- fmap (unpack, consumeFF (blueUnixFifoHandle.Valid));
    Bit #(32) receivedBytes = truncate (raw);
    Bit #(TMul #(t_bytes_sz, 8)) data = truncateLSB (raw);
    // if we received something
    if (receivedBytes == fromInteger (valueOf (t_bytes_sz))) begin
      ff.enq (unpack (truncateOrZeroExtend (data)));
    end
    // nothing was received, keep trying
    else if (receivedBytes == 0) noAction;
    // unexpected case
    else begin
      $display ("raw: ", fshow (raw));
      $display ("receivedBytes: ", fshow (receivedBytes));
      $display ("this should not happen");
      $finish (0);
    end
  endrule

  // return a source interface
  return toGuardedSource (ff);

endmodule

////////////////////////////////////////////////////////////////////////////////
module mkUnixFifoSink #(String pathname) (Sink #(t))
  // constraints
  provisos ( Bits #(t, t_bits_sz)
           , Add #(1, _dummy0, t_bits_sz) // t_bits_sz > 0
           , NumAlias #(t_bytes_sz, TDiv #(t_bits_sz, 8))
           , NumAlias #(t_log2_bytes_sz, TLog #(t_bytes_sz)) );
           //, Add #(_dummy1, t_log2_bytes_sz, 32)
           //, Add #(_dummy2, t_bits_sz, TMul #(TDiv #(t_bits_sz, 8), 8)) );

  // static assertion to replace commented out provisos
  if (valueOf (t_log2_bytes_sz) > 32)
    error (sprintf ( "t_log2_bytes_sz: %0d should be less than 32"
                   , valueOf (t_log2_bytes_sz) ));

  // prepare unix fifo descriptor
  Reg #(Maybe #(BlueUnixFifoHandle)) blueUnixFifoHandle <- mkReg (Invalid);
  rule create (!isValid (blueUnixFifoHandle));
    let handle <- createFF (pathname, fromInteger (valueOf (t_bytes_sz)));
    blueUnixFifoHandle <= Valid (handle);
    $display ( "using unix fifo ", pathname
             , ", %0d-bit payload (%0d byte(s)), opened as a sink"
             , valueOf (t_bits_sz), valueOf (t_bytes_sz) );
  endrule

  // consumption fifo
  FIFOF #(t) ff <- mkUGFIFOF;

  rule sendBytes (isValid (blueUnixFifoHandle) && ff.notEmpty);
    Bit #(TMul #(t_bytes_sz, 8)) data = truncateOrZeroExtend (pack (ff.first));
    Bool sent <- produceFF (blueUnixFifoHandle.Valid, data);
    if (sent) ff.deq;
  endrule

  // return a sink interface
  return toGuardedSink (ff);

endmodule

*/

endpackage
