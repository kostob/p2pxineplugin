/* 
 * File:   output_factory.h
 * Author: tobias
 *
 * Created on 10. Dezember 2013, 09:10
 */

#ifndef OUTPUT_FACTORY_H
#define	OUTPUT_FACTORY_H

#include "output_interface.h"

// avaliable output modules
extern struct output_interface output_ffmpeg;
// here other implementations...


struct output_module {
    struct output_context *context;
    struct output_interface *module;
};

struct output_module *output_init(const char *config);

#endif	/* OUTPUT_FACTORY_H */
