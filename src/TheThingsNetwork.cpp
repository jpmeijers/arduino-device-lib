// Copyright © 2015 The Things Network
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#include <Arduino.h>
#include <TheThingsNetwork.h>

#define debugPrintLn(...) { if (debugStream) debugStream->println(__VA_ARGS__); }
#define debugPrint(...) { if (debugStream) debugStream->print(__VA_ARGS__); }

#define TTN_HEX_CHAR_TO_NIBBLE(c) ((c >= 'A') ? (c - 'A' + 0x0A) : (c - '0'))
#define TTN_HEX_PAIR_TO_BYTE(h, l) ((TTN_HEX_CHAR_TO_NIBBLE(h) << 4) + TTN_HEX_CHAR_TO_NIBBLE(l))

#define MAC_TABLE 0
#define MAC_GET_SET_TABLE 1
#define MAC_JOIN_TABLE 2
#define MAC_CH_TABLE 3
#define MAC_TX_TABLE 4

char set_buffer[300];
uint16_t read_index=0;

TheThingsNetwork::TheThingsNetwork(Stream& modemStream, Stream& debugStream, ttn_fp_t fp, uint8_t sf, uint8_t fsb) {
  this->debugStream = &debugStream;
  this->modemStream = &modemStream;
  this->fp = fp;
  this->sf = sf;
  this->fsb = fsb;
}

void TheThingsNetwork::init() {
  static bool done = false;

  if (done) {
    return;
  }

  // trigger auto-baud detection to fix
  // https://github.com/TheThingsNetwork/arduino-device-lib/issues/114
  modemStream->write(0x55);
  delay(200);

  done = true;
}

String TheThingsNetwork::readLine() {
  while (true) {
    String line = modemStream->readStringUntil('\n');
    if (line.length() > 0) {
      return line.substring(0, line.length() - 1);
    }
  }
}

String TheThingsNetwork::readValue(String cmd) {
  init();

  while(modemStream->available()) {
    modemStream->read();
  }

  modemStream->println(cmd);
  return readLine();
}

void TheThingsNetwork::reset(bool adr) {
  String version = readValue("sys reset");
  model = version.substring(0, version.indexOf(' '));
  debugPrint("Version is ");
  debugPrint(version);
  debugPrint(", model is ");
  debugPrintLn(model);

  String devEui = readValue("sys get hweui");
  loraMacSet(MAC_SET_DEVEUI, devEui.c_str());
  if(adr){
      loraMacSet(MAC_SET_ADR, "on");
  } else {
      loraMacSet(MAC_SET_ADR, "off");
  }
}
void TheThingsNetwork::onMessage(void (*cb)(const byte* payload, size_t length, port_t port)) {
  this->messageCallback = cb;
}

bool TheThingsNetwork::personalize(const char *devAddr, const char *nwkSKey, const char *appSKey) {
  reset();
  loraMacSet(MAC_SET_DEVICEADDRESS, devAddr);
  loraMacSet(MAC_SET_NWKSKEY, nwkSKey);
  loraMacSet(MAC_SET_APPSKEY, appSKey);
  return personalize();
}

bool TheThingsNetwork::personalize() {
  configureChannels(this->sf, this->fsb);
  loraJoinSet(MAC_JOIN_MODE_ABP);
  String response = readLine();
  if (response != "accepted") {
    debugPrint("Personalize not accepted: ");
    debugPrintLn(response);
    return false;
  }

  fillAirtimeInfo();
  debugPrint("Personalize accepted. Status: ");
  debugPrintLn(readValue("mac get status"));
  return true;
}

bool TheThingsNetwork::provision(const char *appEui, const char *appKey) {
  loraMacSet(MAC_SET_APPEUI, appEui);
  loraMacSet(MAC_SET_APPKEY, appKey);
  ChoseTable(MAC_TABLE, MAC_PREFIX, true);
  ChoseTable(MAC_TABLE, MAC_SAVE, false);
  Serial.write("\r\n");
  return true;
}

bool TheThingsNetwork::join(int8_t retries, uint32_t retryDelay) {
  configureChannels(this->sf, this->fsb);
  String devEui = readValue("sys get hweui");
  loraMacSet(MAC_SET_DEVEUI, devEui.c_str());
  while (--retries) {
    if (!loraJoinSet(MAC_JOIN_MODE_OTAA)) {
      debugPrintLn("Send join command failed");
      delay(retryDelay);
      continue;
    }
    String response = readLine();
    if (response != "accepted") {
      debugPrint("Join not accepted: ");
      debugPrintLn(response);
      delay(retryDelay);
      continue;
    }
    debugPrint("Join accepted. Status: ");
    debugPrintLn(readValue("mac get status"));
    debugPrint("DevAddr: ");
    debugPrintLn(readValue("mac get devaddr"));
    fillAirtimeInfo();
    return true;
  }
  return false;
}

bool TheThingsNetwork::join(const char *appEui, const char *appKey, int8_t retries, uint32_t retryDelay) {
  reset();
  provision(appEui, appKey);
  return join(retries, retryDelay);
}

int TheThingsNetwork::sendBytes(const byte* payload, size_t length, port_t port, bool confirm) {
  bool send = confirm ? loraSendPayload(MAC_TX_TYPE_CNF, port, (uint8_t *)payload, length) : loraSendPayload(MAC_TX_TYPE_UCNF, port, (uint8_t *)payload, length);
  if (!send) {
    debugPrintLn("Send command failed");
    return -1;
  }

  String response = readLine();
  float i = this->airtime;
  trackAirtime(length);
  if (!isnan(i) && !isinf(i) && !isnan(this->airtime) && !isinf(this->airtime)) {
    debugPrint("Airtime added: ");
    debugPrint(this->airtime - i);
    debugPrintLn(" s");
    debugPrint("Total airtime: ");
    debugPrint(this->airtime);
    debugPrintLn(" s");
  }
  if (response == "mac_tx_ok") {
    debugPrintLn("Successful transmission");
    return 1;
  }
  if (response.startsWith(F("mac_rx"))) {
    uint8_t portEnds = response.indexOf(" ", 7);
    port_t downlinkPort = response.substring(7, portEnds).toInt();
    String data = response.substring(portEnds + 1);
    size_t downlinkLength = data.length() / 2;
    byte downlink[64];
    for (size_t i = 0, d = 0; i < downlinkLength; i++, d += 2) {
      downlink[i] = TTN_HEX_PAIR_TO_BYTE(data[d], data[d+1]);
    }
    debugPrint("Successful transmission. Received ");
    debugPrint(downlinkLength);
    debugPrintLn(" bytes");
    if (this->messageCallback)
      this->messageCallback(downlink, downlinkLength, downlinkPort);
    return 2;
  }

  debugPrint("Unexpected response: ");
  debugPrintLn(response);
  return -10;
}

int TheThingsNetwork::poll(port_t port, bool confirm) {
  byte payload[] = { 0x00 };
  return sendBytes(payload, 1, port, confirm);
}

void TheThingsNetwork::fillAirtimeInfo() {
  this->info = {0, 0, 0, 0, 0, 0};

  String message = readValue("radio get sf");
  this->info.sf = message[3] ? (message[2] - 48) * 10 + message[3] - 48 : message[2] - 48;

  message = readValue("radio get bw");
  this->info.band = (message[0] - 48) * 100 + (message[1] - 48) * 10 + message[2] - 48;

  message = readValue("radio get prlen");
  this->info.ps = this->info.ps + message[0] - 48;

  message = readValue("radio get crc");
  this->info.header = message == "on" ? 1 : 0;

  message = readValue("radio get cr");
  this->info.cr = (this->info.cr + message[2] - 48);

  this->info.de = this->info.sf >= 11 ? 1 : 0;
}

void TheThingsNetwork::trackAirtime(size_t payloadSize) {
  payloadSize = 13 + payloadSize;

  float Tsym = pow(2, this->info.sf) / this->info.band;
  float Tpreamble = (this->info.ps + 4.25) * Tsym;
  uint16_t payLoadSymbNb = 8 + (max(ceil((8 * payloadSize - 4 * this->info.sf + 28 + 16 - 20 * this->info.header) / (4 * (this->info.sf - 2 * this->info.de))) * (this->info.cr + 4), 0));
  float Tpayload = payLoadSymbNb * Tsym;
  float Tpacket = Tpreamble + Tpayload;
  this->airtime = this->airtime + (Tpacket / 1000);
}

void TheThingsNetwork::showStatus() {
  debugPrint("EUI: ");
  debugPrintLn(readValue("sys get hweui"));
  debugPrint("Battery: ");
  debugPrintLn(readValue("sys get vdd"));
  debugPrint("AppEUI: ");
  debugPrintLn(readValue("mac get appeui"));
  debugPrint(F("DevEUI: "));
  debugPrintLn(readValue("mac get deveui"));

  if (this->model == "RN2483") {
    debugPrint("Band: ");
    debugPrintLn(readValue("mac get band"));
  }
  debugPrint("Data Rate: ");
  debugPrintLn(readValue("mac get dr"));
  debugPrint("RX Delay 1: ");
  debugPrintLn(readValue("mac get rxdelay1"));
  debugPrint("RX Delay 2: ");
  debugPrintLn(readValue("mac get rxdelay2"));
  debugPrint("Total airtime: ");
  debugPrint(this->airtime);
  debugPrintLn(" s");
}

void TheThingsNetwork::configureEU868(uint8_t sf) {
  uint8_t ch;
  char dr[1];
  uint32_t freq = 867100000;
  String str = "";

  loraMacSet(MAC_SET_RX2, "3 869525000");
  for (ch = 0; ch < 8; ch++) {
    if (ch > 2) {
      str = String(freq);
      loraChSet(MAC_CHANNEL_FREQ, ch, str.c_str());
      loraChSet(MAC_CHANNEL_DRRANGE, ch, "0 5");
      loraChSet(MAC_CHANNEL_STATUS, ch, "on");
      freq = freq + 200000;
    }
    loraChSet(MAC_CHANNEL_DCYCLE, ch, "799");
  }
  loraChSet(MAC_CHANNEL_DRRANGE, 1, "0 6");
  loraMacSet(MAC_SET_PWRIDX, TTN_PWRIDX_868);
  switch (sf) {
    case 7:
      dr[0] = '5';
      break;
    case 8:
      dr[0] = '4';
      break;
    case 9:
      dr[0] = '3';
      break;
    case 10:
      dr[0] = '2';
      break;
    case 11:
      dr[0] = '1';
      break;
    case 12:
      dr[0] = '0';
      break;
    default:
      debugPrintLn("Invalid SF")
      break;
  }
  dr[1] = '\0';
  if (dr[0] >= '0' && dr[0] <= '5'){
    loraMacSet(MAC_SET_DR, dr);
  }
}

void TheThingsNetwork::configureUS915(uint8_t sf, uint8_t fsb) {
  uint8_t ch;
  char dr[1];
  uint8_t chLow = fsb > 0 ? (fsb - 1) * 8 : 0;
  uint8_t chHigh = fsb > 0 ? chLow + 7 : 71;
  uint8_t ch500 = fsb + 63;

  
 loraMacSet(MAC_SET_PWRIDX, TTN_PWRIDX_915);
  for (ch = 0; ch < 72; ch++) {
    if (ch == ch500 || ch <= chHigh && ch >= chLow) {
      loraChSet(MAC_CHANNEL_STATUS, ch, "on");
      if (ch < 63) {
        loraChSet(MAC_CHANNEL_DRRANGE, ch, "0 3");
      }
    } else {
      loraChSet(MAC_CHANNEL_STATUS, ch, "off");
    }
  }
  switch (sf) {
    case 7:
      dr[0] = '3';
      break;
    case 8:
      dr[0] = '2';
      break;
    case 9:
      dr[0] = '1';
      break;
    case 10:
      dr[0] = '0';
      break;
    default:
      debugPrintLn("Invalid SF")
      break;
  }
  dr[1] = '\0';
  if (dr[0] >= '0' && dr[0] < '4'){
    loraMacSet(MAC_SET_DR, dr);
  }
}

void TheThingsNetwork::configureChannels(uint8_t sf, uint8_t fsb) {
  switch (this->fp) {
    case TTN_FP_EU868:
      configureEU868(sf);
      break;
    case TTN_FP_US915:
      configureUS915(sf, fsb);
      break;
    default:
      debugPrintLn("Invalid frequency plan");
      break;
  }
  loraMacSet(MAC_SET_RETX, TTN_RETX);
}

void TheThingsNetwork::ChoseTable(uint8_t table, uint8_t index, bool with_space) {
  switch(table) {
    case MAC_TABLE:
      strcpy_P(set_buffer, (char *)pgm_read_word(&(mac_table[index])));
      break;
    case MAC_GET_SET_TABLE:
      strcpy_P(set_buffer, (char *)pgm_read_word(&(mac_set_options[index])));
      break;
    case MAC_JOIN_TABLE:
      strcpy_P(set_buffer, (char *)pgm_read_word(&(mac_join_mode[index])));
      break;
    case MAC_CH_TABLE:
      strcpy_P(set_buffer, (char *)pgm_read_word(&(mac_ch_options[index])));
      break;
    case MAC_TX_TABLE:
      strcpy_P(set_buffer, (char *)pgm_read_word(&(mac_tx_table[index])));
      break;  
    default:
      return ;
      break;
  }
  Serial1.write(set_buffer);
  debugPrint(set_buffer);
  
  if (with_space) {
    Serial1.write(" ");
    debugPrint(" ");
  }
}

bool TheThingsNetwork::loraMacSet(uint8_t index, const char* setting) {
  while (Serial1.available())
    Serial1.read();
  debugPrint("Sending: ");
  ChoseTable(MAC_TABLE, MAC_PREFIX, true);
  ChoseTable(MAC_TABLE, MAC_SET, true);
  ChoseTable(MAC_GET_SET_TABLE, index, true);
  Serial1.write(setting);
  Serial1.write("\r\n");
  debugPrintLn(setting);

  return waitForOk();
}

bool TheThingsNetwork::waitForOk() {
  String line = readValue(set_buffer); 
  if (line != "ok") {
    debugPrint("Response is not OK: ");
    debugPrintLn(line);
    return false;
  }
  return true;
}

bool TheThingsNetwork::loraChSet(uint8_t index, uint8_t channel, const char* setting)
{
  while(Serial1.available()) {
    Serial1.read();
  }
  char ch[5];
  if (channel > 9) {
    ch[0] = ((channel - (channel % 10)) / 10) + 48;
    ch[1] = (channel % 10) + 48;
  } else {
    ch[0] = channel + 48;
    ch[1] = '\0';
  }
  ch[2] = '\0';
  debugPrint("Sending: ");
  ChoseTable(MAC_TABLE, MAC_PREFIX,true);
  ChoseTable(MAC_TABLE, MAC_SET,true);
  ChoseTable(MAC_GET_SET_TABLE, MAC_SET_CH,true);
  ChoseTable(MAC_CH_TABLE, index, true);
  Serial1.write(ch);
  Serial1.write(" ");
  Serial1.write(setting);
  Serial1.write("\r\n");
  debugPrint(channel);
  debugPrint(" ");
  debugPrintLn(setting);
  return waitForOk();
}

bool TheThingsNetwork::loraJoinSet(uint8_t type) {
  while (Serial1.available()) {
    Serial1.read();
  }
  debugPrint("Sending: ");
  ChoseTable(MAC_TABLE, MAC_PREFIX, true);
  ChoseTable(MAC_TABLE, MAC_JOIN, true);
  ChoseTable(MAC_JOIN_TABLE, type, false);
  Serial1.write("\r\n");
  debugPrintLn();
  return waitForOk();
}

bool TheThingsNetwork::loraSendPayload(uint8_t mode, uint8_t port, uint8_t* payload, size_t len) {
  while(Serial1.available()) {
    Serial1.read();
  }
  debugPrint("Sending: ");
  read_index = 0;
  ChoseTable(MAC_TABLE, MAC_PREFIX,true);
  ChoseTable(MAC_TABLE, MAC_TX,true);
  ChoseTable(MAC_TX_TABLE, mode,true);
  Serial1.write(0x30+port);
  debugPrint(port);
  debugPrint(" ");
  Serial1.print(" ");
  uint8_t i=0;
  for(i=0;i<len;i++) {
    if(payload[i] ==0) {
      Serial1.print("00");
      debugPrint("00");
    } else if(payload[i] < 16) {
      Serial1.print("0");
      debugPrint("0");
      Serial1.print(payload[i],HEX);
      debugPrint(payload[i], HEX);
    } else {
      Serial1.print(payload[i],HEX);
      debugPrint(payload[i], HEX);
    }
  }
  Serial1.write("\r\n");
  debugPrintLn();
  return waitForOk();
}
