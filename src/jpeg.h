/*
 * jpeg.h
 *
 *  Created on: Mar 13, 2019
 *      Author: scvready
 */

#ifndef JPEG_H_
#define JPEG_H_

#include <string.h>
#include <FreeImage.h>

bool save_depth_frame_to_jpeg(uint16_t* depth_frame,char *filename);
bool save_video_frame_to_jpeg(uint16_t* video_frame,char *filename);
bool save_video_frame_to_jpeg_inmemory(uint16_t* video_frame, uint8_t* video_jpeg, uint32_t *size_bytes);
bool save_video_frames_to_gif(uint16_t** video_frames_array, int num_frames, float frame_interval, char *filename);


#endif /* JPEG_H_ */