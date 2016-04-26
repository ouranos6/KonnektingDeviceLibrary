/*
 *    Copyright (C) 2016 Alexander Christian <info(at)root1.de>. All rights reserved.
 *    This file is part of KONNEKTING Knx Device Library.
 * 
 *    The KONNEKTING Knx Device Library is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Knx Programming via GroupWrite-telegram
 * 
 * @author Alexander Christian <info(at)root1.de>
 * @since 2015-11-06
 * @license GPLv3
 */

#include "KonnektingProg.h"
#ifdef ESP8266
#include <ESP8266WiFi.h>
#endif

#define DEBUG

// comment just for debugging purpose to disable memory write
#define WRITEMEM 1



#ifdef DEBUG
#define CONSOLEDEBUG(...)  if (_device->hasDebugSerial()) {_device->getDebugSerial()->print(__VA_ARGS__);}
#define CONSOLEDEBUGLN(...)  if (_device->hasDebugSerial()) {_device->getDebugSerial()->println(__VA_ARGS__);}
#else
#define CONSOLEDEBUG(...) 
#define CONSOLEDEBUGLN(...)
#endif



/**
 * Constructor
 */
KonnektingProg::KonnektingProg(KonnektingDevice* device) {
    
    _device = device;
    CONSOLEDEBUG(F("\n\n\n\nSetup KonnektingProg"));
    
#ifdef ESP8266
    CONSOLEDEBUG(F(F("Setup ESP8266 ... "));

    // disable WIFI
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();
    delay(100);
    
    // enable 1k EEPROM on ESP8266 platform
    EEPROM.begin(1024);
    
    CONSOLEDEBUGLN(F(F("*DONE*"));
#endif    
    
}

/**
 * Starts KNX Tools, as well as KNX Device
 * 
 * @param serial serial port reference, f.i. "Serial" or "Serial1"
 * @param progButtonPin pin which drives LED when in programming mode, default should be D3
 * @param progLedPin pin which toggles programming mode, needs an interrupt enabled pin!, default should be D8
 * @param manufacturerID
 * @param deviceID
 * @param revisionID
 * 
 */
void KonnektingProg::init(int progButtonPin, int progLedPin, word manufacturerID, byte deviceID, byte revisionID) {
    _initialized = true;

    _manufacturerID = manufacturerID;
    _deviceID = deviceID;
    _revisionID = revisionID;

    _progLED = progLedPin; // default pin D8
    _progButton = progButtonPin; // default pin D3 (->interrupt!)

    pinMode(_progLED, OUTPUT);
    pinMode(_progButton, INPUT);
    //digitalWrite(_progButton, HIGH); //PULLUP

    digitalWrite(_progLED, LOW);

    attachInterrupt(digitalPinToInterrupt(_progButton), KonnektingProgProgButtonPressed, RISING);

    // hardcoded stuff
    CONSOLEDEBUG(F("Manufacturer: "));
    CONSOLEDEBUG(_manufacturerID, HEX);
    CONSOLEDEBUGLN(F("hex"));

    CONSOLEDEBUG(F("Device: "));
    CONSOLEDEBUG(_deviceID, HEX);
    CONSOLEDEBUGLN(F("hex"));

    CONSOLEDEBUG(F("Revision: "));
    CONSOLEDEBUG(_revisionID, HEX);
    CONSOLEDEBUGLN(F("hex"));

    CONSOLEDEBUG(F("numberOfCommObjects: "));
    CONSOLEDEBUGLN(_device->getNumberOfComObjects());

    // calc index of parameter table in eeprom --> depends on number of com objects
    _paramTableStartindex = EEPROM_COMOBJECTTABLE_START + (_device->getNumberOfComObjects() * 2);

    _deviceFlags = EEPROM.read(EEPROM_DEVICE_FLAGS);
    
    CONSOLEDEBUG(F("_deviceFlags: "));
    CONSOLEDEBUG(_deviceFlags, BIN);
    CONSOLEDEBUGLN(F("bin"));

    _individualAddress = P_ADDR(1, 1, 254);
    if (!isFactorySetting()) {
        CONSOLEDEBUGLN(F("Using EEPROM"));
        /*
         * Read eeprom stuff
         */

        // PA
        byte hiAddr = EEPROM.read(EEPROM_INDIVIDUALADDRESS_HI);
        byte loAddr = EEPROM.read(EEPROM_INDIVIDUALADDRESS_LO);
        _individualAddress = (hiAddr << 8) + (loAddr << 0);

        // ComObjects
        // at most 255 com objects
        for (byte i = 0; i < _device->getNumberOfComObjects()*2; i+=2) {
            byte hi = EEPROM.read(i + EEPROM_COMOBJECTTABLE_START);
            byte lo = EEPROM.read(i + EEPROM_COMOBJECTTABLE_START + 1);
            word comObjAddr = (hi << 8) + (lo << 0);
            _device->setComObjectAddress(i+1, comObjAddr);
            CONSOLEDEBUG(F("ComObj ID="));
            CONSOLEDEBUG(i);
            CONSOLEDEBUG(F(" index="));
            CONSOLEDEBUG((i+1));
            CONSOLEDEBUG(F(" HI: 0x"));
            CONSOLEDEBUG(hi,HEX);
            CONSOLEDEBUG(F(" LO: 0x"));
            CONSOLEDEBUG(lo,HEX );
            CONSOLEDEBUG(F(" GA: 0x"));
            CONSOLEDEBUG(comObjAddr, HEX);
            CONSOLEDEBUGLN(F(""));
        }

    } else {
        CONSOLEDEBUGLN(F("Using FACTORY"));
    }
    CONSOLEDEBUG(F("IA: 0x"));
    CONSOLEDEBUGLN(_individualAddress, HEX);
    //_device->begin(serial, _individualAddress);
}

bool KonnektingProg::isActive() {
    return _initialized;
}



// bytes to skip when reading/writing in param-table

int KonnektingProg::calcParamSkipBytes(byte index) {
    // calc bytes to skip
    int skipBytes = 0;
    if (index > 0) {
        for (byte i = 0; i < index; i++) {
            skipBytes += getParamSize(i);
        }
    }
    return skipBytes;
}

byte KonnektingProg::getParamSize(byte index) {
    return _paramSizeList[index];
}

void KonnektingProg::getParamValue(int index, byte value[]) {

    if (index > _numberOfParams-1){
        return;
    }
    
    int skipBytes = calcParamSkipBytes(index);
    int paramLen = getParamSize(index);
    
    CONSOLEDEBUG(F("getParamValue: index="));
    CONSOLEDEBUG(index);
    CONSOLEDEBUG(F(" _paramTableStartindex="));
    CONSOLEDEBUG(_paramTableStartindex);
    CONSOLEDEBUG(F(" skipBytes="));
    CONSOLEDEBUG(skipBytes);
    CONSOLEDEBUG(F(" paramLen="));
    CONSOLEDEBUG(paramLen);
    CONSOLEDEBUGLN(F(""));
    
    // read byte by byte
    for (byte i = 0; i < paramLen; i++) {
        
        int addr = _paramTableStartindex + skipBytes + i;
        
        value[i] = EEPROM.read(addr);
        CONSOLEDEBUG(F(" val["));
        CONSOLEDEBUG(i);
        CONSOLEDEBUG(F("]@"));
        CONSOLEDEBUG(addr);
        CONSOLEDEBUG(F(" --> 0x"));
        CONSOLEDEBUG(value[i], HEX);
        CONSOLEDEBUGLN(F(""));
    }
}

// local helper method got the prog-button-interrupt

void KonnektingProgProgButtonPressed() {
//    CONSOLEDEBUGLN(F("PROGBUTTON toggle"));
//    //Tools.toggleProgState();
}

/*
 * toggle the actually ProgState
 */
void KonnektingProg::toggleProgState() {
    _progState = !_progState; // toggle
    setProgState(_progState); // set
}

/*
 * Sets thep prog state to given boolean value
 * @param state new prog state
 */
void KonnektingProg::setProgState(bool state) {
    if (state == true) {
        _progState = true;
        digitalWrite(_progLED, HIGH);
        CONSOLEDEBUGLN(F("PROGBUTTON 1"));
    } else if (state == false) {
        _progState = false;
        digitalWrite(_progLED, LOW);
        CONSOLEDEBUGLN(F("PROGBUTTON 0"));
    }
}

/**
 * Reboot device via WatchDogTimer within 1s
 */
void KonnektingProg::reboot() {
    _device->end();
    
#ifdef ESP8266 
    CONSOLEDEBUGLN(F("ESP8266 restart"));
    ESP.restart();    
#endif
    
#ifdef __AVR_ATmega328P__
    // to overcome WDT infinite reboot-loop issue
    // see: https://github.com/arduino/Arduino/issues/4492
    CONSOLEDEBUGLN(F("software reset NOW"));
    delay(500);
    asm volatile (F("  jmp 0"));  
#else     
    CONSOLEDEBUGLN(F("WDT reset NOW"));
    wdt_enable( WDTO_500MS ); 
    while(1) {}
#endif    
    
}

bool KonnektingProg::internalComObject(byte index) {

    CONSOLEDEBUG(F("internalComObject index="));
    CONSOLEDEBUGLN(index);
    bool consumed = false;
    switch (index) {
        case 0: // object index 0 has been updated

            
//            CONSOLEDEBUGLN(F("About to read 14 bytes"));
            byte buffer[14];
            _device->read(0, buffer);
//            CONSOLEDEBUGLN(F("done reading 14 bytes"));

            for (int i = 0; i < 14; i++) {
                CONSOLEDEBUG(F("buffer["));
                CONSOLEDEBUG(i);
                CONSOLEDEBUG(F("]\thex=0x"));
                CONSOLEDEBUG(buffer[i], HEX);
                CONSOLEDEBUG(F("  \tbin="));
                CONSOLEDEBUGLN(buffer[i], BIN);
            }

            byte protocolversion = buffer[0];
            byte msgType = buffer[1];

            CONSOLEDEBUG(F("protocolversion=0x"));
            CONSOLEDEBUGLN(protocolversion,HEX);
            
            CONSOLEDEBUG(F("msgType=0x"));
            CONSOLEDEBUGLN(msgType,HEX);

            if (protocolversion != PROTOCOLVERSION) {
                CONSOLEDEBUG(F("Unsupported protocol version. Using "));
                CONSOLEDEBUG(PROTOCOLVERSION);
                CONSOLEDEBUG(F(" Got: "));
                CONSOLEDEBUG(protocolversion);
                CONSOLEDEBUGLN(F("!"));
            } else {

                switch (msgType) {
                    case MSGTYPE_ACK:
                        CONSOLEDEBUGLN(F("Will not handle received ACK. Skipping message."));        
                        break;
                    case MSGTYPE_READ_DEVICE_INFO:
                        handleMsgReadDeviceInfo(buffer);
                        break;
                    case MSGTYPE_RESTART:
                        handleMsgRestart(buffer);
                        break;
                    case MSGTYPE_WRITE_PROGRAMMING_MODE:
                        handleMsgWriteProgrammingMode(buffer);
                        break;
                    case MSGTYPE_READ_PROGRAMMING_MODE:
                        handleMsgReadProgrammingMode(buffer);
                        break;
                    case MSGTYPE_WRITE_INDIVIDUAL_ADDRESS:
                        if (_progState) handleMsgWriteIndividualAddress(buffer);
                        break;
                    case MSGTYPE_READ_INDIVIDUAL_ADDRESS:
                        if (_progState) handleMsgReadIndividualAddress(buffer);
                        break;
                    case MSGTYPE_WRITE_PARAMETER:
                        if (_progState) handleMsgWriteParameter(buffer);
                        break;
                    case MSGTYPE_READ_PARAMETER:
                        handleMsgReadParameter(buffer);
                        break;
                    case MSGTYPE_WRITE_COM_OBJECT:
                        if (_progState) handleMsgWriteComObject(buffer);
                        break;
                    case MSGTYPE_READ_COM_OBJECT:
                        handleMsgReadComObject(buffer);
                        break;
                    default:
                        CONSOLEDEBUG(F("Unsupported msgtype: 0x"));
                        CONSOLEDEBUG(msgType, HEX);
                        CONSOLEDEBUGLN(F(" !!! Skipping message."));
                        break;
                }

            }
            consumed = true;
            break;

    }
    return consumed;

}

void KonnektingProg::sendAck(byte errorcode, byte indexinformation){
    CONSOLEDEBUG(F("sendAck errorcode=0x"));
    CONSOLEDEBUG(errorcode, HEX);
    CONSOLEDEBUG(F(" indexinformation=0x"));
    CONSOLEDEBUGLN(indexinformation, HEX);
    byte response[14];
    response[0] = PROTOCOLVERSION;
    response[1] = MSGTYPE_ACK;
    response[2] = (errorcode==0x00?0x00:0xFF);
    response[3] = errorcode;
    response[4] = indexinformation;
    for (byte i=5;i<14;i++){
        response[i] = 0x00;
    }
    _device->write(0, response);    
}


void KonnektingProg::handleMsgReadDeviceInfo(byte msg[]) {
    CONSOLEDEBUGLN(F("handleMsgReadDeviceInfo"));
    byte response[14];
    response[0] = PROTOCOLVERSION;
    response[1] = MSGTYPE_ANSWER_DEVICE_INFO;
    response[2] = (_manufacturerID >> 8) & 0xff;
    response[3] = (_manufacturerID >> 0) & 0xff;
    response[4] = _deviceID;
    response[5] = _revisionID;
    response[6] = _deviceFlags;
    response[7] = (_individualAddress >> 8) & 0xff;
    response[8] = (_individualAddress >> 0) & 0xff;
    response[9] = 0x00;
    response[10] = 0x00;
    response[11] = 0x00;
    response[12] = 0x00;
    response[13] = 0x00;
    _device->write(0, response);
}

void KonnektingProg::handleMsgRestart(byte msg[]) {
    CONSOLEDEBUGLN(F("handleMsgRestart"));
    
    byte hi = (_individualAddress >> 8) & 0xff;
    byte lo = (_individualAddress >> 0) & 0xff;
    
    if (hi==msg[2] && lo==msg[3]) {
        CONSOLEDEBUGLN(F("matching IA"));    
        // trigger restart
        reboot();
    } else {
        CONSOLEDEBUGLN(F("no matching IA"));
    }
    
}

void KonnektingProg::handleMsgWriteProgrammingMode(byte msg[]) {
    CONSOLEDEBUGLN(F("handleMsgWriteProgrammingMode"));
    //word addr = (msg[2] << 8) + (msg[3] << 0);
    
    byte ownHI = (_individualAddress >> 8) & 0xff;
    byte ownLO = (_individualAddress >> 0) & 0xff;
    if (msg[2] == ownHI && msg[3] == ownLO) {
        CONSOLEDEBUGLN(F("match"));
        setProgState(msg[4] == 0x01); 
#ifdef ESP8266
        if (msg[4] == 0x00) {
            CONSOLEDEBUGLN(F("ESP8266: EEPROM.commit()"));
            EEPROM.commit();
        }
#endif                
        
    } else {
        CONSOLEDEBUGLN(F("no match"));
    }
    sendAck(0x00, 0x00);
}

void KonnektingProg::handleMsgReadProgrammingMode(byte msg[]) {
    CONSOLEDEBUGLN(F("handleMsgReadProgrammingMode"));
    if (_progState) {
        byte response[14];
        response[0] = PROTOCOLVERSION;
        response[1] = MSGTYPE_ANSWER_PROGRAMMING_MODE;
        response[2] = (_individualAddress >> 8) & 0xff;
        response[3] = (_individualAddress >> 0) & 0xff;
        response[4] = 0x00;
        response[5] = 0x00;
        response[6] = 0x00;
        response[7] = 0x00;
        response[8] = 0x00;
        response[9] = 0x00;
        response[10] = 0x00;
        response[11] = 0x00;
        response[12] = 0x00;
        response[13] = 0x00;
        _device->write(0, response);
    }
}

void KonnektingProg::handleMsgWriteIndividualAddress(byte msg[]) {
    CONSOLEDEBUGLN(F("handleMsgWriteIndividualAddress"));
#if defined(WRITEMEM)    
    memoryUpdate(EEPROM_INDIVIDUALADDRESS_HI, msg[2]);
    memoryUpdate(EEPROM_INDIVIDUALADDRESS_LO, msg[3]);
    
    CONSOLEDEBUG(F("DeviceFlags before=0x"));
    CONSOLEDEBUG(_deviceFlags, HEX);
    CONSOLEDEBUGLN(F(""));
    // see: http://stackoverflow.com/questions/3920307/how-can-i-remove-a-flag-in-c
    _deviceFlags &= ~0x80; // remove factory setting bit (left most bit))
    CONSOLEDEBUG(F("DeviceFlags after =0x"));
    CONSOLEDEBUG(_deviceFlags, HEX);
    CONSOLEDEBUGLN(F(""));
    
    memoryUpdate(EEPROM_DEVICE_FLAGS, _deviceFlags);
#endif    
    _individualAddress = (msg[2] << 8) + (msg[3] << 0);
    sendAck(0x00, 0x00);
}

void KonnektingProg::handleMsgReadIndividualAddress(byte msg[]) {
    CONSOLEDEBUGLN(F("handleMsgReadIndividualAddress"));
    byte response[14];
    response[0] = PROTOCOLVERSION;
    response[1] = MSGTYPE_ANSWER_INDIVIDUAL_ADDRESS;
    response[2] = (_individualAddress >> 8) & 0xff;
    response[3] = (_individualAddress >> 0) & 0xff;
    response[4] = 0x00;
    response[5] = 0x00;
    response[6] = 0x00;
    response[7] = 0x00;
    response[8] = 0x00;
    response[9] = 0x00;
    response[10] = 0x00;
    response[11] = 0x00;
    response[12] = 0x00;
    response[13] = 0x00;
    _device->write(0, response);
}

void KonnektingProg::handleMsgWriteParameter(byte msg[]) {
    CONSOLEDEBUGLN(F("handleMsgWriteParameter"));
    
    byte index = msg[2];  
    
    if (index > _numberOfParams-1) {
        sendAck(KNX_DEVICE_INVALID_INDEX, index);
        return;
    }
    
    int skipBytes = calcParamSkipBytes(index);
    int paramLen = getParamSize(index);
    
    CONSOLEDEBUG(F("id="));
    CONSOLEDEBUG(index);
    CONSOLEDEBUGLN(F(""));
#if defined(WRITEMEM)    
    // write byte by byte
    for (byte i = 0; i < paramLen; i++) {
        CONSOLEDEBUG(F(" data["));
        CONSOLEDEBUG(i);
        CONSOLEDEBUG(F("]=0x"));
        CONSOLEDEBUG(msg[3 + i],HEX);
        CONSOLEDEBUGLN(F(""));
        //EEPROM.update(_paramTableStartindex + skipBytes + i, msg[3 + i]);
        memoryUpdate(_paramTableStartindex + skipBytes + i, msg[3 + i]);
    }
#endif
    sendAck(0x00, 0x00);
}

void KonnektingProg::handleMsgReadParameter(byte msg[]) {
    CONSOLEDEBUGLN(F("handleMsgReadParameter"));
    byte index = msg[0];

    byte paramSize = getParamSize(index);

    byte paramValue[paramSize];
    getParamValue(index, paramValue);

    byte response[14];
    response[0] = PROTOCOLVERSION;
    response[1] = MSGTYPE_ANSWER_PARAMETER;
    response[2] = index;

    // fill in param value
    for (byte i = 0; i < paramSize; i++) {
        response[3 + i] = paramValue[i];
    }

    // fill rest with 0x00
    for (byte i = 0; i < 11 /* max param length */ - paramSize; i++) {
        response[3 + paramSize + i] = 0;
    }

    _device->write(0, response);

}

void KonnektingProg::handleMsgWriteComObject(byte msg[]) {
    CONSOLEDEBUGLN(F("handleMsgWriteComObject"));
    byte tupels = msg[2];

    for (byte tupelNumber = 0; tupelNumber < tupels; tupelNumber++) {
        
        byte tupelOffset = 3 + (tupelNumber*3);
        
        CONSOLEDEBUG(F("tupelOffset="));
        CONSOLEDEBUGLN(tupelOffset);
        
        byte comObjId = msg[tupelOffset + 0];
        byte gaHi = msg[tupelOffset + 1];
        byte gaLo = msg[tupelOffset + 2];
        word ga = (gaHi << 8) + (gaLo << 0);
        
        CONSOLEDEBUG(F("CO id="));
        CONSOLEDEBUG(comObjId);
        CONSOLEDEBUG(F(" hi=0x"));
        CONSOLEDEBUG(gaHi, HEX);
        CONSOLEDEBUG(F(" lo=0x"));
        CONSOLEDEBUG(gaHi, HEX);
        CONSOLEDEBUG(F(" ga=0x"));
        CONSOLEDEBUG(ga, HEX);
        CONSOLEDEBUGLN(F(""));
        
        
        e_KonnektingDeviceStatus result = _device->setComObjectAddress(comObjId, ga);
        if (result != KNX_DEVICE_ERROR) {
            sendAck(result, comObjId);
        } else {
#if defined(WRITEMEM)            
            // write to eeprom?!
            memoryUpdate(EEPROM_COMOBJECTTABLE_START + (comObjId*2)+0, gaHi);
            memoryUpdate(EEPROM_COMOBJECTTABLE_START + (comObjId*2)+1, gaLo);
#endif
        }
    }
    sendAck(0x00, 0x00);
}

void KonnektingProg::handleMsgReadComObject(byte msg[]) {
    CONSOLEDEBUGLN(F("handleMsgReadComObject"));
    byte numberOfComObjects = msg[2];

    byte response[14];
    response[0] = PROTOCOLVERSION;
    response[1] = MSGTYPE_ANSWER_COM_OBJECT;
    response[2] = numberOfComObjects;
    
    for (byte i=0; i<numberOfComObjects; i++) {
        
        byte comObjId = msg[3+i];
        word ga = _device->getComObjectAddress(comObjId);
        
        byte tupelOffset = 3 + ((i - 1)*3);
        response[tupelOffset+0] = comObjId;
        response[tupelOffset+1] = (ga >> 8) & 0xff; // GA Hi
        response[tupelOffset+2] = (ga >> 0) & 0xff; // GA Lo
        
    }

    // fill rest with 0x00
    for (byte i = 0; i < 11 - (numberOfComObjects*3); i++) {
        response[3 + (numberOfComObjects*3) + i] = 0;
    }

    _device->write(0, response);

}

void KonnektingProg::memoryUpdate(int index, byte data){
    
    
    CONSOLEDEBUG(F("memUpdate: index="));    
    CONSOLEDEBUG(index);
    CONSOLEDEBUG(F(" data=0x"));
    CONSOLEDEBUG(data, HEX);
    CONSOLEDEBUGLN(F(""));
    
#ifdef ESP8266    
    CONSOLEDEBUGLN(F("ESP8266: EEPROM.update"));
    byte d = EEPROM.read(index);
    if (d!=data) {
        EEPROM.write(index, data);
    }
#else
    EEPROM.update(index, data);
    delay(10); // really required?
#endif   
    if (isFactorySetting()) {
        
    }
}

/*
 *  #define PARAM_INT8 1
    #define PARAM_UINT8 1
    #define PARAM_INT16 2
    #define PARAM_UINT16 2
    #define PARAM_INT32 4
    #define PARAM_UINT32 4
 */

uint8_t KonnektingProg::getUINT8Param(byte index){    
    if (getParamSize(index)!=PARAM_UINT8) {
        CONSOLEDEBUG(F("Requested UINT8 param for index "));
        CONSOLEDEBUG(index);
        CONSOLEDEBUG(F(" but param has different length! Will Return 0."));
        return 0;
    }
    
    byte paramValue[1];
    getParamValue(index, paramValue);
    
    return paramValue[0];
}

int8_t KonnektingProg::getINT8Param(byte index) {
    if (getParamSize(index)!=PARAM_INT8) {
        CONSOLEDEBUG(F("Requested INT8 param for index "));
        CONSOLEDEBUG(index);
        CONSOLEDEBUG(F(" but param has different length! Will Return 0."));
        return 0;
    }
    
    byte paramValue[1];
    getParamValue(index, paramValue);
    
    return paramValue[0];
}

uint16_t KonnektingProg::getUINT16Param(byte index) {
    if (getParamSize(index)!=PARAM_UINT16) {
        CONSOLEDEBUG(F("Requested UINT16 param for index "));
        CONSOLEDEBUG(index);
        CONSOLEDEBUG(F(" but param has different length! Will Return 0."));
        return 0;
    }
    
    byte paramValue[2];
    getParamValue(index, paramValue);
    
    uint16_t val = (paramValue[1] << 8) + (paramValue[0] << 0);
    
    return val;
}

int16_t KonnektingProg::getINT16Param(byte index) {
    if (getParamSize(index)!=PARAM_INT16) {
        CONSOLEDEBUG(F("Requested INT16 param for index "));
        CONSOLEDEBUG(index);
        CONSOLEDEBUG(F(" but param has different length! Will Return 0."));
        return 0;
    }
    
    byte paramValue[2];
    getParamValue(index, paramValue);
    
    CONSOLEDEBUG(F(" int16: [1]=0x"));
    CONSOLEDEBUG(paramValue[0],HEX);
    CONSOLEDEBUG(F(" [0]=0x"));
    CONSOLEDEBUG(paramValue[1],HEX);
    CONSOLEDEBUGLN(F(""));
    
    int16_t val = (paramValue[1] << 8) + (paramValue[0] << 0);
    
    return val;
}

uint32_t KonnektingProg::getUINT32Param(byte index){
    if (getParamSize(index)!=PARAM_UINT32) {
        CONSOLEDEBUG(F("Requested UINT32 param for index "));
        CONSOLEDEBUG(index);
        CONSOLEDEBUG(F(" but param has different length! Will Return 0."));
        return 0;
    }
    
    byte paramValue[4];
    getParamValue(index, paramValue);
    
    uint32_t val = (paramValue[1] <<24) + (paramValue[1] <<16) + (paramValue[1] << 8) + (paramValue[0] << 0);
    
    return val;
}

int32_t KonnektingProg::getINT32Param(byte index) {
    if (getParamSize(index)!=PARAM_INT32) {
        CONSOLEDEBUG(F("Requested UINT32 param for index "));
        CONSOLEDEBUG(index);
        CONSOLEDEBUG(F(" but param has different length! Will Return 0."));
        return 0;
    }
    
    byte paramValue[4];
    getParamValue(index, paramValue);
    
    int32_t val = (paramValue[1] <<24) + (paramValue[1] <<16) + (paramValue[1] << 8) + (paramValue[0] << 0);
    
    return val;
}

word KonnektingProg::getIndividualAddress() {
    return _individualAddress;
}


bool KonnektingProg::isFactorySetting() {
    return _deviceFlags == 0xff;
}