module;
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

export module caudio.player:miniaudio_impl;

export ma_result caudio_miniaudio_decoder_init_with_tell(ma_decoder_read_proc onRead,
                                                         ma_decoder_seek_proc onSeek,
                                                         ma_decoder_tell_proc onTell,
                                                         void* pUserData,
                                                         const ma_decoder_config* pConfig,
                                                         ma_decoder* pDecoder) {
  ma_decoder_config config = ma_decoder_config_init_copy(pConfig);
  ma_result result = ma_decoder__preinit(onRead, onSeek, onTell, pUserData, &config, pDecoder);
  if (result != MA_SUCCESS) {
    return result;
  }
  result = ma_decoder_init__internal(onRead, onSeek, pUserData, &config, pDecoder);
  if (result != MA_SUCCESS) {
    return result;
  }
  return ma_decoder__postinit(&config, pDecoder);
}