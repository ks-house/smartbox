#ifndef SMARTBOX_OWNER_REQUEST_GATE_H
#define SMARTBOX_OWNER_REQUEST_GATE_H
#include <cstdint>
class OwnerRequestGate {
 public:
  bool accept(const char* id,uint64_t expires,uint64_t now,bool retained,bool duplicate,uint32_t& ttl);
  void restoreWatermark(uint64_t value) {if(value>watermark_)watermark_=value;}
  uint64_t expiryWatermark() const {return watermark_;}
 private:
  uint64_t watermark_=0;
  char accepted_[8][33]{};
  unsigned count_=0;
};
class OwnerRequestStore {
 public:
  virtual ~OwnerRequestStore()=default;
  virtual bool load(uint64_t& value)=0;
  virtual bool save(uint64_t value)=0;
};
bool acceptStoredOwnerRequest(OwnerRequestGate&,OwnerRequestStore&,const char*,uint64_t,uint64_t,bool,bool,uint32_t&);
#endif
