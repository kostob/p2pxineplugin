/* 
 * File:   network.c
 * Author: tobias
 *
 * Created on 26. November 2013, 15:43
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>

// GRAPES
#include <net_helper.h>
#include <chunk.h>
#include <chunkbuffer.h>
#include <chunkidset.h>
#include <peer.h>
#include <peerset.h>
#include <peersampler.h>
#include <scheduler_common.h>

#include "network.h"
#include "streamer.h"
//#include "output_ffmpeg.h"
#include "output_factory.h"

extern struct output_module *output;

char *network_create_interface(char *interface) {
#ifdef DEBUG
    fprintf(stdout, "Called network_create_interface with parameter interface=%s\n", interface);
#endif

    int socketHandle;
    int res;
    struct ifreq iface_request;
    struct sockaddr_in *sin;
    char buff[512];

    socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (socketHandle < 0) {
        fprintf(stderr, "Could not create socket\n");
        exit(EXIT_FAILURE);
    }

    memset(&iface_request, 0, sizeof (struct ifreq));
    sin = (struct sockaddr_in *) & iface_request.ifr_addr;
    strcpy(iface_request.ifr_name, interface);
    /* sin->sin_family = AF_INET); */
    res = ioctl(socketHandle, SIOCGIFADDR, &iface_request);
    if (res < 0) {
        perror("ioctl(SIOCGIFADDR)");
        close(socketHandle);

        exit(EXIT_FAILURE);
    }
    close(socketHandle);

    inet_ntop(AF_INET, &sin->sin_addr, buff, sizeof (buff));

    return strdup(buff);
}

int network_add_to_peerChunk(struct nodeID *remote, int chunkId) {
    // check if chunk is not too old and is still in the chunk buffer
    if (cb_get_chunk((struct chunk_buffer*) chunkBuffer, chunkId) != NULL) {
        peerChunks = (struct PeerChunk*) realloc(peerChunks, sizeof (struct PeerChunk));
        struct peer *p;
        p = (struct peer*) malloc(sizeof (struct peer));
        char ip[256];
        node_ip(remote, ip, 256);
        p->id = create_node(ip, node_port(remote));
        peerChunks[peerChunksSize].peer = peerset_get_peer((struct peerset*) peerSet, remote);
        peerChunks[peerChunksSize++].chunk = chunkId;
        ++peerChunksSize;
    } else {
        return -1;
    }
    return 0;
}

struct ChunkIDSet *network_chunkBuffer_to_buffermap() {
    struct chunk *chunks;
    int num_chunks, i;
    struct ChunkIDSet *my_bmap = (struct ChunkIDSet*) chunkID_set_init(0);
    chunks = cb_get_chunks((struct chunk_buffer*) chunkBuffer, &num_chunks);

    for (i = 0; i < num_chunks; i++) {
        chunkID_set_add_chunk((struct chunkID_set*) my_bmap, chunks[i].id);
    }
    return my_bmap;
}

void network_send_chunks_to_peers() {
    int i;

    for (i = 0; i < peerChunksSize; ++i) {
        const struct chunk *c = cb_get_chunk((struct chunk_buffer*) chunkBuffer, peerChunks[i].chunk);

        if (c != NULL && peerChunks[i].peer != NULL) { // TODO: modify statement: check if still in psampler
#ifdef DEBUG
            char addressRemote[256];
            node_addr(peerChunks[i].peer->id, addressRemote, 256);
            fprintf(stderr, "DEBUG: Sending chunk %d to peer %s", peerChunks[i].chunk, addressRemote);
#endif
            sendChunk(peerChunks[i].peer->id, c, 0);
            free(peerChunks[i].peer->id);
            peerChunks[i].peer->id = NULL;
        } else {
#ifdef DEBUG
            //char addressRemote[256];
            //node_addr(peerChunks[i].peer->id, addressRemote, 256);
            fprintf(stderr, "DEBUG: Sending chunk %d failed: chunk to old (not in chunkbuffer anymore)\n", peerChunks[i].chunk);
#endif
        }
        free(peerChunks[i].peer);
        peerChunks[i].peer = NULL;
    }

    // reset peerChunks
    free(peerChunks);
    peerChunks = NULL;
    peerChunksSize = 0;
}

void network_print_chunkBuffer() {
    struct chunk *chunks;
    int num_chunks, i;
    chunks = cb_get_chunks((struct chunk_buffer*) chunkBuffer, &num_chunks);

    fprintf(stderr, "DEBUG: Chunks in buffer:");

    for (i = 0; i < num_chunks; i++) {
        if (i % 10 == 0) {
            fprintf(stderr, "\n\t");
        }
        fprintf(stderr, "%5d ", chunks[i].id);
    }

    fprintf(stderr, "\n");
    fflush(stderr);
}

struct ChunkIDSet *network_get_needed_chunks(struct ChunkIDSet *chunkIDSetReceived) {
    int i;
    struct ChunkIDSet *cset = (struct ChunkIDSet*) chunkID_set_init("size=0,type=bitmap");
    if (cset == NULL) {
        fprintf(stderr, "Unable to allocate memory for cset (network_get_needed_chunks)\n");
        return NULL;
    }
    for (i = 0; i < chunkID_set_size((struct chunkID_set*) chunkIDSetReceived); ++i) {
        if (chunkID_set_get_chunk((struct chunkID_set*) chunkIDSetReceived, i) > firstChunk) {
            chunkID_set_add_chunk((struct chunkID_set*) cset, chunkID_set_get_chunk((struct chunkID_set*) chunkIDSetReceived, i));
        }
    }

    return cset;
}

void network_handle_chunk_message(struct nodeID *remote, uint8_t *buffer, int numberOfReceivedBytes) {
    int res;
    static struct chunk c;
    struct peer *p;
    uint16_t transid;

#ifdef DEBUG
    char remoteAddress[256];
    node_addr(remote, remoteAddress, 256);
    fprintf(stderr, "Received chunk data from peer %s\n", remoteAddress);
#endif

    res = parseChunkMsg(buffer + 1, numberOfReceivedBytes - 1, &c, &transid);
    if (res > 0) {
        //chunk_attributes_update_received(&c); // needed? atm possibly not...
        //chunk_unlock(c.id); // needed? atm possibly not...
#ifdef DEBUG
        char remoteAddress[256];
        node_addr(remote, remoteAddress, 256);
        fprintf(stderr, "Received chunk %d from peer: %s\n", c.id, remoteAddress);
#endif
        // check if there are secure data needed...
        if (output->module->secured_data_enabled_chunk(output->context) == 1) {
            struct ChunkIDSet *cset = (struct ChunkIDSet *) chunkID_set_init("size=1");
            chunkID_set_add_chunk((struct chunkID_set *) cset, c.id);
            // ask server for secured data for the chunk
            requestSecuredDataChunk(serverSocket, cset, transid);
        } else {
            output->module->deliver(output->context, &c);
        }
        res = cb_add_chunk((struct chunk_buffer*) chunkBuffer, &c);

        if (res < 0) {
#ifdef DEBUG
            fprintf(stderr, "\tchunk too old, buffer full with newer chunks\n");
#endif
            free(c.data);
            free(c.attributes);
        }

        //struct ChunkIDSet *cids = network_chunkBuffer_to_buffermap();
        //sendAck(remote, (struct chunkID_set *) cids, transid);
    } else {
        fprintf(stderr, "\tError: can't decode chunk!\n");
    }

#ifdef DEBUG
    network_print_chunkBuffer();
#endif
}

void network_handle_secured_chunk_message(struct nodeID* remote, uint8_t *buffer, int numberOfReceivedBytes) {
    // combine original chunk and received data
    int res;
    static struct chunk c;
    struct peer *p;
    uint16_t transid;

#ifdef DEBUG
    char remoteAddress[256];
    node_addr(remote, remoteAddress, 256);
    fprintf(stderr, "Received secure chunk data\n", remoteAddress);
#endif

    res = parseChunkMsg(buffer + 1, numberOfReceivedBytes - 1, &c, &transid);
    if (res > 0) {
        res = output->module->deliver_secured_data_chunk(output->context, &c);
        if (res < 0) {
            fprintf(stderr, "\tError: Something went wrong while processing the secured chunk data!\n");
            free(c.data);
            free(c.attributes);
        }
    } else {
        fprintf(stderr, "\tError: can't decode secure chunk data!\n");
    }
}

void network_handle_secured_login_message(struct nodeID* remote, uint8_t *buffer, int numberOfReceivedBytes) {
    int res;
    static struct chunk c;
    struct peer *p;
    uint16_t transid;

#ifdef DEBUG
    char remoteAddress[256];
    node_addr(remote, remoteAddress, 256);
    fprintf(stderr, "Received secure login data\n", remoteAddress);
#endif

    res = parseChunkMsg(buffer + 1, numberOfReceivedBytes - 1, &c, &transid);
    if (res > 0) {
        res = output->module->deliver_secured_data_login(output->context, &c);
        if (res < 0) {
            fprintf(stderr, "\tError: Something went wrong while processing the secured login data!\n");
            free(c.data);
            free(c.attributes);
        }
    } else {
        fprintf(stderr, "\tError: can't decode secure login data!\n");
    }
}

void network_ask_for_chunk(int chunkId) {
    int numberOfNeighbours, i;
    const struct nodeID * const *neighbours;
    neighbours = psample_get_cache(peersampleContext, &numberOfNeighbours);

    struct ChunkIDSet *cset;
    chunkID_set_add_chunk(cset, chunkId);

    for (i = 0; i < numberOfNeighbours; ++i) {
#ifdef DEBUG
        char addr[256];
        node_addr(neighbours[i], addr, 256);
        fprintf(stderr, "DEBUG: requested chunk from %s\n", addr);
#endif
        requestChunks((struct nodeID*) neighbours[i], (struct chunkID_set*) cset, 1, 0);
    }
}
