#include "../network_validation.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    const char *valid[]={"192.168.0.41","0.0.0.0","255.255.255.255","printer",
        "printer.local","klipper-host.example","A1.local"};
    const char *invalid[]={"","http://printer","ws://printer","printer:7125",
        "user@printer","printer/path","printer?x=1","printer#x","printer name",
        ".printer","printer.","printer..local","-printer","printer-",
        "printer_.local","300.1.1.1","1.2.3","1.2.3.4.5","1234.0.0.1","1"};
    for(size_t i=0;i<sizeof(valid)/sizeof(*valid);++i)assert(panel_host_valid(valid[i]));
    for(size_t i=0;i<sizeof(invalid)/sizeof(*invalid);++i)assert(!panel_host_valid(invalid[i]));
    assert(!panel_host_valid(NULL));
    char maximum[65];memset(maximum,'a',63);maximum[63]=0;assert(panel_host_valid(maximum));
    maximum[63]='a';maximum[64]=0;assert(!panel_host_valid(maximum));
    uint16_t port=99;
    assert(panel_port_parse("1",&port)&&port==1);
    assert(panel_port_parse("7125",&port)&&port==7125);
    assert(panel_port_parse("65535",&port)&&port==65535);
    const char *bad_ports[]={"","0","65536","9999999999999999999","-1","+80","80x"," 80","80 ","1.0"};
    for(size_t i=0;i<sizeof(bad_ports)/sizeof(*bad_ports);++i){port=99;assert(!panel_port_parse(bad_ports[i],&port)&&port==99);}
    assert(!panel_port_parse(NULL,&port));assert(!panel_port_parse("7125",NULL));
    assert(!panel_station_valid(NULL,"password",false));
    assert(!panel_station_valid("network",NULL,false));
    assert(!panel_station_valid("","password",false));
    assert(panel_station_valid("open","",false));
    assert(!panel_station_valid("secured","",true));
    assert(!panel_station_valid("secured","1234567",true));
    assert(panel_station_valid("secured","12345678",true));
    char ssid[34];memset(ssid,'s',32);ssid[32]=0;
    char psk[66];memset(psk,'a',63);psk[63]=0;
    assert(panel_station_valid(ssid,psk,true));
    psk[63]='F';psk[64]=0;assert(panel_station_valid(ssid,psk,true));
    psk[63]='z';assert(!panel_station_valid(ssid,psk,true));
    psk[63]='a';psk[64]='a';psk[65]=0;assert(!panel_station_valid(ssid,psk,true));
    ssid[32]='s';ssid[33]=0;assert(!panel_station_valid(ssid,"12345678",false));
    // Limits count UTF-8 bytes, not visual characters.
    char multibyte[35];for(unsigned i=0;i<16;++i){multibyte[i*2]=(char)0xc3;multibyte[i*2+1]=(char)0xa9;}
    multibyte[32]=0;assert(panel_station_valid(multibyte,"12345678",true));
    multibyte[32]=(char)0xc3;multibyte[33]=(char)0xa9;multibyte[34]=0;
    assert(!panel_station_valid(multibyte,"12345678",true));
    puts("PASS: endpoint validation, URL rejection, port bounds, SSID byte limits, open/secured passwords and hex PSK");
    return 0;
}
