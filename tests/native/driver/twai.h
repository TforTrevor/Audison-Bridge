#pragma once
#include <Arduino.h>
#include <deque>
using esp_err_t=int;
constexpr int ESP_OK=0,TWAI_MODE_LISTEN_ONLY=1,TWAI_MODE_NORMAL=0;
struct twai_message_t { bool extd=false,rtr=false; uint32_t identifier=0; uint8_t data_length_code=0; uint8_t data[8]={}; };
struct twai_general_config_t {int rx_queue_len=0,tx_queue_len=0;};
struct twai_timing_config_t {};
struct twai_filter_config_t {};
struct twai_status_info_t {
  int state=1; uint32_t rx_missed_count=0,rx_overrun_count=0,bus_error_count=0;
  uint32_t rx_error_counter=0,tx_error_counter=0,msgs_to_rx=0;
};
inline int installError=0,startError=0;
inline std::deque<twai_message_t> incoming;
inline twai_status_info_t fakeStatus;
#define TWAI_GENERAL_CONFIG_DEFAULT(tx,rx,mode) twai_general_config_t{}
#define TWAI_TIMING_CONFIG_100KBITS() twai_timing_config_t{}
#define TWAI_FILTER_CONFIG_ACCEPT_ALL() twai_filter_config_t{}
inline int twai_driver_install(const twai_general_config_t*,const twai_timing_config_t*,const twai_filter_config_t*){return installError;}
inline int twai_start(){return startError;}
inline void twai_driver_uninstall(){}
inline int twai_get_status_info(twai_status_info_t *out){*out=fakeStatus;return 0;}
inline int twai_receive(twai_message_t *out,int){if(incoming.empty())return -1;*out=incoming.front();incoming.pop_front();return 0;}
inline const char *esp_err_to_name(int){return "SIMULATED_ERROR";}
