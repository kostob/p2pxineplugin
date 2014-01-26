/* 
 * File:   output.c
 * Author: tobias
 *
 * Created on 01. December 2013, 11:06
 * 
 * originally from peerstreamer (Streamers/output-grapes.c)
 * ring buffer stuff from xine plugin input_rtp.c
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

// GRAPES
#include <chunk.h>
#include <chunkbuffer.h>
#include <chunkiser.h>
#include <config.h>

#include "output_ffmpeg.h"
#include "output_interface.h"
#include "network.h"

#define TIMEOUT 2 // timeout between sending request a chunk and discarding the chunk in seconds

struct output_buffer {
    struct chunk c;
};

extern int firstChunk;

struct output_context {
    int outputBufferSize;
    struct output_buffer *outputBuffer;
    struct output_stream *outputStream;
    int playedChunk; // last played chunk
    int maximumChunk; // maximum chunk received
    struct timeval *lastRequestSent; // time when request for chunks was send to the server/peers

    uint8_t securedDataLogin;

    // plugin data
    p2p_input_plugin_t *plugin;

    // not needed anymore    
    int lastChunk;
    char sflag;
    char eflag;
    int startId;
    int endId;
};

/*
 * helper functions START
 * 
 * from GRAPES/src/Chunkizer/payload.h
 */
static inline uint32_t int_rcpy(const uint8_t *p) {
    uint32_t tmp;

    memcpy(&tmp, p, 4);
    tmp = ntohl(tmp);

    return tmp;
}

static inline uint16_t int16_rcpy(const uint8_t *p) {
    uint16_t tmp;

    memcpy(&tmp, p, 2);
    tmp = ntohs(tmp);
    return tmp;
}

/*
 * helper function END
 */

/**
 * Initialize output
 * 
 * original sources from peerstreamer
 */
struct output_context *output_ffmpeg_init(p2p_input_plugin_t *plugin, const char *config) {
    struct output_context *context;
    context = (struct output_context*) malloc(sizeof (struct output_context));

    // predefine context
    context->plugin = plugin;
    context->lastChunk = -1;
    context->outputBuffer = NULL;
    context->outputBufferSize;
    context->playedChunk = -1;
    context->maximumChunk = -1;
    context->lastRequestSent = NULL;

    // not needed anymore
    context->sflag = 0;
    context->eflag = 0;
    context->startId = -1;
    context->endId = -1;


    struct tag *configTags;
    configTags = config_parse(config);
    if (configTags == NULL) {
        fprintf(stderr, "Error: parsing of config failed [output_ffmpeg_init]\n");
        free(configTags);
        free(context);
        return NULL;
    }

    config_value_int_default(configTags, "buffer", &context->outputBufferSize, 75);

    context->outputStream = out_stream_init("", config); // output goes to /dev/stdout

    free(configTags); // configTags not needed anymore

    if (context->outputStream == NULL) {
        fprintf(stderr, "Error: can't initialize output module\n");
        free(context);
        return NULL;
    }

    if (context->outputBuffer == NULL) {
        int i;

        context->outputBuffer = malloc(sizeof (struct output_buffer) * context->outputBufferSize);
        if (!context->outputBuffer) {
            fprintf(stderr, "Error: can't allocate output buffer\n");
            free(context->outputBuffer);
            free(context);
            return NULL;
        }
        for (i = 0; i < context->outputBufferSize; ++i) {
            context->outputBuffer[i].c.data = NULL;
        }
    } else {
        fprintf(stderr, "Error: output already initialized!\n");
        free(context->outputBuffer);
        free(context);
        return NULL;
    }

    return context;
}

/**
 * print output buffer
 * 
 * original sources from peerstreamer
 */
static void output_ffmpeg_buffer_print(struct output_context *context, int currentChunkId) {
#ifdef DEBUG
    int i;

    if (firstChunk < 0) {
        return;
    }

    fprintf(stderr, "DEBUG: outputBuffer: currentChunkId %d", currentChunkId);
    for (i = 0; i < context->outputBufferSize; ++i) {
        if (i % 10 == 0) {
            fprintf(stderr, "\n\t");
        }
        if (context->outputBuffer[i].c.data) {
            fprintf(stderr, "%5d ", context->outputBuffer[i].c.id);
        } else {
            fprintf(stderr, "----- ");
        }
    }
    fprintf(stderr, "\n");
#endif
}

/**
 * free output buffer
 * 
 * original sources from peerstreamer
 */
static void output_ffmpeg_buffer_free_original(struct output_context *context, int i) {
#ifdef DEBUG
    fprintf(stderr, "DEBUG: \t\tFlush outputBuffer %d: %s\n", i, context->outputBuffer[i].c.data);
#endif
    if (context->startId == -1 || context->outputBuffer[i].c.id >= context->startId) {
        if (context->endId == -1 || context->outputBuffer[i].c.id <= context->endId) {
            if (context->sflag == 0) {
#ifdef DEBUG
                fprintf(stderr, "DEBUG: First chunk id played out: %d\n\n", context->outputBuffer[i].c.id);
#endif
                context->sflag = 1;
            }

            output_ffmpeg_write_chunk(context, &context->outputBuffer[i].c);

            context->lastChunk = context->outputBuffer[i].c.id;
        } else if (context->eflag == 0 && context->lastChunk != -1) {
#ifdef DEBUG
            fprintf(stderr, "DEBUG: Last chunk id played out: %d\n\n", context->lastChunk);
#endif
            context->eflag = 1;
        }
    }

    free(context->outputBuffer[i].c.data);
    context->outputBuffer[i].c.data = NULL;
#ifdef DEBUG
    fprintf(stderr, "DEBUG: Next Chunk: %d -> %d\n", firstChunk, context->outputBuffer[i].c.id + 1);
#endif
    //reg_chunk_playout(outputBuffer[i].c.id, true, outputBuffer[i].c.timestamp);
    firstChunk = context->outputBuffer[i].c.id + 1;
}

/**
 * free buffer
 * 
 * @param context
 * @param i chunk id
 * @return 0 on success, 1 if time limit not reached and -1 if chunk is skipped
 */
static int output_ffmpeg_buffer_free(struct output_context *context, int i) {
    if (context->outputBuffer[i].c.data == NULL) {
        // check if there was a request before...
        if (context->lastRequestSent == NULL) {
            // no request before
#ifdef DEBUG
            fprintf(stderr, "DEBUG: chunk is missing [%d]. sending request...\n", context->outputBuffer[i].c.id);
#endif
            network_ask_for_chunk(context->playedChunk + i); // request chunk
            gettimeofday(context->lastRequestSent, NULL);
        } else {
            // check if time is over...
            struct timeval currentTime;
            gettimeofday(&currentTime, NULL);
            if (currentTime.tv_sec > context->lastRequestSent->tv_sec + TIMEOUT) {
                // skip chunk...
#ifdef DEBUG
                fprintf(stderr, "DEBUG: timeout reached -> skip chunk [%d]\n", i);
#endif
                context->playedChunk = i;

                // reset request time
                free(context->lastRequestSent);
                context->lastRequestSent = NULL;

                return -1;
            } else {
                // send a request (even for following chunks)...
#ifdef DEBUG
                fprintf(stderr, "DEBUG: chunk is missing [%d]. sending request...\n", context->outputBuffer[i].c.id);
#endif

                network_ask_for_chunk(context->playedChunk + i);

                return 1;
            }
        }
    } else {
#ifdef DEBUG
        fprintf(stderr, "DEBUG: writing chunk to plugin buffer [%d]\n", i);
#endif
        output_ffmpeg_write_chunk(context, &context->outputBuffer[i].c);
        // free space
        free(context->outputBuffer[i].c.data);
        context->outputBuffer[i].c.data = NULL;
    }
}

/**
 * flush output buffer
 * 
 * original sources from peerstreamer
 */
static void output_ffmpeg_buffer_flush_original(struct output_context *context, int id) {
    int i = id % context->outputBufferSize;

    while (context->outputBuffer[i].c.data) {
        output_ffmpeg_buffer_free(context, i);
        i = (i + 1) % context->outputBufferSize;
        if (i == id % context->outputBufferSize) {
            break;
        }
    }
}

static void output_ffmpeg_buffer_flush(struct output_context *context) {
    // start at context->playedChunk + 1 and run till maximum chunk
    int i;

    // run till end
    for (i = (context->playedChunk % context->outputBufferSize) + 1; i < context->outputBufferSize && context->outputBuffer[i].c.data != NULL; ++i) {
        if (output_ffmpeg_buffer_free(context, i) == -1) {
            return;
        }
    }

    // run from start
    for (i = 0; i < context->playedChunk % context->outputBufferSize && context->outputBuffer[i].c.data != NULL; ++i) {
        if (output_ffmpeg_buffer_free(context, i) == -1) {
            return;
        }
    }
}

static void output_ffmpeg_buffer_cleanup(struct output_context *context, int position) {
    int i;
    int lastPlayed = context->playedChunk;

    for (i = lastPlayed % context->outputBufferSize; i < position && i < context->outputBufferSize; ++i) {
#ifdef DEBUG
        fprintf(stderr, "DEBUG: cleanup output buffer %d\n", i);
#endif
        if (context->outputBuffer[i].c.data) {
            output_ffmpeg_write_chunk(context, context->outputBuffer[i].c);
            context->playedChunk = context->outputBuffer[i].c.id;
            free(context->outputBuffer[i].c.data);
            context->outputBuffer[i].c.data = NULL;
        }
    }

    // wrap?
    if (position < lastPlayed % context->outputBufferSize) {
        for (i = 0; i < position; ++i) {
#ifdef DEBUG
            fprintf(stderr, "DEBUG: cleanup output buffer %d\n", i);
#endif
            if (context->outputBuffer[i].c.data) {
                output_ffmpeg_write_chunk(context, context->outputBuffer[i].c);
                context->playedChunk = context->outputBuffer[i].c.id;
                free(context->outputBuffer[i].c.data);
                context->outputBuffer[i].c.data = NULL;
            }
        }
    }
}

/**
 * handle incomming chunk
 * 
 * returns 0 on success, -1 on error
 * 
 * original sources from peerstreamer
 */
int output_ffmpeg_deliver(struct output_context *context, struct chunk *c) {
    if (!context->outputBuffer) {
        fprintf(stderr, "Warning: code should use output_init!\n");
        return -1;
    }

#ifdef DEBUG
    fprintf(stderr, "DEBUG: Chunk %d delivered\n", c->id);
#endif

    // chunk not needed anymore: newer chunks already written...
    if (c->id <= context->playedChunk) {
        return 0;
    }

    // maximum chunk?
    if (context->maximumChunk < c->id) {
        context->maximumChunk = c->id;
    }

    // ckeck if first chunk
    if (c->id == firstChunk) {
#ifdef DEBUG
        fprintf(stderr, "DEBUG: writing first chunk to plugin buffer: %d\n", c->id);
#endif
        output_ffmpeg_write_chunk(context, c);
        context->playedChunk = c->id;
        return 0;
    }

    // check if current chunk is next for writing out
    if (c->id == context->playedChunk + 1) {
#ifdef DEBUG
        fprintf(stderr, "DEBUG: writing chunk to plugin buffer %d\n", c->id);
#endif
        output_ffmpeg_write_chunk(context, c);
        context->playedChunk = c->id;
        return 0;
    }

    // chunk is not the next needed chunk, we must save it in output buffer
#ifdef DEBUG
    fprintf(stderr, "DEBUG: chunk [%d] is not the next [%d] for plugin buffer (maximum [%d])\n", c->id, (context->playedChunk + 1), context->maximumChunk);
#endif

    // check if there is space in output buffer for saving current chunk
    if (context->outputBuffer[c->id % context->outputBufferSize].c.data == NULL) {
#ifdef DEBUG
        fprintf(stderr, "DEBUG: writing chunk [%d] to output buffer [%d]\n", c->id, c->id % context->outputBufferSize);
#endif
        memcpy(&context->outputBuffer[c->id % context->outputBufferSize].c, c, sizeof (struct chunk));
        context->outputBuffer[c->id % context->outputBufferSize].c.data = malloc(c->size);
        memcpy(context->outputBuffer[c->id % context->outputBufferSize].c.data, c->data, c->size);

        // debugging output: printing complete output buffer
        output_ffmpeg_buffer_print(context, c->id);

        output_ffmpeg_buffer_flush(context);
    } else {
        // no space, clean it
#ifdef DEBUG
        fprintf(stderr, "DEBUG: output buffer full -> cleanup\n");
#endif
        output_ffmpeg_buffer_cleanup(context, c->id % context->outputBufferSize);
    }

    return 0;

    /*
        // check if there is enough space for saving the current chunk in output buffer
        if (c->id >= firstChunk + context->outputBufferSize) {
            // We might need some space for storing this chunk,
            // or the stored chunks are too old

            int i;

            for (i = firstChunk; i <= c->id - context->outputBufferSize; i++) {
                if (context->outputBuffer[i % context->outputBufferSize].c.data) {
                    output_ffmpeg_buffer_free(context, i % context->outputBufferSize);
                } else {
                    firstChunk++;
                }
            }
            output_ffmpeg_buffer_flush(context, firstChunk);
    #ifdef DEBUG
            fprintf(stderr, "DEBUG: Next is now %d, chunk is %d\n", firstChunk, c->id);
    #endif
        }

    #ifdef DEBUG
        fprintf(stderr, "DEBUG: received chunk [%d] == next chunk [%d]?\n", c->id, firstChunk);
    #endif
        if (c->id == firstChunk) {
    #ifdef DEBUG
            //fprintf(stderr, "\tOut Chunk[%d] - %d: %s\n", c->id, c->id % outputBufferSize, c->data);
            fprintf(stderr, "DEBUG: \tOut Chunk[%d] - %d\n", c->id, c->id % context->outputBufferSize);
    #endif

            if (context->startId == -1 || c->id >= context->startId) {
                if (context->endId == -1 || c->id <= context->endId) {
                    if (context->sflag == 0) {
    #ifdef DEBUG
                        fprintf(stderr, "DEBUG: First chunk id played out: %d\n", c->id);
    #endif
                        context->sflag = 1;
                    }

    #ifdef DEBUG
                    fprintf(stderr, "DEBUG: Writing chunk to plugin buffer: %d\n", c->id);
    #endif
                    output_ffmpeg_write_chunk(context, c);

                    context->lastChunk = c->id;
                } else if (context->eflag == 0 && context->lastChunk != -1) {
    #ifdef DEBUG
                    fprintf(stderr, "DEBUG: Last chunk id played out: %d\n\n", context->lastChunk);
    #endif
                    context->eflag = 1;
                }
            }
            ++firstChunk;
            output_ffmpeg_buffer_flush(context, firstChunk);
        } else {
    #ifdef DEBUG
            fprintf(stderr, "DEBUG: Storing %d in outputBuffer[%d]\n", c->id, c->id % context->outputBufferSize);
    #endif
            if (context->outputBuffer[c->id % context->outputBufferSize].c.data) {
                if (context->outputBuffer[c->id % context->outputBufferSize].c.id == c->id) {
                    // Duplicate of a stored chunk
    #ifdef DEBUG
                    fprintf(stderr, "DEBUG: Duplicate! chunkID: %d\n", c->id);
    #endif
                    //reg_chunk_duplicate();
                    return -1;
                }
                fprintf(stderr, "Crap!, chunkid:%d, storedid: %d\n", c->id, context->outputBuffer[c->id % context->outputBufferSize].c.id);
                exit(-1);
            }
            // We previously flushed, so we know that c->id is free
            memcpy(&context->outputBuffer[c->id % context->outputBufferSize].c, c, sizeof (struct chunk));
            context->outputBuffer[c->id % context->outputBufferSize].c.data = malloc(c->size);
            memcpy(context->outputBuffer[c->id % context->outputBufferSize].c.data, c->data, c->size);
        }

        return 0;
     */
}

/**
 * close output
 * 
 * original sources from peerstreamer
 */
void output_ffmpeg_close(struct output_context *context) {
    out_stream_close(context->outputStream);
}

/**
 * handle incomming secured data for single chunk
 */
int output_ffmpeg_deliver_secured_data_chunk(struct output_context *context, struct chunk *securedData) {
    // get p2p chunk data from chunkbuffer and combine with securedData
    return 0; // returns 0 on success, -1 on error
}

/**
 * handle secured data transmitted at login time
 */
int output_ffmpeg_deliver_secured_data_login(struct output_context *context, struct chunk *securedData) {
    memcpy(&context->securedDataLogin, securedData->data, sizeof (uint8_t));
    return 0; // return 0 on success, -1 on error
}

/**
 * Check if module uses secure data for chunks
 * 
 * returns 0 if not, 1 if secure data are used
 * 
 * @return 0 is secured data
 */
int output_ffmpeg_secured_data_enabled_chunk(struct output_context *context) {
    return 0;
}

/**
 * Check if module uses secure data for login
 * 
 * returns 0 if not, 1 if secure data are used
 * 
 * @return int
 */
int output_ffmpeg_secured_data_enabled_login(struct output_context *context) {
    return 0;
}

/**
 * write chunk data to output or some variable...
 */
int output_ffmpeg_write_chunk(struct output_context *context, struct chunk *c) {
    // if secured data are used:
    // make your custom calculations...

    //chunk_write(context->outputStream, c);

    /* originally from
     *  - GRAPES/src/Chunkizer/output-stream-raw.c
     *  - GRAPES/src/Chunkizer/payload.h
     */
    int header_size;
    int frames;
    int i;
    uint8_t codec;
    int offset;

    if (c->data[0] == 0) {
        fprintf(stderr, "Error! Strange chunk: %x!!!\n", c->data[0]);
        return;
    } else if (c->data[0] < 127) {
        int width, height, frame_rate_n, frame_rate_d;

        header_size = 1 + 2 + 2 + 2 + 2 + 1; // 1 Frame type + 2 width + 2 height + 2 frame rate num + 2 frame rate den + 1 number of frames
        //video_payload_header_parse(c->data, &codec, &width, &height, &frame_rate_n, &frame_rate_d);
        codec = c->data[0];
        width = int16_rcpy(c->data + 1);
        height = int16_rcpy(c->data + 3);
        frame_rate_n = int16_rcpy(c->data + 5);
        frame_rate_d = int16_rcpy(c->data + 7);
#ifdef DEBUG
        fprintf(stderr, "Chunk[%05d]: Frame size: %dx%d -- Frame rate: %d / %d\n", c->id, width, height, frame_rate_n, frame_rate_d);
#endif
    }


    frames = c->data[header_size - 1];
    for (i = 0; i < frames; i++) {
        int frame_size;
        int64_t pts, dts;

        int i;

        frame_size = 0;
        /* FIXME: Maybe this can use int_coding? */

        for (i = 0; i < 3; i++) {
            frame_size = frame_size << 8;
            frame_size |= c->data[i];
        }
        dts = int_rcpy(c->data + 3);
        if (c->data[7] != 255) {
            pts = dts + c->data[7];
        } else {
            pts = -1;
        }
        //      dprintf("Frame %d has size %d\n", i, frame_size);
    }
    offset = header_size + frames * (3 + 4 + 1); // 3 Frame size + 4 PTS + 1 DeltaTS


    pthread_mutex_lock(&context->plugin->buffer_ring_mutex);


    // copy data to ring buffer (from xine plugin "input_rtp.c")
    /* wait for enough space to write the whole of the recv'ed data */
    while ((context->plugin->buffer_max_size - context->plugin->buffer_count) < c->size) {

        printf("%s: there is not enough space in buffer for current chunk (free: %d | needed: %d)\n", LOG_MODULE, (context->plugin->buffer_max_size - context->plugin->buffer_count), c->size);

        struct timeval tv;
        struct timespec timeout;

        gettimeofday(&tv, NULL);

        timeout.tv_nsec = tv.tv_usec * 1000;
        timeout.tv_sec = tv.tv_sec + 2;

        if (pthread_cond_timedwait(&context->plugin->writer_cond, &context->plugin->buffer_ring_mutex, &timeout) != 0) {
            fprintf(stdout, "input_p2p: buffer ring not read within 2 seconds!\n");
        }
    }

    /* Now there's enough space to write some bytes into the buffer
     * determine how many bytes can be written. If the buffer wraps
     * around, write in two pieces: from the head pointer to the
     * end of the buffer and from the base to the remaining number
     * of bytes.
     */

    long buffer_space_remaining = BUFFER_SIZE - (context->plugin->buffer_put_ptr - context->plugin->buffer);

    if (buffer_space_remaining >= c->size - offset) {
        /* data fits inside the buffer */
        memcpy(context->plugin->buffer_put_ptr, c->data + offset, c->size - offset);
        context->plugin->buffer_put_ptr += c->size;
    } else {
        /* data wrapped around the end of the buffer */
        memcpy(context->plugin->buffer_put_ptr, c->data + offset, buffer_space_remaining);
        memcpy(context->plugin->buffer, c->data + offset + buffer_space_remaining, c->size - offset - buffer_space_remaining);
        context->plugin->buffer_put_ptr = &context->plugin->buffer[ c->size - offset - buffer_space_remaining ];
    }

    context->plugin->buffer_count += (c->size - offset);

    /* signal the reader that there is new data	*/
    pthread_cond_signal(&context->plugin->reader_cond);
    pthread_mutex_unlock(&context->plugin->buffer_ring_mutex);
}

struct output_interface output_ffmpeg = {
    .init = output_ffmpeg_init,
    .deliver = output_ffmpeg_deliver,
    .close = output_ffmpeg_close,
    .deliver_secured_data_chunk = output_ffmpeg_deliver_secured_data_chunk,
    .deliver_secured_data_login = output_ffmpeg_deliver_secured_data_login,
    .secured_data_enabled_chunk = output_ffmpeg_secured_data_enabled_chunk,
    .secured_data_enabled_login = output_ffmpeg_secured_data_enabled_login,
};
