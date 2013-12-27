/* 
 * File:   output_ffmpeg.h
 * Author: tobias
 *
 * Created on 26. November 2013, 08:17
 */

#ifndef OUTPUT_FFMPEG_H
#define	OUTPUT_FFMPEG_H

extern int nextChunk;
struct output_context;

struct output_context *output_ffmpeg_init(const char *config);
int output_ffmpeg_deliver(struct output_context *context, struct chunk *c);
void output_ffmpeg_close(struct output_context *context);
int output_ffmpeg_deliver_secured_data_chunk(struct output_context *context, struct chunk *securedData);
int output_ffmpeg_deliver_secured_data_login(struct output_context *context, struct chunk *securedData);
int output_ffmpeg_secured_data_enabled_chunk(struct output_context *context);
int output_ffmpeg_secured_data_enabled_login(struct output_context *context);

#endif	/* OUTPUT_H */

