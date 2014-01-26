/* 
 * File:   input_p2p.h
 * Author: tobias
 *
 * Created on 28. Dezember 2013, 16:32
 */

#ifndef INPUT_P2P_H
#define	INPUT_P2P_H

#include <stdio.h>
#include <stdint.h>
#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/input_plugin.h>

//#include "output_factory.h"

#define BUFFER_SIZE (1024*1024)
#define DEFAULT_HOST_PORT 55555
#define OWN_PORT 44444

typedef struct {
    input_plugin_t input_plugin;

    xine_stream_t *stream;

    char *interface;
    int own_port;
    char *host;
    int host_port;

    int fh;
    char *mrl;
    off_t curpos;

    uint8_t preview[MAX_PREVIEW_SIZE];
    off_t preview_size;
    int preview_read_done; /* boolean true after attempt to read input stream for preview */

    uint8_t *buffer; /* circular buffer */
    uint8_t *buffer_get_ptr; /* get pointer used by the reader */
    uint8_t *buffer_put_ptr; /* put pointer used by the writer */
    long buffer_count; /* number of bytes in the buffer */
    long buffer_max_size;

    pthread_mutex_t buffer_ring_mutex;
    pthread_cond_t writer_cond;
    pthread_cond_t reader_cond;
    
    // streamer
    struct output_module *output;

} p2p_input_plugin_t;

typedef struct {
    input_class_t input_class;

    xine_t *xine;
    config_values_t *config;

} p2p_input_class_t;

#endif	/* INPUT_P2P_H */

