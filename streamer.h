/* 
 * File:   peer.h
 * Author: tobias
 *
 * Created on 26. November 2013, 13:49
 */

#ifndef STREAMER_H
#define	STREAMER_H

// GRAPES
#include <net_helper.h>
#include <peersampler.h>
#include <peerset.h>
#include <chunk.h>
#include <chunkbuffer.h>
#include <chunkidset.h>
#include <scheduler_common.h>

#include "input_p2p.h"

static char* configServerAddress;
static int configServerPort;
static char* configInterface;
static int configPort;

extern struct ChunkBuffer *chunkBuffer;
extern int chunkBufferSize;
extern int chunkBufferSizeMax;
extern struct PeerSet *peerSet;
extern struct ChunkIDSet *chunkIDSet;
extern struct PeerChunk *peerChunks;
extern int peerChunksSize;

extern struct nodeID *localSocket;
extern struct psample_context *peersampleContext;
extern struct nodeID *serverSocket;

int init(p2p_input_plugin_t *plugin);

#endif	/* STREAMER_H */

