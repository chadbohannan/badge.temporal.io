#pragma once
#include "driver/rmt_types.h"
typedef struct rmt_encoder_t rmt_encoder_t;
typedef rmt_encoder_t* rmt_encoder_handle_t;
typedef int rmt_encode_state_t;
struct rmt_encoder_t {
  size_t (*encode)(rmt_encoder_t* encoder, rmt_channel_handle_t channel, const void* primary_data,
                   size_t data_size, rmt_encode_state_t* ret_state);
  esp_err_t (*reset)(rmt_encoder_t* encoder);
  esp_err_t (*del)(rmt_encoder_t* encoder);
};
