/* 
 * File:   output_factory.c
 * Author: tobias
 *
 * Created on 10. Dezember 2013, 15:34
 */

#include <stdio.h>

#include "output_factory.h"

struct output_module *output_init(const char *config) {
    struct output_module *res;

    res = malloc(sizeof (struct output_module));
    if (res == NULL) {
        return NULL;
    }

    // HERE YOU HAVE TO CALL THE OUTPUT MODULE
    res->module = &output_ffmpeg;

    res->context = res->module->init(config);
    if (res->context == NULL) {
        return NULL;
    }

    return res;
}
