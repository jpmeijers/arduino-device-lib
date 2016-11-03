// Copyright © 2016 The Things Network
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#ifndef _THETHINGSNETWORK_H_
#define _THETHINGSNETWORK_H_

#include <Arduino.h>
#include <Stream.h>
#include <avr/pgmspace.h>

#define TTN_DEFAULT_SF 7
#define TTN_DEFAULT_FSB 2
#define TTN_RETX "7"

#define TTN_PWRIDX_868 "1"
#define TTN_PWRIDX_915 "5"

const char mac_prefix[] PROGMEM = "mac";
const char mac_reset[] PROGMEM = "reset";
const char mac_tx[] PROGMEM = "tx";
const char mac_join[] PROGMEM = "join";
const char mac_save[] PROGMEM = "save";
const char mac_force_enable[] PROGMEM = "forceENABLE";
const char mac_pause[] PROGMEM = "pause";
const char mac_resume[] PROGMEM = "resume";
const char mac_set[] PROGMEM = "set";
const char mac_get[] PROGMEM = "get";

const char* const mac_table[] PROGMEM = {mac_prefix,mac_reset,mac_tx,mac_join,mac_save,mac_force_enable,mac_pause,mac_resume,mac_set,mac_get};

#define MAC_PREFIX 0
#define MAC_RESET 1
#define MAC_TX 2
#define MAC_JOIN 3
#define MAC_SAVE 4
#define MAC_FORCE_ENABLE 5
#define MAC_PAUSE 6
#define MAC_RESUME 7
#define MAC_SET 8
#define MAC_GET 9

const char mac_set_devaddr[] PROGMEM = "devaddr";
const char mac_set_deveui[] PROGMEM = "deveui";
const char mac_set_appeui[] PROGMEM = "appeui";
const char mac_set_nwkskey[] PROGMEM = "nwkskey";
const char mac_set_appskey[] PROGMEM = "appskey";
const char mac_set_appkey[] PROGMEM = "appkey";
const char mac_set_pwridx[] PROGMEM = "pwridx";
const char mac_set_dr[] PROGMEM = "dr";
const char mac_set_adr[] PROGMEM = "adr";
const char mac_set_bat[] PROGMEM = "bat";
const char mac_set_retx[] PROGMEM = "retx";
const char mac_set_linkchk[] PROGMEM = "linkchk";
const char mac_set_rxdelay1[] PROGMEM = "rxdelay1";
const char mac_set_ar[] PROGMEM = "ar";
const char mac_set_rx2[] PROGMEM = "rx2";
const char mac_set_ch[] PROGMEM = "ch";

const char* const mac_set_options[] PROGMEM = {mac_set_devaddr,mac_set_deveui,mac_set_appeui,mac_set_nwkskey,mac_set_appskey,mac_set_appkey,mac_set_pwridx,mac_set_dr,mac_set_adr,mac_set_bat,mac_set_retx,mac_set_linkchk,mac_set_rxdelay1,mac_set_ar,mac_set_rx2,mac_set_ch};

#define MAC_SET_DEVICEADDRESS 0
#define MAC_SET_DEVEUI 1
#define MAC_SET_APPEUI 2
#define MAC_SET_NWKSKEY 3
#define MAC_SET_APPSKEY 4
#define MAC_SET_APPKEY 5
#define MAC_SET_PWRIDX 6
#define MAC_SET_DR 7
#define MAC_SET_ADR 8
#define MAC_SET_BAT 9
#define MAC_SET_RETX 10
#define MAC_SET_LINKCHK 11
#define MAC_SET_RXDELAY1 12
#define MAC_SET_AR 13
#define MAC_SET_RX2 14
#define MAC_SET_CH 15

const char mac_join_mode_otaa[] PROGMEM = "otaa";
const char mac_join_mode_abp[] PROGMEM = "abp";

const char* const mac_join_mode[] PROGMEM = {mac_join_mode_otaa,mac_join_mode_abp};

#define MAC_JOIN_MODE_OTAA 0
#define MAC_JOIN_MODE_ABP 1

const char channel_dcycle[] PROGMEM = "dcycle";
const char channel_drrange[] PROGMEM = "drrange";
const char channel_freq[] PROGMEM = "freq";
const char channel_status[] PROGMEM = "status";

const char* const mac_ch_options[] PROGMEM = {channel_dcycle,channel_drrange,channel_freq,channel_status};

#define MAC_CHANNEL_DCYCLE 0
#define MAC_CHANNEL_DRRANGE 1
#define MAC_CHANNEL_FREQ 2
#define MAC_CHANNEL_STATUS 3 

const char mac_tx_type_cnf[] PROGMEM = "cnf";
const char mac_tx_type_ucnf[] PROGMEM = "uncnf";

const char* const mac_tx_table[] PROGMEM = {mac_tx_type_cnf,mac_tx_type_ucnf};

#define MAC_TX_TYPE_CNF 0
#define MAC_TX_TYPE_UCNF 1

typedef uint8_t port_t;

enum ttn_fp_t {
  TTN_FP_EU868,
  TTN_FP_US915
};

typedef struct  airtime_s
{
  uint8_t sf;
  uint8_t de;
  uint8_t ps;
  uint16_t band;
  uint8_t header;
  uint8_t cr;
} airtime_t;

class TheThingsNetwork
{
  private:
    port_t port;
    Stream* modemStream;
    Stream* debugStream;
    String model;
    airtime_t info;
    float airtime;
    ttn_fp_t fp;
    uint8_t sf;
    uint8_t fsb;
    void (* messageCallback)(const byte* payload, size_t length, port_t port);

    String readLine();
    void fillAirtimeInfo();
    void trackAirtime(size_t payloadSize);
    String readValue(String key);
    void init();
    void reset(bool adr = true);
    void configureEU868(uint8_t sf);
    void configureUS915(uint8_t sf, uint8_t fsb);
    void configureChannels(uint8_t sf, uint8_t fsb);
    bool waitForOk();
    void ChoseTable(uint8_t table, uint8_t index, bool with_space);
    bool loraMacSet(uint8_t index, const char* setting);
    bool loraChSet(uint8_t index, uint8_t channel, const char* setting);
    bool loraJoinSet(uint8_t type);
    bool loraSendPayload(uint8_t mode, uint8_t port, uint8_t* payload, size_t len);

  public:
    TheThingsNetwork(Stream& modemStream, Stream& debugStream, ttn_fp_t fp, uint8_t sf = TTN_DEFAULT_SF, uint8_t fsb = TTN_DEFAULT_FSB);
    void showStatus();
    void onMessage(void (*cb)(const byte* payload, size_t length, port_t port));
    bool provision(const char *appEui, const char *appKey);
    bool join(const char *appEui, const char *appKey, int8_t retries = -1, uint32_t retryDelay = 10000);
    bool join(int8_t retries = -1, uint32_t retryDelay = 10000);
    bool personalize(const char *devAddr, const char *nwkSKey, const char *appSKey);
    bool personalize();
    int sendBytes(const byte* payload, size_t length, port_t port = 1, bool confirm = false);
    int poll(port_t port = 1, bool confirm = false);
};

#endif
