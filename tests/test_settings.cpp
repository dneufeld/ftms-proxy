#include "gradient_filter.h"
#include "settings.h"
#include "ftms_defs.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
int main() {
    Settings s; GradeFilter f;
    assert(valid_settings(s));
    assert(f.apply(-101,s,0)==-51);
    assert(f.apply(100,s,1)==100);
    s.climb=2; s.climb_cap=3; assert(f.apply(1000,s,2)==300);
    s.descent=2; assert(f.apply(-32768,s,3)==-32768);
    s.climb_cap=0; assert(f.apply(32767,s,4)==32767);
    s.enabled=false; assert(f.apply(-321,s,5)==-321);
    s.enabled=true; s.descent=.5; s.climb=1; s.crest_ms=1000;
    f.reset(); assert(f.apply(0,s,100)==0); assert(f.apply(-1000,s,101)==0);
    assert(f.apply(-1000,s,601)==-250); assert(f.apply(-1000,s,1101)==-500);
    f.reset(); const uint32_t start=0xfffffff0u;
    f.apply(0,s,start-1); assert(f.apply(-1000,s,start)==0);
    assert(f.apply(-1000,s,start+500u)==-250);
    assert(f.apply(-1000,s,start+1000u)==-500);
    s.descent=std::numeric_limits<float>::quiet_NaN(); assert(!valid_settings(s));
    s.descent=.5; s.climb_cap=31; assert(!valid_settings(s));
    s.climb_cap=0; s.crest_ms=10001; assert(!valid_settings(s));
    s.crest_ms=0; memset(s.target,'x',sizeof(s.target)); assert(!valid_settings(s));
    strcpy(s.target,"Tacx"); assert(valid_settings(s));
    uint8_t p[8]={0x11,0x34,0x12,0x18,0xfc,42,99,0xaa}; // -1000, wind 0x1234
    uint8_t original[8]; memcpy(original,p,8);
    assert(!f.process(p,8,s,0)); assert(memcmp(p,original,8)==0); // exact length
    f.reset(); assert(f.process(p,7,s,0)); assert(p[3]==0x0c && p[4]==0xfe);
    assert(p[1]==0x34 && p[2]==0x12 && p[5]==42 && p[6]==99 && p[7]==0xaa);
    assert(!f.process(nullptr,7,s,0));
    uint32_t random=17;
    for(int i=0;i<100000;i++) {
        random=random*1664525u+1013904223u;
        const int16_t grade=int16_t(random);
        s.descent=float((random>>16)%201)/100; s.climb=1; s.climb_cap=0; s.crest_ms=0;
        f.reset(); int16_t out=f.apply(grade,s,0);
        assert((grade<0 && out<=0) || (grade>=0 && out==grade));
        if(s.descent==1)assert(out==grade);
    }
    puts("Settings/filter tests PASS (including 100000 seeded grades)");
}
