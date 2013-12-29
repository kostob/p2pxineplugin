/* 
 * File:   threads.c
 * Author: tobias
 *
 * Created on 26. November 2013, 10:16
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>

// GRAPES
#include <grapes_msg_types.h>
#include <chunk.h>
#include <chunkbuffer.h>
#include <net_helper.h>
#include <peer.h>
#include <peersampler.h>
#include <chunkidset.h>
#include <trade_sig_ha.h>

#include "threads.h"
#include "streamer.h"
#include "network.h"
#include "output_factory.h"

#define BUFFSIZE 64 * 1024

pthread_mutex_t chunkBufferMutex; // for chunkbuffer and chunkIDSet
pthread_mutex_t topologyMutex; // for peersampler
pthread_mutex_t peerChunkMutex; // for peerChunk

static int stopThreads;
static int transId = 1;

extern int configServerPort;

extern struct output_module *output;

static void *threads_receive_data(void *mut) {
#ifdef DEBUG
    fprintf(stderr, "DEBUG: Thread started receive_data\n");
#endif
    
    p2p_input_plugin_t *plugin = (p2p_input_plugin_t *) mut;

    while (stopThreads != 1) {
        int numberOfReceivedBytes;
        struct nodeID *remote;
        static uint8_t buffer[BUFFSIZE];
#ifdef DEBUG
        fprintf(stderr, "DEBUG: Waiting for data...\n");
#endif
        numberOfReceivedBytes = recv_from_peer(localSocket, &remote, buffer, BUFFSIZE);
#ifdef DEBUG
        char remoteAddress[256];
        node_addr(remote, remoteAddress, 256);
        fprintf(stderr, "DEBUG: Data received from %s: MSG_TYPE [0x%x]!\n", remoteAddress, buffer[0]);
#endif
        switch (buffer[0] /* Message Type */) {
            case MSG_TYPE_TOPOLOGY:
            {
#ifdef DEBUG
                char remoteAddress[256];
                node_addr(remote, remoteAddress, 256);
                fprintf(stderr, "DEBUG: Received topology message from peer %s!\n", remoteAddress);
#endif
                pthread_mutex_lock(&topologyMutex);
                psample_parse_data(peersampleContext, buffer, numberOfReceivedBytes);
                pthread_mutex_unlock(&topologyMutex);
                break;
            }
            case MSG_TYPE_CHUNK:
            {
                pthread_mutex_lock(&chunkBufferMutex);
                network_handle_chunk_message(remote, buffer, numberOfReceivedBytes);
                pthread_mutex_unlock(&chunkBufferMutex);
                break;
            }
            case MSG_TYPE_SECURED_DATA_CHUNK:
            {
                if (output->module->secured_data_enabled_chunk(output->context) == 1) {
                    pthread_mutex_lock(&chunkBufferMutex);
                    network_handle_secured_chunk_message(remote, buffer, numberOfReceivedBytes);
                    pthread_mutex_unlock(&chunkBufferMutex);
                }
                break;
            }
            case MSG_TYPE_SECURED_DATA_LOGIN:
            {
                if (output->module->secured_data_enabled_login(output->context) == 1) {
                    network_handle_secured_login_message(remote, buffer, numberOfReceivedBytes);
                }
                break;
            }
            case MSG_TYPE_SIGNALLING:
            {
                struct nodeID *owner;
                int maxDeliver;
                uint16_t transId;
                enum signaling_type signalingType;
                int result;
                ChunkIDSet *chunkIDSetReceived;

                result = parseSignaling(buffer + 1, numberOfReceivedBytes - 1, &owner, &chunkIDSetReceived, &maxDeliver, &transId, &signalingType);
                if (owner)
                    nodeid_free(owner);

                switch (signalingType) {
                    case sig_accept:
                    {
#ifdef DEBUG
                        fprintf(stderr, "DEBUG: 1) Message ACCEPT: peer accepted %d chunks\n", chunkID_set_size(chunkIDSetReceived));
#endif
                        pthread_mutex_lock(&peerChunkMutex);
                        int i, chunkID;
                        for (i = 0; i < chunkID_set_size(chunkIDSetReceived); ++i) {
                            chunkID = chunkID_set_get_chunk(chunkIDSetReceived, i);

                            // add to PeerChunk
                            network_add_to_peerChunk(remote, chunkID);
                        }
                        pthread_mutex_unlock(&peerChunkMutex);

                        break;
                    }
                    case sig_deliver:
                    {
#ifdef DEBUG
                        fprintf(stderr, "DEBUG: 1) Message DELIVER: peer wants me to deliver %d chunks\n", chunkID_set_size(chunkIDSetReceived));
#endif
                        pthread_mutex_lock(&peerChunkMutex);
                        int i, chunkID;
                        for (i = 0; i < chunkID_set_size(chunkIDSetReceived); ++i) {
                            chunkID = chunkID_set_get_chunk(chunkIDSetReceived, i);

                            // add to PeerChunk
                            network_add_to_peerChunk(remote, chunkID);
                        }
                        pthread_mutex_unlock(&peerChunkMutex);
                        break;
                    }
                    case sig_ack:
                        break;
                    case sig_offer:
                    {

#ifdef DEBUG
                        fprintf(stderr, "DEBUG: 1) Message OFFER: peer offers %d chunks\n", chunkID_set_size(chunkIDSetReceived));
#endif
                        int i, count = 0;
                        ChunkIDSet *chunkIDSetForMe = chunkID_set_init("size=0");

                        pthread_mutex_lock(&chunkBufferMutex);

                        // run through all chunks transmitted chunkIDSet and accept 'maxDeliver' chunks not in own chunkbuffer
                        for (i = 0; i < chunkID_set_size((struct chunkID_set*) chunkIDSetReceived); ++i) {
                            int chunkId = chunkID_set_get_chunk((struct chunkID_set*) chunkIDSetReceived, i);
                            const struct chunk *chunk;
                            chunk = cb_get_chunk((struct chunk_buffer*) chunkBuffer, chunkId);
                            if (chunk == NULL && count < maxDeliver) {
                                chunkID_set_add_chunk((struct chunkID_set*) chunkIDSetForMe, chunkId);
                                ++count;
                            }
                        }

                        pthread_mutex_unlock(&chunkBufferMutex);
#ifdef DEBUG
                        fprintf(stderr, "DEBUG: 2) Accepting %d chunks\n", chunkID_set_size((struct chunkID_set*) chunkIDSetForMe));
#endif
                        // accept the chunks in chunkset
                        int res = acceptChunks(remote, (struct chunkID_set*) chunkIDSetForMe, transId++);
#ifdef DEBUG
                        char remoteAddress[256];
                        node_addr(remote, remoteAddress, 256);
                        if (res == 1) {
                            fprintf(stderr, "DEBUG: 3) Accepting message successfully transmitted to %s\n", remoteAddress);
                        } else {
                            fprintf(stderr, "DEBUG: 3) Accepting message transmitting to %s failed.\n", remoteAddress);
                        }
#endif

                        break;
                    }
                    case sig_request: // peer requests x chunks
                    {
#ifdef DEBUG
                        fprintf(stderr, "DEBUG: 1) Message REQUEST: peer requests %d chunks\n", chunkID_set_size(chunkIDSetReceived));
#endif
                        int chunkToSend;
                        chunkToSend = chunkID_set_get_earliest((struct chunkID_set*) chunkIDSet);

                        pthread_mutex_lock(&chunkBufferMutex);
                        pthread_mutex_lock(&peerChunkMutex);
                        int res;
                        res = network_add_to_peerChunk(remote, chunkToSend); // add peer/chunk to a list, for sending in sendChunk thread

                        if (res == 0) {
                            // we have the chunk, we could send it to the remote peer
                            struct ChunkIDSet *chunkIDSetForDeliverToRemote = (struct ChunkIDSet*) chunkID_set_init("size=1");
#ifdef DEBUG
                            fprintf(stderr, "DEBUG: 2) Deliver only earliest chunk #%d\n", chunkToSend);
#endif
                            chunkID_set_add_chunk((struct chunkID_set*) chunkIDSetForDeliverToRemote, chunkToSend);

                            // tell remote that this chunk could be delivered
                            deliverChunks(remote, (struct chunkID_set*) chunkIDSetForDeliverToRemote, transId++);
                        } else {
                            // the requested chunk is too old, send current buffermap
                            struct ChunkIDSet *chunkIDSetRemote = network_chunkBuffer_to_buffermap();
                            sendBufferMap(remote, localSocket, (struct chunkID_set*) chunkIDSetRemote, chunkBufferSize, transId++);
                        }
                        pthread_mutex_unlock(&peerChunkMutex);
                        pthread_mutex_unlock(&chunkBufferMutex);

                        break;
                    }
                    case sig_send_buffermap: // peer sent a buffer of chunks
                    {
                        fprintf(stdout, "1) Message SEND_BMAP: I received a buffer of %d chunks\n", chunkID_set_size(chunkIDSetReceived));
                        //printChunkID_set(Streamer::chunkIDSet);

                        pthread_mutex_lock(&chunkBufferMutex);

                        // run through local buffermap and check which chunk is needed
                        struct ChunkIDSet *chunkIDSetForMe = network_get_needed_chunks((struct ChunkIDSet*) chunkIDSetReceived);

                        pthread_mutex_unlock(&chunkBufferMutex);

                        fprintf(stdout, "2) Message SEND_BMAP: I send my buffer of %d chunks\n", chunkID_set_size((struct chunkID_set*) chunkIDSetForMe));

                        // request chunks out of the received buffermap
                        requestChunks(remote, (struct chunkID_set*) chunkIDSetForMe, chunkID_set_size((struct chunkID_set*) chunkIDSetForMe), transId++);
                        break;
                    }
                    case sig_request_buffermap: // peer requests my buffer map
                    {
#ifdef DEBUG
                        char remoteAddress[256];
                        node_addr(remote, remoteAddress, 256);
                        fprintf(stdout, "1) Message REQUEST_BMAP: %s requested my buffer map\n", remoteAddress);
#endif
                        pthread_mutex_lock(&chunkBufferMutex);
                        sendBufferMap(remote, localSocket, (struct chunkID_set*) chunkIDSet, chunkID_set_size((struct chunkID_set*) chunkIDSet), transId++);
                        pthread_mutex_unlock(&chunkBufferMutex);
                        break;
                    }
                }
                break;
            }
            default:
            {
                fprintf(stderr, "Unknown Message Type %x\n", buffer[0]);
            }
        }
        nodeid_free(remote);
    }

    return NULL;
}

static void *threads_send_topology(void *mut) {
#ifdef DEBUG
    fprintf(stderr, "DEBUG: Thread started send_topology\n");
#endif

    while (stopThreads != 1) {
        const struct nodeID * const *neighbours;
        int numberOfNeighbours, i;

        pthread_mutex_lock(&topologyMutex);
        psample_parse_data(peersampleContext, NULL, 0);
        neighbours = psample_get_cache(peersampleContext, &numberOfNeighbours);
#ifdef DEBUG
        char addr[256];
        fprintf(stderr, "DEBUG: I have %d neighbours:\n", numberOfNeighbours);
        for (i = 0; i < numberOfNeighbours; i++) {
            node_addr(neighbours[i], addr, 256);
            fprintf(stderr, "DEBUG: \t%d: %s\n", i, addr);
        }
#endif
        if (numberOfNeighbours == 0) {
            fprintf(stderr, "ERROR: Lost all connections, even to server.\n");
            stopThreads = 1;
            // TODO: stop the receiving thread (recv_from_peer will block till data where received)
        }
        pthread_mutex_unlock(&topologyMutex);
        usleep(1 * 1000 * 1000); // 1 sec
    }

    return NULL;
}

static void *threads_offer_chunk(void *mut) {
#ifdef DEBUG
    fprintf(stderr, "DEBUG: Thread started offer_chunk\n");
#endif
    while (stopThreads != 1) {
        pthread_mutex_lock(&topologyMutex);
        pthread_mutex_lock(&chunkBufferMutex);

        int maximumNeighboursToSendOffersTo = 10;
        const struct nodeID * const *neighbours;
        int numberOfNeighbours, i;
        neighbours = psample_get_cache(peersampleContext, &numberOfNeighbours);

        for (i = 0; i < maximumNeighboursToSendOffersTo && i < numberOfNeighbours; ++i) {
            // don't offer chunks to the server...
            if (nodeid_equal(serverSocket, neighbours[i]) == 0) {
#ifdef DEBUG
                char addr[256];
                node_addr(neighbours[i], addr, 256);
                fprintf(stderr, "DEBUG: offer chunk: offer buffermap to %s\n", addr);
#endif
                struct ChunkIDSet *cset = network_chunkBuffer_to_buffermap();
                offerChunks((struct nodeID*) neighbours[i], (struct chunkID_set*) cset, 50, transId++);
                //sendBufferMap(peers[i]->id, localSocket, network_chunkBuffer_to_buffermap(), chunkBufferSize, transId++);
            }
        }

        pthread_mutex_unlock(&chunkBufferMutex);
        pthread_mutex_unlock(&topologyMutex);
        usleep(5 * 1000 * 100); // 0.5 sec
    }

    return NULL;
}

static void *threads_send_chunk(void *mut) {
#ifdef DEBUG
    fprintf(stderr, "DEBUG: Thread started send_chunk\n");
#endif
    //    mutexes *m = (mutexes *) mut;
    //    Threads *t = m->t;
    //
    //    Network *network = Network::getInstance();
    //    int chunk_period = Threads::gossipingPeriod / Threads::chunks_per_period;

    while (stopThreads != 1) {
        pthread_mutex_lock(&topologyMutex);
        pthread_mutex_lock(&chunkBufferMutex);
        pthread_mutex_lock(&peerChunkMutex);
        network_send_chunks_to_peers();
        pthread_mutex_unlock(&peerChunkMutex);
        pthread_mutex_unlock(&chunkBufferMutex);
        pthread_mutex_unlock(&topologyMutex);
        //        usleep(chunk_period);
    }

    return NULL;
}

static void *threads_request_secured_data_login(void *mut) {
#ifdef DEBUG
    fprintf(stderr, "DEBUG: Thread started request_secured_data_login\n");
#endif
    if (output->module->secured_data_enabled_login(output->context) == 1) {
        requestSecuredDataLogin(serverSocket, transId++);
    }

    return NULL;
}

void threads_start(p2p_input_plugin_t *plugin) {
#ifdef DEBUG
    fprintf(stderr, "DEBUG: Called threads_start\n");
#endif

    pthread_t offerChunkThread, receiveDataThread, sendTopologyThread, sendChunkThread, requestSecuredDataLoginThread;

    stopThreads = 0;

#ifdef DEBUG
    fprintf(stderr, "DEBUG: psample_parse_data started...\n");
#endif
    //    peerSet = peerset_init(0); // CHECK
    psample_parse_data(peersampleContext, NULL, 0);
#ifdef DEBUG
    fprintf(stderr, "DEBUG: psample_parse_data done...\n");
#endif

    // initialize mutexes
#ifdef DEBUG
    fprintf(stderr, "DEBUG: initializing mutexes...\n");
#endif
    pthread_mutex_init(&chunkBufferMutex, NULL);
    pthread_mutex_init(&topologyMutex, NULL);
    pthread_mutex_init(&peerChunkMutex, NULL);
#ifdef DEBUG
    fprintf(stderr, "DEBUG: initializing mutexes done...\n");
#endif

    // create threads
#ifdef DEBUG
    fprintf(stderr, "DEBUG: creating threads...\n");
#endif
    pthread_create(&receiveDataThread, NULL, threads_receive_data, (void *) plugin); // Thread for receiving data
    pthread_create(&sendTopologyThread, NULL, threads_send_topology, NULL); // Thread for sharing the topology of the p2p network
    pthread_create(&offerChunkThread, NULL, threads_offer_chunk, NULL); // Thread for generating chunks
    pthread_create(&sendChunkThread, NULL, threads_send_chunk, NULL); // Thread for sending the chunks
    pthread_create(&requestSecuredDataLoginThread, NULL, threads_request_secured_data_login, NULL); // Thread for requesting secured data for login
#ifdef DEBUG
    fprintf(stderr, "DEBUG: creating threads done...\n");
#endif

    // join threads
#ifdef DEBUG
    fprintf(stderr, "DEBUG: joining threads...\n");
#endif
    //pthread_join(offerChunkThread, NULL);
    //pthread_join(receiveDataThread, NULL);
    //pthread_join(sendTopologyThread, NULL);
    //pthread_join(sendChunkThread, NULL);
    //pthread_join(requestSecuredDataLoginThread, NULL);
#ifdef DEBUG
    fprintf(stderr, "DEBUG: joining threads done...\n");
#endif
}
