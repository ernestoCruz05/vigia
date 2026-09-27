#ifndef VIGIA_AUDIO_H
#define VIGIA_AUDIO_H

#include <stdbool.h>

typedef struct VigiaAudio VigiaAudio;
typedef void (*VigiaAudioCallback)(unsigned int volume, bool muted, void *data);

VigiaAudio *vigia_audio_create(VigiaAudioCallback callback, void *data);
void vigia_audio_poll(VigiaAudio *audio);
void vigia_audio_destroy(VigiaAudio *audio);

#endif
