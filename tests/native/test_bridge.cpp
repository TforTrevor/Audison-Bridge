#include "../../HyundaiAudisonBridge/HyundaiAudisonBridge.ino"
#include "../../HyundaiAudisonBridge/LogRing.h"
#include "../../HyundaiAudisonBridge/FirmwareUpload.h"
#include <cassert>
#include <iostream>

BridgeConsole Console;
bool maintenanceTest=false,debugStarted=false;
BridgeStats published;
size_t BridgeConsole::write(uint8_t value){return Serial.write(value);}
size_t BridgeConsole::write(const uint8_t *data,size_t size){return Serial.write(data,size);}
void debugInitConsole(){}
void debugBegin(){debugStarted=true;}
void debugPublishStats(const BridgeStats &stats){published=stats;}
bool debugMaintenanceActive(){return maintenanceTest;}

void reset(){
  requestedVolume=-1;requestedMute=false;muteStateKnown=false;lastCanMute=false;
  pendingCode=-1;lastSentCode=-1;lastSendMs=0;canReady=false;
  liveStats=BridgeStats{};lastStatsMs=0;fakeMs=100;
  packets.clear();usbLog.clear();incoming.clear();maintenanceTest=false;
  debugStarted=false;installError=0;startError=0;fakeStatus={};
}
twai_message_t volume(int level){twai_message_t f;f.identifier=0x183;f.data_length_code=1;f.data[0]=HYUNDAI_VOLUME_RAW[level];return f;}
twai_message_t mute(bool enabled){twai_message_t f;f.identifier=0x181;f.data_length_code=3;f.data[2]=enabled?1:0;return f;}
void expectPacket(uint8_t code){
  assert(!packets.empty());const auto &p=packets.back();
  assert(p.size()==7&&p[0]==1&&p[1]==4&&p[2]==0&&p[3]==1&&p[4]==code);
  assert((unsigned(p[5])<<8|p[6])==unsigned(6+code));
}
void mapping(){
  reset();
  for(int direction: {1,-1}) for(int k=0;k<=44;++k){
    int level=direction==1?k:44-k;
    processCanMessage(volume(level));fakeMs+=50;sendPendingVolume();
    expectPacket(uint8_t(0xA8-level*3));assert(requestedVolume==level);
  }
  const auto size=packets.size();const auto previous=requestedVolume;
  processCanMessage(volume(45));fakeMs+=50;sendPendingVolume();
  assert(packets.size()==size&&requestedVolume==previous);
  for(int raw=0;raw<256;++raw){
    if(decodeHyundaiVolume(uint8_t(raw))>=0)continue;
    auto f=volume(0);f.data[0]=uint8_t(raw);processCanMessage(f);
    assert(requestedVolume==previous);
  }
  for(int kind=0;kind<5;++kind){auto f=volume(20);
    if(kind==0)f.extd=true;if(kind==1)f.rtr=true;
    if(kind==2)f.data_length_code=0;if(kind==3)f.data_length_code=9;
    if(kind==4)f.identifier=0x777;
    processCanMessage(f);assert(requestedVolume==previous);
  }
}
void muteAndTiming(){
  reset();processCanMessage(volume(20));sendPendingVolume();expectPacket(0x6C);
  processCanMessage(mute(true));fakeMs+=50;sendPendingVolume();expectPacket(0xA8);
  const auto size=packets.size();processCanMessage(volume(20));fakeMs+=50;sendPendingVolume();
  assert(packets.size()==size&&requestedMute); // Broadcast of unchanged level stays muted.
  processCanMessage(volume(30));sendPendingVolume();expectPacket(0x4E);
  assert(packets.size()==size+1&&!requestedMute);
  processCanMessage(mute(true));fakeMs+=50;sendPendingVolume();
  assert(!requestedMute&&packets.size()==size+1); // Stale ON cannot reassert mute.
  processCanMessage(mute(false));processCanMessage(mute(true));sendPendingVolume();
  expectPacket(0xA8);assert(requestedMute); // A fresh mute transition still works.
  processCanMessage(mute(false));fakeMs+=50;sendPendingVolume();expectPacket(0x4E);
  reset();processCanMessage(mute(true));sendPendingVolume();expectPacket(0xA8);
  processCanMessage(mute(false));assert(pendingCode==-1);
  auto shortMute=mute(true);shortMute.data_length_code=2;processCanMessage(shortMute);assert(!requestedMute);
  reset();fakeMs=0;processCanMessage(volume(1));sendPendingVolume();assert(packets.empty());
  fakeMs=49;processCanMessage(volume(2));sendPendingVolume();assert(packets.empty());
  fakeMs=50;processCanMessage(volume(3));sendPendingVolume();expectPacket(0x9F);
  const auto n=packets.size();fakeMs=99;processCanMessage(volume(4));sendPendingVolume();assert(packets.size()==n);
  fakeMs=100;sendPendingVolume();expectPacket(0x9C);
  lastSendMs=0xfffffff0u;fakeMs=0x21;processCanMessage(volume(5));sendPendingVolume();assert(packets.size()==n+1);
  fakeMs=0x22;sendPendingVolume();expectPacket(0x99);
}
void coldBootMuteRecovery(){
  reset();processCanMessage(mute(true));sendPendingVolume();expectPacket(0xA8);
  processCanMessage(volume(15));assert(requestedMute&&requestedVolume==15);
  const auto size=packets.size();
  auto invalid=volume(0);invalid.data[0]=0xFF;
  processCanMessage(invalid);processCanMessage(volume(45));
  processCanMessage(volume(15));fakeMs+=50;sendPendingVolume();
  assert(requestedMute&&packets.size()==size);
  processCanMessage(volume(16));processCanMessage(mute(true));sendPendingVolume();
  expectPacket(0x78);assert(!requestedMute&&requestedVolume==16);
  assert(usbLog.find("CAN volume change: mute cleared")!=std::string::npos);
  processCanMessage(mute(false));processCanMessage(mute(true));
  fakeMs+=50;sendPendingVolume();expectPacket(0xA8);
  processCanMessage(volume(15)); // A decrease also clears mute and respects 50 ms.
  sendPendingVolume();expectPacket(0xA8);assert(!requestedMute);
  fakeMs+=50;sendPendingVolume();expectPacket(0x7B);
  publishStats();assert(!published.muted&&published.muteKnown&&published.volume==15);
  processCanMessage(mute(false));processCanMessage(mute(true));
  processCanMessage(volume(0));fakeMs+=50;sendPendingVolume();
  expectPacket(0xA8);assert(!requestedMute&&requestedVolume==0);
}
void maintenanceAndStartup(){
  reset();maintenanceTest=true;processCanMessage(volume(20));sendPendingVolume();assert(packets.empty());
  processCanMessage(volume(25));fakeMs+=50;sendPendingVolume();assert(packets.empty());
  maintenanceTest=false;sendPendingVolume();expectPacket(0x5D);
  reset();installError=-1;setup();assert(debugStarted&&!canReady&&!published.canReady);
  assert(usbLog.find("CAN install failed")!=std::string::npos);
  reset();startError=-1;setup();assert(debugStarted&&!canReady);
  reset();setup();assert(debugStarted&&canReady&&published.canReady);
  for(int i=0;i<70;++i)incoming.push_back(volume(i%45));
  loop();assert(incoming.size()==6&&liveStats.canFrames==64);
  fakeMs+=250;loop();assert(incoming.empty()&&published.canFrames==70);
  assert(published.serialPackets>0&&published.volume==24);
}
void ringHistory(){
  LogRing<3,8> ring;LogRing<3,8>::Entry batch[4];uint64_t missed=99;
  assert(ring.copyAfter(0,batch,4,missed)==0&&missed==0);
  ring.push(10,"first");ring.push(20,"second");ring.push(30,"third");
  assert(ring.copyAfter(0,batch,2,missed)==2&&batch[0].id==1&&batch[1].id==2);
  assert(ring.copyAfter(2,batch,4,missed)==1&&batch[0].id==3);
  ring.push(40,"fourth");ring.push(50,"0123456789");
  assert(ring.count()==3&&ring.oldest()==3&&ring.latest()==5);
  assert(ring.copyAfter(1,batch,4,missed)==3&&missed==1&&batch[0].id==3);
  assert(std::string(batch[2].text)=="0123456");
  assert(ring.copyAfter(5,batch,4,missed)==0);
  assert(ring.copyAfter(99,batch,4,missed)==3&&batch[0].id==3);
}
struct FakeUpdater {
  int begins=0,writes=0,ends=0,aborts=0;
  bool beginOk=true,writeOk=true,endOk=true;
  size_t expected=0;
  bool begin(size_t size){++begins;expected=size;return beginOk;}
  size_t write(uint8_t*,size_t size){++writes;return writeOk?size:0;}
  bool end(bool allowIncomplete){assert(!allowIncomplete);++ends;return endOk;}
  void abort(){++aborts;}
  const char *errorString(){return "Simulated flash error";}
};
void firmwareLifecycle(){
  uint8_t data[32]={0xE9};
  {
    FakeUpdater u;FirmwareUpload<FakeUpdater> f(u);
    assert(f.begin(32,64));assert(f.write(data,16));assert(f.write(data+16,16));
    assert(f.finishFile(32)&&f.ready()&&u.ends==0); // End-of-file never activates.
    assert(f.activate()&&!f.active()&&u.ends==1);f.reset();assert(u.aborts==0);
  }
  for(size_t invalid: {size_t(0),size_t(23),size_t(65)}){
    FakeUpdater u;FirmwareUpload<FakeUpdater> f(u);
    assert(!f.begin(invalid,64)&&u.begins==0&&u.ends==0);
  }
  {
    FakeUpdater u;FirmwareUpload<FakeUpdater> f(u);
    assert(f.begin(32,64)&&f.write(data,16));assert(!f.finishFile(32));
    assert(u.aborts==1&&!f.activate()&&u.ends==0);
  }
  {
    FakeUpdater u;FirmwareUpload<FakeUpdater> f(u);
    assert(f.begin(32,64));assert(!f.write(data,33)&&u.writes==0&&u.ends==0);
  }
  {
    FakeUpdater u;FirmwareUpload<FakeUpdater> f(u);
    assert(f.begin(32,64)&&f.write(data,32));assert(!f.finishFile(31)&&u.ends==0);
  }
  {
    FakeUpdater u;FirmwareUpload<FakeUpdater> f(u);
    assert(f.begin(32,64)&&f.write(data,32)&&f.finishFile(32));
    assert(!f.begin(32,64));assert(!f.activate()&&u.ends==0&&u.aborts==1);
  }
  {
    FakeUpdater u;FirmwareUpload<FakeUpdater> f(u);
    assert(f.begin(32,64)&&f.write(data,32)&&f.finishFile(32));
    f.reset();assert(!f.activate()&&u.ends==0&&u.aborts==1); // Disconnected request.
  }
  for(int failure=0;failure<3;++failure){
    FakeUpdater u;FirmwareUpload<FakeUpdater> f(u);
    if(failure==0){u.beginOk=false;assert(!f.begin(32,64));}
    else if(failure==1){assert(f.begin(32,64));u.writeOk=false;assert(!f.write(data,32));}
    else {assert(f.begin(32,64)&&f.write(data,32)&&f.finishFile(32));u.endOk=false;assert(!f.activate());}
    assert(!f.active()&&u.aborts==1);f.reset();u.beginOk=u.writeOk=u.endOk=true;
    assert(f.begin(32,64)&&f.write(data,32)&&f.finishFile(32)&&f.activate());
  }
}
int main(){mapping();muteAndTiming();coldBootMuteRecovery();maintenanceAndStartup();ringHistory();firmwareLifecycle();
  std::cout<<"PASS: actual bridge mapping, mute and volume-change unmute, cold-boot mute recovery, invalid CAN, command timing/wrap, OTA pause/resume, CAN-failure diagnostics, bounded receive loop, stats, log-ring pagination/eviction, and OTA lifecycle (size, truncation, duplicate image, disconnect, flash errors, retry, deferred activation)\n";
}
