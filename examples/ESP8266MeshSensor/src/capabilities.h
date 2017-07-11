#ifndef _CAPABILITIES_H_
#define _CAPABILITIES_H_

#ifdef DS18B20
    #define HAS_DS18B20 1
#else 
    #define HAS_DS18B20 0
#endif

#if defined(HLW8012_SEL) && defined(HLW8012_CF) && defined (HLW8012_CF1)
    #define HAS_HLW8012 1
#else
    #define HAS_HLW8012 0
#endif
#endif //_CAPABILITIES_H_
