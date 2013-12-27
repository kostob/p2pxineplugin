/* 
 * File:   main.c
 * Author: tobias
 *
 * Created on 26. November 2013, 08:08
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// GRAPES
#include <net_helper.h>
#include <peersampler.h>
#include <trade_msg_ha.h>

#include "streamer.h"
#include "network.h"
#include "output_factory.h"

char* configServerAddress = "127.0.0.1";
int configServerPort = 6666;
char* configInterface = "lo0";
int configPort = 5555;
char *configPeersample = "protocol=cyclon";
char *configChunkBuffer = "size=100,time=now"; // size must be same value as chunkBufferSizeMax
char *configChunkIDSet = "size=100"; // size must be same value as chunkBufferSizeMax
char *configOutput = "buffer=75";
int configOutputBufferSize = 75;

struct ChunkBuffer *chunkBuffer = NULL;
int chunkBufferSize = 0;
int chunkBufferSizeMax = 100;
struct PeerSet *peerSet = NULL;
struct ChunkIDSet *chunkIDSet = NULL;
struct PeerChunk *peerChunks = NULL;
int peerChunksSize = 0;

struct output_module *output = NULL;

struct nodeID *localSocket;
struct nodeID *serverSocket;
struct psample_context *peersampleContext;

void parseCommandLineArguments(int argc, char* argv[]) {
#ifdef DEBUG
    fprintf(stderr, "DEBUG: called parseCommandLineArguments\n");
#endif
    int i;
    char arg[512];
    for (i = 1; i < argc; ++i) {
        // TODO: check if there is an argument i + 1
        strcpy(arg, argv[i]);

        // server address
        if (strcmp(arg, "-i") == 0) {
            configServerAddress = argv[i + 1];
        }

        // server port
        if (strcmp(arg, "-p") == 0) {
            configServerPort = atoi(argv[i + 1]);
        }

        // interface
        if (strcmp(arg, "-I") == 0) {
            configInterface = argv[i + 1];
        }

        // local port
        if (strcmp(arg, "-P") == 0) {
            configPort = atoi(argv[i + 1]);
        }

        ++i;
    }
}

int init(char *interface, char *serverAddress, int serverPort, int localPort) {

#ifdef DEBUG
    fprintf(stderr, "DEBUG: Called streamer.c init\n");
#endif

    // create the interface for connection
    char *my_addr = network_create_interface(interface);

    if (strlen(my_addr) == 0) {
        fprintf(stderr, "Cannot find network interface %s\n", interface);
        return -1;
    }
#ifdef DEBUG
    fprintf(stderr, "Created network successfully\n");
#endif

    // initialize net helper
    localSocket = net_helper_init(my_addr, localPort, "");
    if (localSocket == NULL) {
        fprintf(stderr, "Error creating my socket (%s:%d)!\n", my_addr, localPort);
        return -1;
    }
#ifdef DEBUG
    fprintf(stderr, "net helper successfully initialized\n");
#endif

    // initialize PeerSampler
    peersampleContext = psample_init(localSocket, NULL, 0, configPeersample);
    if (peersampleContext == NULL) {
        fprintf(stderr, "Error while initializing the peer sampler\n");
        return -1;
    }
#ifdef DEBUG
    fprintf(stderr, "PeerSampler successfully initialized\n");
#endif


    // initialize ChunkBuffer
    chunkBuffer = (struct ChunkBuffer*) cb_init(configChunkBuffer);
    if (chunkBuffer == NULL) {
        fprintf(stderr, "Error while initializing chunk buffer\n");
        return -1;
    }
#ifdef DEBUG
    fprintf(stderr, "ChunkBuffer successfully initialized\n");
#endif

    // initialize chunk delivery
    if (chunkDeliveryInit(localSocket) < 0) {
        fprintf(stderr, "Error while initializing chunk delivery\n");
        return -1;
    }
#ifdef DEBUG
    fprintf(stderr, "chunk delivery successfully initialized\n");
#endif

    // init chunk signaling
    if (chunkSignalingInit(localSocket) == 0) {
        fprintf(stderr, "Error while initializing chunk signaling\n");
        return -1;
    }
#ifdef DEBUG
    fprintf(stderr, "chunk signaling successfully initialized\n");
#endif

    // init chunkid set
    chunkIDSet = (struct ChunkIDSet*) chunkID_set_init(configChunkIDSet);
    if (chunkIDSet == NULL) {
        fprintf(stderr, "Error while initializing chunkid set\n");
        return -1;
    }
#ifdef DEBUG
    fprintf(stderr, "chunkid set successfully initialized\n");
#endif

    // init peerset
    peerSet = (struct PeerSet*) peerset_init(0);
    if (peerSet == NULL) {
        fprintf(stderr, "Error while initializing chunkid set\n");
        return -1;
    }
#ifdef DEBUG
    fprintf(stderr, "peerset successfully initialized\n");
#endif

    // set server
    serverSocket = create_node(serverAddress, serverPort);
    if(serverSocket == NULL) {
        fprintf(stderr, "server socket could not be initialized\n");
        return -1;
    }
#ifdef DEBUG
    fprintf(stderr, "server socket successfully created (%s:%d)\n", serverAddress, serverPort);
#endif

    // add server node to peersampler
    struct nodeID *s;
    s = create_node(serverAddress, serverPort);
    psample_add_peer(peersampleContext, s, NULL, 0);
#ifdef DEBUG
    fprintf(stderr, "server socket successfully added to peersampler context\n");
#endif

    // initialize output
    output = output_init(configOutput);
    if (output == NULL) {
        fprintf(stderr, "Error occurred: see message above.\n");
        return -1;
    }
#ifdef DEBUG
    fprintf(stderr, "output successfully initialized\n");
#endif

    return 0;
}

/*
 * 
 */
int main(int argc, char* argv[]) {

    // parse command line arguments
    parseCommandLineArguments(argc, argv);

    // initialization
    if (init(configInterface, configServerAddress, configServerPort, configPort) != 0) {
        fprintf(stderr, "Error occurred. Please see message above for further details.\n");
        return 0;
    }

    // threads
    threads_start();

    return 0;
}

