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

static char* configServerAddress = "127.0.0.1";
static int configServerPort = 6666;
static char* configInterface = "lo0";
static int configPort = 5555;
static char *configPeersample = "protocol=cyclon";
static char *configChunkBuffer = "size=100,time=now"; // size must be same value as chunkBufferSizeMax
static char *configChunkIDSet = "size=100"; // size must be same value as chunkBufferSizeMax
static char *configOutput = "buffer=200,dechunkiser=raw,payload=avf";
static int configOutputBufferSize = 200;

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

int init(p2p_input_plugin_t *plugin) { //char *interface, char *serverAddress, int serverPort, int localPort) {

#ifdef DEBUG
    fprintf(stderr, "DEBUG: Called streamer.c init with interface=%s, serverAddress=%s, serverPort=%d, localPort=%d\n", plugin->interface, plugin->host, plugin->host_port, plugin->own_port);
#endif

    // create the interface for connection
    char *my_addr = network_create_interface(plugin->interface);

    if (strlen(my_addr) == 0) {
        fprintf(stderr, "Cannot find network interface %s\n", plugin->interface);
        return -1;
    }
#ifdef DEBUG
    fprintf(stderr, "Created network interface successfully\n");
#endif

    // initialize net helper
    localSocket = net_helper_init(my_addr, plugin->own_port, "");
    if (localSocket == NULL) {
        fprintf(stderr, "Error creating my socket (%s:%d)!\n", my_addr, plugin->own_port);
        return -1;
    }
#ifdef DEBUG
    fprintf(stderr, "net helper successfully initialized\n");
#endif

    // initialize PeerSampler
    // trying to avoid the bug..
    char* sa = strdup(plugin->host);
    peersampleContext = psample_init(localSocket, NULL, 0, configPeersample); // BUG: THIS WILL REMOVE THE CONTENT OF "serverAddress"...
    if (peersampleContext == NULL) {
        fprintf(stderr, "Error while initializing the peer sampler (config: %s)\n", configPeersample);
        return -1;
    }
#ifdef DEBUG
    fprintf(stderr, "PeerSampler successfully initialized (config: %s)\n", configPeersample);
#endif
    plugin->host = strdup(sa);
    free(sa);

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
    serverSocket = create_node(plugin->host, plugin->host_port);
    if (serverSocket == NULL) {
        fprintf(stderr, "server socket could not be initialized (serverAddress=%s, serverPort=%d)\n", plugin->host, plugin->host_port);
        return -1;
    }
#ifdef DEBUG
    fprintf(stderr, "server socket successfully created (%s:%d)\n", plugin->host, plugin->host_port);
#endif

    // add server node to peersampler
    struct nodeID *s;
    s = create_node(plugin->host, plugin->host_port);
    if (psample_add_peer(peersampleContext, s, NULL, 0) == -1) {
        fprintf(stderr, "node (server) could not be added to peersampler\n");
        return -1;
    }
#ifdef DEBUG
    fprintf(stderr, "server socket successfully added to peersampler context\n");
#endif

    // initialize output
    output = output_init(plugin, configOutput);
    //plugin->output = output; // needed to close the output later
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
    if (init(NULL) != 0) {
        fprintf(stderr, "Error occurred. Please see message above for further details.\n");
        return 0;
    }

    // threads
    threads_start();

    return 0;
}

