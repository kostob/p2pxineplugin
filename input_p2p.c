#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG
#define LOG_MODULE "input_p2p"
#define LOG_VERBOSE

#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include <xine/input_plugin.h>

#include "streamer.h"
#include "threads.h"

#define BUFSIZE 1024
#define DEFAULT_HOST_PORT 55555
#define OWN_PORT 44444

typedef struct {
    input_plugin_t input_plugin;

    xine_stream_t *stream;

    char *host;
    int host_port;

    int fh;
    char *mrl;
    char buf[BUFSIZE];
    off_t curpos;

    char preview[MAX_PREVIEW_SIZE];
    off_t preview_size;

    char seek_buf[BUFSIZE];

    xine_t *xine;
} p2p_input_plugin_t;

typedef struct {
    input_class_t input_class;

    xine_t *xine;
    config_values_t *config;

} p2p_input_class_t;

typedef struct {
    p2p_input_plugin_t input_plugin;

    // streamer
    pthread_mutex_t chunkBufferMutex; // for chunkbuffer and chunkIDSet
    pthread_mutex_t topologyMutex; // for peersampler
    pthread_mutex_t peerChunkMutex; // for peerChunk

    int stopThreads;
    int transId;
} p2p_streammer_data;

extern char* configServerAddress;
extern int configServerPort;
extern char* configInterface;
extern int configPort;

/**
 * Retrieves the autoplay playlist from the plugin. This function is optional.
 * 
 * @param this_gen
 * @param num_files
 * @return 
 */
static char **p2p_get_autoplay_list(input_class_t *this_gen, int *num_files);

/**
 * This function frees the memory used by the input plugin class object.
 * 
 * @param this_gen
 */
static void p2p_class_dispose(input_class_t *this_gen) {
    p2p_input_class_t *this = (p2p_input_class_t *) this_gen;
    // frees all memory used by the class
    free(this);
}

/**
 * You should do any device-specific initialisation within this function.
 * 
 * @param this_gen
 * @return 
 */
static int p2p_plugin_open(input_plugin_t *this_gen) {
    p2p_input_plugin_t *this = (p2p_input_plugin_t *) this_gen;
    p2p_input_class_t *this_class = (p2p_input_class_t *) this->input_plugin.input_class;

    // initialize the streamer
    //printf("%s: Initialize streamer...\n", LOG_MODULE);
    //memmove(configInterface, "lo0", strlen("lo0") + 1); printf("%s: interface=%s\n", LOG_MODULE, configInterface);
    //memmove(configServerAddress, this->host, strlen(this->host) + 1); printf("%s: server address=%s\n", LOG_MODULE, configServerAddress);
    //configServerPort = this->host_port; printf("%s: server port=%d\n", LOG_MODULE, configServerPort);
    //configPort = OWN_PORT; printf("%s: own port=%d\n", LOG_MODULE, configPort);
    
    printf("%s: interface=%s\n", LOG_MODULE, "lo0");
    printf("%s: server address=%s\n", LOG_MODULE, this->host);
    printf("%s: server port=%d\n", LOG_MODULE, this->host_port);
    printf("%s: own port=%d\n", LOG_MODULE, OWN_PORT);
    
    if(init("lo0", this->host, this->host_port, OWN_PORT) == -1) {
        return 0;
    }

    // start streamer threads
    threads_start();

    return 1;
}

/**
 * Returns a bit mask describing the input device's capabilities. You may logically
 * OR the INPUT_CAP_* constants together to get a suitable bit-mask (via the '|' operator).
 * 
 * @param this_gen
 * @return 
 */
static uint32_t p2p_plugin_get_capabilities(input_plugin_t *this_gen) {
    return INPUT_CAP_PREVIEW;
}

/**
 * Reads a specified number of bytes into a buffer and returns the number of bytes
 * actually copied.
 * 
 * @param this_gen
 * @param buf
 * @param nlen
 * @return 
 */
static off_t p2p_plugin_read(input_plugin_t *this_gen, char *buf_gen, off_t nlen) {
    p2p_input_plugin_t *this = (p2p_input_plugin_t *) this_gen;
    char *buf = (char *) buf_gen;
    off_t n, total;

    lprintf("reading %"PRId64" bytes...\n", nlen);
    if (nlen < 0)
        return -1;

    total = 0;
    if (this->curpos < this->preview_size) {
        n = this->preview_size - this->curpos;
        if (n > (nlen - total))
            n = nlen - total;
        lprintf("%"PRId64" bytes from preview (which has %"PRId64" bytes)\n", n, this->preview_size);

        memcpy(&buf[total], &this->preview[this->curpos], n);
        this->curpos += n;
        total += n;
    }

    if ((nlen - total) > 0) {
        n = _x_io_file_read(this->stream, this->fh, &buf[total], nlen - total);

        lprintf("got %"PRId64" bytes (%"PRId64"/%"PRId64" bytes read)\n", n, total, nlen);

        if (n < 0) {
            _x_message(this->stream, XINE_MSG_READ_ERROR, NULL);
            return 0;
        }

        this->curpos += n;
        total += n;
    }
    return total;
}

/**
 * Should the input plugin set the block-oriented hint and if the demuxer supports
 * it, this function will be called to read a block directly into a xine buffer
 * from the buffer pool.
 * 
 * @param this_gen
 * @param fifo
 * @param len
 * @return 
 */
static buf_element_t *p2p_plugin_read_block(input_plugin_t *this_gen, fifo_buffer_t *fifo, off_t len) {
    off_t total_bytes;
    /* stdin_input_plugin_t  *this = (stdin_input_plugin_t *) this_gen; */
    buf_element_t *buf = fifo->buffer_pool_alloc(fifo);

    if (len > buf->max_size)
        len = buf->max_size;
    if (len < 0) {
        buf->free_buffer(buf);
        return NULL;
    }

    buf->content = buf->mem;
    buf->type = BUF_DEMUX_BLOCK;

    total_bytes = p2p_plugin_read(this_gen, (char*) buf->content, len);

    if (total_bytes != len) {
        buf->free_buffer(buf);
        return NULL;
    }

    buf->size = total_bytes;

    return buf;
}

/**
 * This function is called by xine when it is required that subsequent reads come
 * from another part of the stream.
 * 
 * @param this_gen
 * @param offset
 * @param origin
 * @return 
 */
static off_t p2p_plugin_seek(input_plugin_t *this_gen, off_t offset, int origin) {
    p2p_input_plugin_t *this = (p2p_input_plugin_t *) this_gen;

    lprintf("seek %"PRId64" offset, %d origin...\n", offset, origin);

    if ((origin == SEEK_CUR) && (offset >= 0)) {

        for (; ((int) offset) - BUFSIZE > 0; offset -= BUFSIZE) {
            if (this_gen->read(this_gen, this->seek_buf, BUFSIZE) <= 0)
                return this->curpos;
        }

        this_gen->read(this_gen, this->seek_buf, offset);
    }

    if (origin == SEEK_SET) {

        if (offset < this->curpos) {

            if (this->curpos <= this->preview_size)
                this->curpos = offset;
            else
                xprintf(this->xine, XINE_VERBOSITY_LOG,
                    _("stdin: cannot seek back! (%" PRIdMAX " > %" PRIdMAX ")\n"),
                    (intmax_t) this->curpos, (intmax_t) offset);

        } else {
            offset -= this->curpos;

            for (; ((int) offset) - BUFSIZE > 0; offset -= BUFSIZE) {
                if (this_gen->read(this_gen, this->seek_buf, BUFSIZE) <= 0)
                    return this->curpos;
            }

            this_gen->read(this_gen, this->seek_buf, offset);
        }
    }

    return this->curpos;
}

/**
 * Returns the current position within a finite length stream.
 * 
 * @param this_gen
 * @return 
 */
static off_t p2p_plugin_get_current_pos(input_plugin_t *this_gen) {
    p2p_input_plugin_t *this = (p2p_input_plugin_t *) this_gen;

    return this->curpos;
}

/**
 * Similarly this function returns the length of the stream.
 * 
 * @param this_gen
 * @return 
 */
static off_t p2p_plugin_get_length(input_plugin_t *this_gen) {
    return 0;
}

/**
 * Returns the device's prefered block-size if applicable.
 * 
 * @param this_gen
 * @return 
 */
static uint32_t p2p_plugin_get_blocksize(input_plugin_t *this_gen) {
    return 0;
}

/**
 * Returns the current MRL.
 * 
 * @param this_gen
 * @return 
 */
static char *p2p_plugin_get_mrl(input_plugin_t *this_gen) {
    p2p_input_plugin_t *this = (p2p_input_plugin_t *) this_gen;

    return this->mrl;
}

/**
 * This function allows the input to advertise extra information that is not
 * available through other API functions. See INPUT_OPTIONAL_* defines.
 * 
 * @param this_gen
 * @param data
 * @param data_type
 * @return 
 */
static int p2p_plugin_get_optional_data(input_plugin_t *this_gen, void *data, int data_type) {
    p2p_input_plugin_t *this = (p2p_input_plugin_t *) this_gen;

    switch (data_type) {
        case INPUT_OPTIONAL_DATA_PREVIEW:

            memcpy(data, this->preview, this->preview_size);
            return this->preview_size;

            break;
    }

    return INPUT_OPTIONAL_UNSUPPORTED;
}

/**
 * This function closes all resources and frees the input_plugin_t object.
 * 
 * @param this_gen
 */
void p2p_plugin_dispose(input_plugin_t *this_gen) {
    p2p_input_plugin_t *this = (p2p_input_plugin_t *) this_gen;

    // free everythink in this
    if (this->mrl) free(this->mrl);
    // ...
    free(configInterface);
    free(configServerAddress);
    free(this);
}

/**
 * The plugin should try, if it can handle the specified MRL and return an instance
 * of itself if so. If not, NULL should be returned. When a new MRL is to be played,
 * xine engine asks all the available input plugins one by one if they can handle
 * the MRL. Note that input plugins are not guaranteed to be queried in any particular
 * order and the first input plugin to claim an MRL gets control so try not to
 * duplicate MRLs already found within xine.
 * 
 * @param class_gen
 * @param stream
 * @param mrl
 * @return 
 */
static input_plugin_t *p2p_class_get_instance(input_class_t *class_gen, xine_stream_t *stream, const char *mrl) {
    printf("%s: p2p_class_get_instance (START)\n", LOG_MODULE);

    p2p_input_class_t *class = (p2p_input_class_t *) class_gen;
    p2p_input_plugin_t *this;

    printf("%s: check if protocol is supported\n", LOG_MODULE);

    if (strncasecmp(mrl, "p2p://", 6) != 0) {
        printf("%s: protocol not supported", LOG_MODULE);
        return NULL;
    }

    printf("%s: calloc memory for input_plugin\n", LOG_MODULE);
    this = calloc(1, sizeof (p2p_input_plugin_t));

    this->mrl = strdup(mrl);

    this->stream = stream;
    this->fh = -1; // needed?
    this->xine = class->xine;

    ////////////////////
    // parse the mrl and write to host and host_port
    ////////////////////
    printf("%s: start parsing the mrl (%s) [size=%d]\n", LOG_MODULE, this->mrl, strlen(this->mrl));

    // copy everything without p2p:// to url
    int urlLength = strlen(this->mrl) + 1 - 6; // +1 for \0
    char *url = (char *) malloc(urlLength);
    printf("%s: malloc url (size=%d)\n", LOG_MODULE, urlLength);
    strncpy(url, this->mrl + 6, urlLength - 1);
    url[urlLength - 1] = '\0';
    printf("%s: url is %s\n", LOG_MODULE, url);

    // host
    char *colon = strchr(url, ':');
    int positionColon = (int) (colon - url);
    printf("%s: position of : is %d\n", LOG_MODULE, positionColon);

    this->host = (char*) malloc(positionColon + 1); // + 1 for \0
    strncpy(this->host, url, positionColon);
    this->host[positionColon] = '\0';
    printf("%s: host is %s\n", LOG_MODULE, this->host);

    // port
    if (positionColon) {
        // port
        char *port = malloc(strlen(url) - positionColon); // 1 more for \0
        strncpy(port, url + positionColon + 1, strlen(url) - positionColon);
        port[strlen(url) - positionColon] = '\0';
        this->host_port = atoi(port);
        printf("%s: port is %d\n", LOG_MODULE, this->host_port);
        free(port);
    } else {
        // use default port
        this->host_port = DEFAULT_HOST_PORT;
        printf("%s: using default port (%d)\n", LOG_MODULE, this->host_port);
    }

    free(colon);
    free(url);

    printf("%s: parsing mrl finished\n", LOG_MODULE);

    this->input_plugin.open = p2p_plugin_open;
    this->input_plugin.get_capabilities = p2p_plugin_get_capabilities;
    this->input_plugin.read = p2p_plugin_read;
    this->input_plugin.read_block = p2p_plugin_read_block;
    this->input_plugin.seek = p2p_plugin_seek;
    this->input_plugin.get_current_pos = p2p_plugin_get_current_pos;
    this->input_plugin.get_length = p2p_plugin_get_length;
    this->input_plugin.get_blocksize = p2p_plugin_get_blocksize;
    this->input_plugin.get_mrl = p2p_plugin_get_mrl;
    this->input_plugin.get_optional_data = p2p_plugin_get_optional_data;
    this->input_plugin.dispose = p2p_plugin_dispose;
    this->input_plugin.input_class = class_gen;

    return &this->input_plugin;
}

/**
 * This function initializes an input plugin class object
 * 
 * @param xine
 * @param data
 * @return 
 */
static void *p2p_init_class(xine_t *xine, void *data) {
    p2p_input_class_t *this;

    this = calloc(1, sizeof (p2p_input_class_t));

    this->xine = xine;
    this->config = xine->config;

    // set functions for xine access
    this->input_class.get_instance = p2p_class_get_instance;
    this->input_class.identifier = "p2p"; //p2p_class_get_identifier;
    this->input_class.description = N_("p2p input plugin"); //p2p_class_get_description;
    this->input_class.get_dir = NULL;
    this->input_class.get_autoplay_list = NULL;
    this->input_class.dispose = p2p_class_dispose;
    this->input_class.eject_media = NULL;

    return this;
}

const plugin_info_t xine_plugin_info[] = {
    /* type, API, "name", version, special_info, init_function */
    { PLUGIN_INPUT, 18, "p2p", XINE_VERSION_CODE, NULL, p2p_init_class},
    { PLUGIN_NONE, 0, "", 0, NULL, NULL}
};
