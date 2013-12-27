/* 
 * File:   output_interface.h
 * Author: tobias
 *
 * Created on 10. Dezember 2013, 14:07
 */

#ifndef OUTPUT_INTERFACE_H
#define	OUTPUT_INTERFACE_H

#include <stdio.h>
#include <stdint.h>

// GRAPES
#include <chunk.h>

struct output_context;

struct output_interface {
    struct output_context *(*init)(const char *config);
    int (*deliver)(struct output_context *context, struct chunk *c);
    void (*close)(struct output_context *context);
    int (*deliver_secured_data_chunk)(struct output_context *context, struct chunk *securedData);
    int (*deliver_secured_data_login)(struct output_context *context, struct chunk *securedData);
    int (*secure_data_enabled_chunk)(struct output_context *context);
    int (*secure_data_enabled_login)(struct output_context *context);
};

#endif	/* OUTPUT_INTERFACE_H */

