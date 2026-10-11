#include "NGProfileUAPI.h"
#include "NGUAPIClient.h"
#include "NGNetWatch.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int fails;
#define CHECK(c) do{ if(!(c)){printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c);fails++;} }while(0)
int main(int argc,char**argv){
  char hex[65], out[128]; int fam; unsigned pre;
  CHECK(ng_b64key_to_hex("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=",hex)==0);
  CHECK(!strcmp(hex,"0000000000000000000000000000000000000000000000000000000000000000"));
  CHECK(ng_b64key_to_hex(argv[1],hex)==0); CHECK(!strcmp(hex,argv[2]));
  CHECK(ng_b64key_to_hex("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAB=",hex)!=0); /* non canonico */
  CHECK(ng_b64key_to_hex("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA==",hex)!=0);
  CHECK(ng_b64key_to_hex("AAAA",hex)!=0);
  CHECK(ng_b64key_to_hex("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA*A=",hex)!=0);
  CHECK(ng_normalize_cidr("10.0.0.1",out,sizeof out,&fam,&pre)==0 && !strcmp(out,"10.0.0.1/32") && fam==4 && pre==32);
  CHECK(ng_normalize_cidr("0.0.0.0/0",out,sizeof out,&fam,&pre)==0 && pre==0);
  CHECK(ng_normalize_cidr("fd00:0::1/64",out,sizeof out,&fam,&pre)==0 && !strcmp(out,"fd00::1/64") && fam==6);
  CHECK(ng_normalize_cidr("10.0.0.1/33",out,sizeof out,&fam,&pre)!=0);
  CHECK(ng_normalize_cidr("10.0.0.1/",out,sizeof out,&fam,&pre)!=0);
  CHECK(ng_normalize_cidr("host/32",out,sizeof out,&fam,&pre)!=0);
  char ip4[16];
  CHECK(ng_resolve_endpoint("192.0.2.1:51820",out,sizeof out,ip4)==0 && !strcmp(out,"192.0.2.1:51820") && !strcmp(ip4,"192.0.2.1"));
  CHECK(ng_resolve_endpoint("[2001:db8::1]:51820",out,sizeof out,ip4)==0 && !strcmp(out,"[2001:db8::1]:51820") && ip4[0]==0);
  CHECK(ng_resolve_endpoint("localhost:1",out,sizeof out,ip4)==0 && !strcmp(out,"127.0.0.1:1"));
  CHECK(ng_resolve_endpoint("192.0.2.1",out,sizeof out,ip4)!=0);
  CHECK(ng_resolve_endpoint("192.0.2.1:0",out,sizeof out,ip4)!=0);
  CHECK(ng_resolve_endpoint("192.0.2.1:70000",out,sizeof out,ip4)!=0);
  CHECK(ng_resolve_endpoint("2001:db8::1:51820",out,sizeof out,ip4)!=0);
  ng_peer_input peers[2]={{argv[1],NULL,"192.0.2.1:51820","25"," 198.51.100.7/32, 192.0.2.1/32 ,10.0.0.0/8, ::/0, 198.51.100.7"},
                          {argv[3],argv[1],NULL,"off","203.0.113.5"}};
  ng_profile_input in={argv[3],"51900","fd00::2/64, 10.253.91.2/24",peers,2,NULL,NULL};
  static char uapi[NG_UAPI_CAPACITY]; ng_profile_plan plan;
  int r=ng_profile_build_uapi(&in,uapi,sizeof uapi,&plan);
  CHECK(r==0);
  if(argc>4){FILE*f=fopen(argv[4],"w");fputs(uapi,f);fclose(f);}
  CHECK(plan.peer_count==2); CHECK(!strcmp(plan.ipv4_address,"10.253.91.2"));
  CHECK(plan.host_route_count==2); CHECK(!strcmp(plan.host_routes[0],"198.51.100.7")); CHECK(!strcmp(plan.host_routes[1],"203.0.113.5"));
  CHECK(plan.skipped_allowed_ips==2); /* endpoint, ::/0 */
  CHECK(plan.net_route_count==1 && !strcmp(plan.net_routes[0],"10.0.0.0") && plan.net_prefixes[0]==8);
  CHECK(!plan.full_ipv4 && plan.dns_count==0 && plan.search_count==0);
  CHECK(strstr(uapi,"persistent_keepalive_interval=25\n")!=NULL);
  CHECK(strstr(uapi,"allowed_ip=198.51.100.7/32\n")!=NULL);
  CHECK(uapi[strlen(uapi)-1]=='\n' && uapi[strlen(uapi)-2]=='\n');
  { /* full tunnel + DNS + rete non mascherata */
    ng_peer_input fp={argv[1],NULL,"192.0.2.1:51820",NULL,"0.0.0.0/0, ::/0, 10.1.2.3/16, 10.1.0.0/16"};
    ng_profile_input fi={argv[3],NULL,"10.253.92.2/32",&fp,1,"10.253.92.1, 2001:db8::53, corp.example, 1.1.1.1, bad_dom!",NULL};
    CHECK(ng_profile_build_uapi(&fi,uapi,sizeof uapi,&plan)==0);
    CHECK(plan.full_ipv4==1 && plan.host_route_count==0);
    CHECK(plan.net_route_count==1 && !strcmp(plan.net_routes[0],"10.1.0.0") && plan.net_prefixes[0]==16);
    CHECK(plan.dns_count==2 && !strcmp(plan.dns_servers[0],"10.253.92.1") && !strcmp(plan.dns_servers[1],"1.1.1.1"));
    CHECK(plan.search_count==1 && !strcmp(plan.search_domains[0],"corp.example"));
    CHECK(plan.skipped_allowed_ips==1); /* ::/0 */
    CHECK(strstr(uapi,"allowed_ip=0.0.0.0/0\n")!=NULL);
  }
  { /* MTU del profilo */
    ng_peer_input mp={argv[1],NULL,NULL,NULL,"10.9.0.0/16"};
    ng_profile_input mi={argv[3],NULL,"10.9.0.2/32",&mp,1,NULL,"1280"};
    CHECK(ng_profile_build_uapi(&mi,uapi,sizeof uapi,&plan)==0 && plan.mtu==1280);
    CHECK(strstr(uapi,"mtu")==NULL); /* non e' una chiave UAPI */
    mi.mtu=NULL; CHECK(ng_profile_build_uapi(&mi,uapi,sizeof uapi,&plan)==0 && plan.mtu==0);
    mi.mtu="575"; CHECK(ng_profile_build_uapi(&mi,uapi,sizeof uapi,&plan)==NG_PROFILE_ERR_MTU && plan.mtu==0 && uapi[0]==0);
    mi.mtu="1501"; CHECK(ng_profile_build_uapi(&mi,uapi,sizeof uapi,&plan)==NG_PROFILE_ERR_MTU);
    mi.mtu="12a0"; CHECK(ng_profile_build_uapi(&mi,uapi,sizeof uapi,&plan)==NG_PROFILE_ERR_MTU);
    mi.mtu="1500"; CHECK(ng_profile_build_uapi(&mi,uapi,sizeof uapi,&plan)==0 && plan.mtu==1500);
    CHECK(!strcmp(ng_profile_error_name(NG_PROFILE_ERR_MTU),"MTU"));
  }
  ng_peer_input bad={argv[1],NULL,NULL,NULL,"10.0.0.0/40"}; ng_profile_input inb={argv[3],NULL,NULL,&bad,1,NULL,NULL};
  CHECK(ng_profile_build_uapi(&inb,uapi,sizeof uapi,&plan)==NG_PROFILE_ERR_ALLOWED_IP && uapi[0]==0);
  ng_uapi_stats st;
  CHECK(ng_uapi_parse_get("private_key=ab\nlisten_port=5\npublic_key=cd\nlast_handshake_time_sec=10\ntx_bytes=7\nrx_bytes=9\nallowed_ip=1.2.3.4/32\npublic_key=ef\nlast_handshake_time_sec=12\ntx_bytes=1\nerrno=0\n\n",&st)==0);
  CHECK(st.peers==2 && st.tx_bytes==8 && st.rx_bytes==9 && st.last_handshake_sec==12 && st.has_private_key && st.listen_port==5 && st.allowed_ips==1);
  { /* watchdog cambio rete */
    ng_watch_input w={0,1,0x0a0a6301u,1,0x0a0a6301u,1};
    CHECK(ng_watch_decide(&w)==NG_WATCH_KEEP);
    w.kernel_matches=0; CHECK(ng_watch_decide(&w)==NG_WATCH_REPLACE);      /* route sparita */
    w.kernel_matches=1; w.router=0xac140a01u; CHECK(ng_watch_decide(&w)==NG_WATCH_REPLACE); /* nuovo gateway */
    w.have_router=0; CHECK(ng_watch_decide(&w)==NG_WATCH_NO_NETWORK);
    w.have_router=1; w.router=0; CHECK(ng_watch_decide(&w)==NG_WATCH_NO_NETWORK);
    w.router=0xac140a01u; w.recorded=0; CHECK(ng_watch_decide(&w)==NG_WATCH_ADD); /* era on-link */
    w.onlink=1; CHECK(ng_watch_decide(&w)==NG_WATCH_KEEP);
    w.recorded=1; CHECK(ng_watch_decide(&w)==NG_WATCH_REMOVE);
    w.have_router=0; CHECK(ng_watch_decide(&w)==NG_WATCH_REMOVE);
    CHECK(ng_watch_decide(NULL)==NG_WATCH_KEEP);
    long since=0;
    CHECK(!ng_watch_grace_expired(&since,1,100,30) && since==100);
    CHECK(!ng_watch_grace_expired(&since,1,129,30));
    CHECK(ng_watch_grace_expired(&since,1,130,30));
    CHECK(!ng_watch_grace_expired(&since,0,131,30) && since==0);
    CHECK(!ng_watch_grace_expired(&since,1,200,30) && since==200);
    CHECK(!ng_watch_grace_expired(NULL,1,500,30));
  }
  printf("UNIT %s fails=%d\n", fails?"FAIL":"OK", fails); return fails?1:0;
}
