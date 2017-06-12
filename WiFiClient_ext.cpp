/* This code adds functionality that is available only in the ESP8266
   2.4.0 API back into 2.3.0.
   It is used to directly access private variables inside a class while
   still beng fully legal C++
 */

extern "C" {
  #include "user_interface.h"
}
#ifdef STAILQ_NEXT // ! //HAS_ESP8266_24

#define LWIP_INTERNAL
#include "WiFiClient.h"
#include "lwip/tcp.h"
#include "include/ClientContext.h"
/************************************************************************/
template<typename Tag>
struct result {
  /* export it ... */
  typedef typename Tag::type type;
  static type ptr;
};

template<typename Tag>
typename result<Tag>::type result<Tag>::ptr;

template<typename Tag, typename Tag::type p>
struct rob : result<Tag> {
  /* fill it ... */
  struct filler {
    filler() { result<Tag>::ptr = p; }
  };
  static filler filler_obj;
};

template<typename Tag, typename Tag::type p>
typename rob<Tag, p>::filler rob<Tag, p>::filler_obj;
/************************************************************************/
class ClientContext;

struct WifiClient_CC { typedef ClientContext * WiFiClient::*type; };
template class rob<WifiClient_CC, &WiFiClient::_client>;
struct ClientContext_pcb { typedef tcp_pcb* ClientContext::*type; };
template class rob<ClientContext_pcb, &ClientContext::_pcb>;

tcp_pcb* get_pcb(WiFiClient *client) {
    ClientContext *cc = client->*result<WifiClient_CC>::ptr;
    tcp_pcb *pcb = cc->*result<ClientContext_pcb>::ptr;
    return pcb;
}

size_t available_for_write(WiFiClient *client) {
    tcp_pcb *_pcb = get_pcb(client);
    return _pcb? tcp_sndbuf(_pcb): 0;
}


#endif //HAS_ESP8266_24
