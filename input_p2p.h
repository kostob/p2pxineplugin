/* 
 * File:   input_p2p.h
 * Author: tobias
 *
 * Created on 28. Dezember 2013, 16:32
 */

#ifndef INPUT_P2P_H
#define	INPUT_P2P_H

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/input_plugin.h>

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
    char buf[BUFFER_SIZE];
    off_t curpos;

    char preview[MAX_PREVIEW_SIZE];
    off_t preview_size;
    int preview_read_done; /* boolean true after attempt to read input stream for preview */


    char seek_buf[BUFFER_SIZE];

    xine_t *xine;

    unsigned char *buffer; /* circular buffer */
    unsigned char *buffer_get_ptr; /* get pointer used by the reader */
    unsigned char *buffer_put_ptr; /* put pointer used by the writer */
    long buffer_count; /* number of bytes in the buffer */
    long buffer_max_size;

    unsigned char packet_buffer[65536];

    pthread_mutex_t buffer_ring_mutex;
    pthread_cond_t writer_cond;
    pthread_cond_t reader_cond;

} p2p_input_plugin_t;

typedef struct {
    input_class_t input_class;

    xine_t *xine;
    config_values_t *config;

} p2p_input_class_t;

#endif	/* INPUT_P2P_H */

