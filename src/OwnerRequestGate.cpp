#include "OwnerRequestGate.h"
#include "RecoveryOtaRequest.h"
#include <cstring>
bool OwnerRequestGate::accept(const char* id,uint64_t expires,uint64_t now,bool retained,bool duplicate,uint32_t& ttl) {
 ttl=0;uint32_t candidate=0;
 if(retained || duplicate || expires<=watermark_ || count_>=8 || !validateRecoveryOtaRequest(id,expires,now,candidate))return false;
 for(unsigned i=0;i<count_;++i)if(std::strcmp(id,accepted_[i])==0)return false;
 std::memcpy(accepted_[count_++],id,33);watermark_=expires;ttl=candidate;return true;
}

bool acceptStoredOwnerRequest(OwnerRequestGate& gate,OwnerRequestStore& store,const char* id,uint64_t expiry,uint64_t now,bool retained,bool duplicate,uint32_t& ttl) {
 ttl=0;uint64_t previous=0;if(!store.load(previous))return false;
 gate.restoreWatermark(previous);
 if(!gate.accept(id,expiry,now,retained,duplicate,ttl))return false;
 if(!store.save(gate.expiryWatermark())){ttl=0;return false;}
 return true;
}
