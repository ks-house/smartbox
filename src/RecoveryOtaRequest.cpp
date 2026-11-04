#include "RecoveryOtaRequest.h"
#include <cstring>
bool validateRecoveryOtaRequest(const char* id,uint64_t expires,uint64_t now,uint32_t& ttl) {
  ttl=0;if(!id || std::strlen(id)!=32 || now<1700000000ULL || expires<=now || expires-now>120)return false;
  for(unsigned i=0;i<32;++i)if(!((id[i]>='0'&&id[i]<='9')||(id[i]>='a'&&id[i]<='f')))return false;
  ttl=static_cast<uint32_t>((expires-now)*1000);return true;
}
