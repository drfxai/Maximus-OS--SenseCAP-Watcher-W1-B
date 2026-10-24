#pragma once

#include <stdbool.h>

#include "ui/model.h"

/*
 * Camera app and gallery. One task owns the camera chip while the Camera page
 * is open (preview, photo, video) and the card while the Gallery is open
 * (listing, viewing, playing, deleting). The UI only sends commands.
 */
void mx_media_start(mx_model_t *model);

void mx_media_camera_open(void);
void mx_media_camera_close(void);
void mx_media_camera_shutter(void);
void mx_media_camera_set_video(bool video);

void mx_media_gallery_open(void);
void mx_media_gallery_show(int idx);
void mx_media_gallery_play(void);               /* toggles playback of the video shown */
bool mx_media_gallery_delete(int idx);
void mx_media_gallery_close(void);

bool mx_media_camera_active(void);              /* page open: the chip is in use */
bool mx_media_busy(void);                       /* preview, recording or playback: keep the screen on */
