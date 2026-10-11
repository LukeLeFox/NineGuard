#include "NGProfileUAPI.h"
#include "NGUAPIClient.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
int main(int argc,char**argv){
  const char*sockA=argv[1],*sockB=argv[2],*port=argv[3],*info=argv[4],*trigger=argv[5];
  char privA[64]={0},pubB[64]={0}; FILE*f=NULL;
  for(int i=0;i<100&&!(f=fopen(info,"r"));i++) usleep(100000);
  if(!f){puts("NO_INFO");return 1;}
  fscanf(f,"%63s %63s",privA,pubB); fclose(f); unlink(info);
  char ep[64]; snprintf(ep,sizeof ep,"127.0.0.1:%s",port);
  ng_peer_input peer={pubB,NULL,ep,"0","10.253.91.1/32"};
  ng_profile_input in={privA,NULL,"10.253.91.2/32",&peer,1,NULL,NULL};
  static char uapi[NG_UAPI_CAPACITY]; ng_profile_plan plan;
  int r=ng_profile_build_uapi(&in,uapi,sizeof uapi,&plan); printf("BUILD=%d\n",r);
  int e=ng_uapi_set(sockA,uapi); printf("UAPI_SET_ERRNO=%d\n",e);
  ng_secure_zero(uapi,sizeof uapi);
  ng_uapi_stats st; e=ng_uapi_get_stats(sockA,&st);
  printf("GET_ERRNO=%d PEERS=%u PRIV=%d ALLOWED=%u ENDPOINTS=%u HS=%lld\n",e,st.peers,st.has_private_key,st.allowed_ips,st.endpoints,(long long)st.last_handshake_sec);
  e=ng_uapi_set(sockA,"set=1\nbogus=1\n\n"); printf("BAD_SET_ERRNO=%d\n",e);
  e=ng_uapi_set(sockA,"set=1\npublic_key=zz\n\n"); printf("BAD_KEY_ERRNO=%d\n",e);
  FILE*t=fopen(trigger,"w"); fclose(t);
  sleep(2);
  e=ng_uapi_get_stats(sockA,&st);
  long long age = st.last_handshake_sec? (long long)time(NULL)-st.last_handshake_sec : -1;
  printf("A_AFTER GET_ERRNO=%d PEERS=%u HS_AGE=%lld TX=%llu RX=%llu\n",e,st.peers,age,(unsigned long long)st.tx_bytes,(unsigned long long)st.rx_bytes);
  e=ng_uapi_get_stats(sockB,&st);
  printf("B_AFTER GET_ERRNO=%d PEERS=%u TX=%llu RX=%llu\n",e,st.peers,(unsigned long long)st.tx_bytes,(unsigned long long)st.rx_bytes);
  return 0;
}
