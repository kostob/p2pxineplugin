/* 
 * File:   network.h
 * Author: tobias
 *
 * Created on 26. November 2013, 08:16
 */

#ifndef NETWORK_H
#define	NETWORK_H

// GRAPES
#include <net_helper.h>

//#include "output_ffmpeg.h"
#include "streamer.h"
#include "output_factory.h"

extern int nextChunk;

char *network_create_interface(char *interface);
int network_add_to_peerChunk(struct nodeID *remote, int chunkId);
struct ChunkIDSet *network_chunkBuffer_to_buffermap();
void network_send_chunks_to_peers();
void network_print_chunkBuffer();
struct ChunkIDSet *network_get_needed_chunks(struct ChunkIDSet *chunkIDSetReceived);
void network_handle_chunk_message(struct nodeID *remote, uint8_t *buffer, int numberOfReceivedBytes);
void network_handle_secured_chunk_message(struct nodeID *remote, uint8_t *buffer, int numberOfReceivedBytes);
void network_handle_secured_login_message(struct nodeID *remote, uint8_t *buffer, int numberOfReceivedBytes);


#endif	/* NETWORK_H */

