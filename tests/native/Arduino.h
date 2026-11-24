#pragma once
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using std::min;
using std::max;
using gpio_num_t = int;
constexpr int GPIO_NUM_26=26, GPIO_NUM_25=25, HIGH=1, OUTPUT=1, SERIAL_8N1=0;
inline uint32_t fakeMs=0;
inline std::string usbLog;
inline std::vector<std::vector<uint8_t>> packets;
class Print {
public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t value) = 0;
  virtual size_t write(const uint8_t *data, size_t size) {
    for(size_t i=0;i<size;++i) write(data[i]); return size;
  }
  size_t print(const char *text) { return write(reinterpret_cast<const uint8_t*>(text), strlen(text)); }
  size_t println(const char *text) { return print(text)+print("\n"); }
  size_t printf(const char *format, ...) {
    char buffer[1024]; va_list args; va_start(args,format);
    int size=vsnprintf(buffer,sizeof(buffer),format,args); va_end(args);
    return write(reinterpret_cast<const uint8_t*>(buffer), min(size_t(max(size,0)),sizeof(buffer)-1));
  }
};
class HardwareSerial : public Print {
  int port_;
public:
  explicit HardwareSerial(int port):port_(port){}
  void begin(int,int=0,int=0,int=0){}
  size_t write(uint8_t value) override { return write(&value,1); }
  size_t write(const uint8_t *data,size_t size) override {
    if(port_==2) packets.emplace_back(data,data+size);
    else usbLog.append(reinterpret_cast<const char*>(data),size);
    return size;
  }
  void flush(){}
  int available(){return 0;}
  int read(){return -1;}
};
inline HardwareSerial Serial(0);
inline uint32_t millis(){return fakeMs;}
inline void delay(uint32_t ms){fakeMs+=ms;}
inline void digitalWrite(int,int){}
inline void pinMode(int,int){}
