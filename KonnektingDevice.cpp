/*
 *    This file is part of KONNEKTING Knx Device Library.
 * 
 *    It is derived from another GPLv3 licensed project:
 *      The Arduino Knx Bus Device library allows to turn Arduino into "self-made" KNX bus device.
 *      Copyright (C) 2014 2015 Franck MARINI (fm@liwan.fr)
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

// File : KonnektingDevice.cpp
// Modified: Alexander Christian <info(at)root1.de>
// Description : KonnektingDevice Abstraction Layer
// Module dependencies : HardwareSerial, KnxTelegram, KnxComObject, KnxTpUart, RingBuffer

#include "KonnektingDevice.h"

// enable debug code
#define DEBUG

#ifdef DEBUG
#define CONSOLEDEBUG(...)  if (hasDebugSerial()) {getDebugSerial()->print(__VA_ARGS__);}
#define CONSOLEDEBUGLN(...)  if (hasDebugSerial()) {getDebugSerial()->println(__VA_ARGS__);}
#else
#define CONSOLEDEBUG(...) 
#define CONSOLEDEBUGLN(...)
#endif


static inline word TimeDeltaWord(word now, word before) {
    return (word) (now - before);
}

// Constructor

KonnektingDevice::KonnektingDevice() {


    _state = INIT;
    _tpuart = NULL;
    _txActionList = ActionRingBuffer<type_tx_action, ACTIONS_QUEUE_SIZE>();
    _initCompleted = false;
    _initIndex = 0;
    _rxTelegram = NULL;
    _debugSerial = NULL;

    _prog = new KonnektingProg(this);

}

/**
 * Starts KNX Tools, as well as KNX Device
 * 
 * @param serial serial port reference, f.i. "Serial" or "Serial1"
 * @param comObjectList
 * @param paramSizeList
 * @param progButtonPin pin which drives LED when in programming mode, default should be D3
 * @param progLedPin pin which toggles programming mode, needs an interrupt enabled pin!, default should be D8
 * @param manufacturerID
 * @param deviceID
 * @param revisionID
 * 
 */
void KonnektingDevice::init(HardwareSerial& serial, KnxComObject comObjectList[], byte* paramSizeList,
        int progButtonPin, int progLedPin, word manufacturerID, byte deviceID, byte revisionID) {

    CONSOLEDEBUGLN(F("KD::init"));
    _prog->init(progButtonPin, progLedPin, manufacturerID, deviceID, revisionID);
    begin(serial, _prog->getIndividualAddress());
}

/**
 * Returns number of comobjects (includes prog com obj)
 * @return _numberOfComObjects
 */
int KonnektingDevice::getNumberOfComObjects() {
    return _numberOfComObjects;
}

/**
 * Start the KNX Device
 * return KNX_DEVICE_ERROR (255) if begin() failed
 * else return KNX_DEVICE_OK
 * @param serial serial device to which the KNX Transceiver is connected
 * @param physicalAddr physical address (or individual address) of device, something like 1.0.100
 * 
 * @return e_KonnektingDeviceStatus
 * 
 */
e_KonnektingDeviceStatus KonnektingDevice::begin(HardwareSerial& serial, word physicalAddr) {
    _tpuart = new KnxTpUart(serial, this, physicalAddr, NORMAL);
    _rxTelegram = &_tpuart->GetReceivedTelegram();
    // delay(10000); // Workaround for init issue with bus-powered arduino
    // the issue is reproduced on one (faulty?) TPUART device only, so remove it for the moment.
    if (_tpuart->Reset() != KNX_TPUART_OK) {
        delete(_tpuart);
        _tpuart = NULL;
        _rxTelegram = NULL;
        //        //DebugInfo("Init Error!\n");
        return KNX_DEVICE_ERROR;
    }
    _tpuart->AttachComObjectsList(_comObjectsList);
    //    _tpuart->SetEvtCallback(&KonnektingDevice::GetTpUartEvents);
    //    _tpuart->SetAckCallback(&KonnektingDevice::TxTelegramAck);
    _tpuart->Init();
    _state = IDLE;
    //DebugInfo("Init successful\n");
    _lastInitTimeMillis = millis();
    _lastTXTimeMicros = _lastTXTimeMicros = micros();

    return KNX_DEVICE_OK;
}


// Stop the KNX Device

void KonnektingDevice::end() {
    type_tx_action action;

    _state = INIT;
    while (_txActionList.Pop(action)); // empty ring buffer
    _initCompleted = false;
    _initIndex = 0;
    _rxTelegram = NULL;
    delete(_tpuart);
    _tpuart = NULL;
}


// KNX device execution task
// This function call shall be placed in the "loop()" Arduino function

void KonnektingDevice::task(void) {

    type_tx_action action;
    word nowTimeMillis, nowTimeMicros;

    // STEP 1 : Initialize Com Objects having Init Read attribute
    if (!_initCompleted) {
        nowTimeMillis = millis();
        // To avoid KNX bus overloading, we wait for 500 ms between each Init read request
        if (TimeDeltaWord(nowTimeMillis, _lastInitTimeMillis) > 500) {
            while ((_initIndex < _numberOfComObjects) && (_comObjectsList[_initIndex].GetValidity())) _initIndex++;

            if (_initIndex == _numberOfComObjects) {
                _initCompleted = true; // All the Com Object initialization have been performed
                //  //DebugInfo(String("KonnektingDevice INFO: Com Object init completed, ")+ String( _nbOfInits) + String("objs initialized.\n"));
            } else { // Com Object to be initialised has been found
                // Add a READ request in the TX action list
                action.command = KNX_READ_REQUEST;
                action.index = _initIndex;
                _txActionList.Append(action);
                _lastInitTimeMillis = millis(); // Update the timer
            }
        }
    }

    // STEP 2 : Get new received KNX messages from the TPUART
    // The TPUART RX task is executed every 400 us
    nowTimeMicros = micros();
    if (TimeDeltaWord(nowTimeMicros, _lastRXTimeMicros) > 400) {
        _lastRXTimeMicros = nowTimeMicros;
        _tpuart->RXTask();
    }

    // STEP 3 : Send KNX messages following TX actions
    if (_state == IDLE) {
        if (_txActionList.Pop(action)) { // Data to be transmitted
            switch (action.command) {

                case KNX_READ_REQUEST: // a read operation of a Com Object on the KNX network is required
                    _comObjectsList[action.index].CopyAttributes(_txTelegram);
                    _txTelegram.ClearLongPayload();
                    _txTelegram.ClearFirstPayloadByte(); // Is it required to have a clean payload ??
                    _txTelegram.SetCommand(KNX_COMMAND_VALUE_READ);
                    _txTelegram.UpdateChecksum();
                    _tpuart->SendTelegram(_txTelegram);
                    _state = TX_ONGOING;
                    break;

                case KNX_RESPONSE_REQUEST: // a response operation of a Com Object on the KNX network is required

                    _comObjectsList[action.index].CopyAttributes(_txTelegram);
                    _comObjectsList[action.index].CopyValue(_txTelegram);
                    _txTelegram.SetCommand(KNX_COMMAND_VALUE_RESPONSE);
                    _txTelegram.UpdateChecksum();
                    _tpuart->SendTelegram(_txTelegram);
                    _state = TX_ONGOING;
                    break;

                case KNX_WRITE_REQUEST: // a write operation of a Com Object on the KNX network is required
                    // update the com obj value
                    if ((_comObjectsList[action.index].GetLength()) <= 2)
                        _comObjectsList[action.index].UpdateValue(action.byteValue);
                    else {
                        _comObjectsList[action.index].UpdateValue(action.valuePtr);
                        free(action.valuePtr);
                    }
                    // transmit the value through KNX network only if the Com Object has transmit attribute
                    if ((_comObjectsList[action.index].GetIndicator()) & KNX_COM_OBJ_T_INDICATOR) {
                        _comObjectsList[action.index].CopyAttributes(_txTelegram);
                        _comObjectsList[action.index].CopyValue(_txTelegram);
                        _txTelegram.SetCommand(KNX_COMMAND_VALUE_WRITE);
                        _txTelegram.UpdateChecksum();
                        _tpuart->SendTelegram(_txTelegram);
                        _state = TX_ONGOING;
                    }
                    break;

                default: break;
            }
        }
    }

    // STEP 4 : LET THE TP-UART TRANSMIT KNX MESSAGES
    // The TPUART TX task is executed every 800 us
    nowTimeMicros = micros();
    if (TimeDeltaWord(nowTimeMicros, _lastTXTimeMicros) > 800) {
        _lastTXTimeMicros = nowTimeMicros;
        _tpuart->TXTask();
    }
}


// Quick method to read a short (<=1 byte) com object
// NB : The returned value will be hazardous in case of use with long objects

byte KonnektingDevice::read(byte objectIndex) {
    return _comObjectsList[objectIndex].GetValue();
}


// Read an usual format com object
// Supported DPT formats are short com object, U16, V16, U32, V32, F16 and F32 (not implemented yet)

template <typename T> e_KonnektingDeviceStatus KonnektingDevice::read(byte objectIndex, T& returnedValue) {
    // Short com object case
    if (_comObjectsList[objectIndex].GetLength() <= 2) {
        returnedValue = (T) _comObjectsList[objectIndex].GetValue();
        return KNX_DEVICE_OK;
    } else // long object case, let's see if we are able to translate the DPT value
    {
        byte dptValue[14]; // define temporary DPT value with max length
        _comObjectsList[objectIndex].GetValue(dptValue);
        return ConvertFromDpt(dptValue, returnedValue, pgm_read_byte(&KnxDPTIdToFormat[_comObjectsList[objectIndex].GetDptId()]));
    }
}

template e_KonnektingDeviceStatus KonnektingDevice::read <boolean>(byte objectIndex, boolean& returnedValue);
template e_KonnektingDeviceStatus KonnektingDevice::read <char>(byte objectIndex, char& returnedValue);
template e_KonnektingDeviceStatus KonnektingDevice::read <unsigned char>(byte objectIndex, unsigned char& returnedValue);
template e_KonnektingDeviceStatus KonnektingDevice::read <unsigned int>(byte objectIndex, unsigned int& returnedValue);
template e_KonnektingDeviceStatus KonnektingDevice::read <int>(byte objectIndex, int& returnedValue);
template e_KonnektingDeviceStatus KonnektingDevice::read <unsigned long>(byte objectIndex, unsigned long& returnedValue);
template e_KonnektingDeviceStatus KonnektingDevice::read <long>(byte objectIndex, long& returnedValue);
template e_KonnektingDeviceStatus KonnektingDevice::read <float>(byte objectIndex, float& returnedValue);
template e_KonnektingDeviceStatus KonnektingDevice::read <double>(byte objectIndex, double& returnedValue);



// Read any type of com object (DPT value provided as is)

e_KonnektingDeviceStatus KonnektingDevice::read(byte objectIndex, byte returnedValue[]) {
    _comObjectsList[objectIndex].GetValue(returnedValue);
    return KNX_DEVICE_OK;
}


// Update an usual format com object
// Supported DPT types are short com object, U16, V16, U32, V32, F16 and F32
// The Com Object value is updated locally
// And a telegram is sent on the KNX bus if the com object has communication & transmit attributes

template <typename T> e_KonnektingDeviceStatus KonnektingDevice::write(byte objectIndex, T value) {
    type_tx_action action;
    byte *destValue;
    byte length = _comObjectsList[objectIndex].GetLength();

    if (length <= 2) action.byteValue = (byte) value; // short object case
    else { // long object case, let's try to translate value to the com object DPT
        destValue = (byte *) malloc(length - 1); // allocate the memory for DPT
        e_KonnektingDeviceStatus status = ConvertToDpt(value, destValue, pgm_read_byte(&KnxDPTIdToFormat[_comObjectsList[objectIndex].GetDptId()]));
        if (status) // translation error
        {
            free(destValue);
            return status; // we cannot convert, we stop here
        } else action.valuePtr = destValue;
    }
    // add WRITE action in the TX action queue
    action.command = KNX_WRITE_REQUEST;
    action.index = objectIndex;
    _txActionList.Append(action);
    return KNX_DEVICE_OK;
}

template e_KonnektingDeviceStatus KonnektingDevice::write <boolean>(byte objectIndex, boolean value);
template e_KonnektingDeviceStatus KonnektingDevice::write <unsigned char>(byte objectIndex, unsigned char value);
template e_KonnektingDeviceStatus KonnektingDevice::write <char>(byte objectIndex, char value);
template e_KonnektingDeviceStatus KonnektingDevice::write <unsigned int>(byte objectIndex, unsigned int value);
template e_KonnektingDeviceStatus KonnektingDevice::write <int>(byte objectIndex, int value);
template e_KonnektingDeviceStatus KonnektingDevice::write <unsigned long>(byte objectIndex, unsigned long value);
template e_KonnektingDeviceStatus KonnektingDevice::write <long>(byte objectIndex, long value);
template e_KonnektingDeviceStatus KonnektingDevice::write <float>(byte objectIndex, float value);
template e_KonnektingDeviceStatus KonnektingDevice::write <double>(byte objectIndex, double value);


// Update any type of com object (rough DPT value shall be provided)
// The Com Object value is updated locally
// And a telegram is sent on the KNX bus if the com object has communication & transmit attributes

e_KonnektingDeviceStatus KonnektingDevice::write(byte objectIndex, byte valuePtr[]) {
    type_tx_action action;
    byte *dptValue;
    byte length = _comObjectsList[objectIndex].GetLength();
    if (length > 2) // check we are in long object case
    { // add WRITE action in the TX action queue
        action.command = KNX_WRITE_REQUEST;
        action.index = objectIndex;
        dptValue = (byte *) malloc(length - 1); // allocate the memory for long value
        for (byte i = 0; i < length - 1; i++) dptValue[i] = valuePtr[i]; // copy value
        action.valuePtr = (byte *) dptValue;
        _txActionList.Append(action);
        return KNX_DEVICE_OK;
    }
    return KNX_DEVICE_ERROR;
}


// Com Object KNX Bus Update request
// Request the local object to be updated with the value from the bus
// NB : the function is asynchroneous, the update completion is notified by the knxEvents() callback

void KonnektingDevice::update(byte objectIndex) {
    type_tx_action action;
    action.command = KNX_READ_REQUEST;
    action.index = objectIndex;
    _txActionList.Append(action);
}


// The function returns true if there is rx/tx activity ongoing, else false

boolean KonnektingDevice::isActive(void) const {
    if (_tpuart->IsActive()) return true; // TPUART is active
    if (_state == TX_ONGOING) return true; // the Device is sending a request
    if (_txActionList.ElementsNb()) return true; // there is at least one tx action in the queue
    return false;
}

// Overwrite the address of an attache Com Object
// Overwriting is allowed only when the KonnektingDevice is in INIT state
// Typically usage is end-user application stored Group Address in EEPROM

e_KonnektingDeviceStatus KonnektingDevice::setComObjectAddress(byte index, word addr) {
    if (_state != INIT) return KNX_DEVICE_ERROR;
    if (index >= _numberOfComObjects) return KNX_DEVICE_INVALID_INDEX;
    _comObjectsList[index].SetAddr(addr);
    return KNX_DEVICE_OK;
}

word KonnektingDevice::getComObjectAddress(byte index) {
    return _comObjectsList[index].GetAddr();
}


// Static GetTpUartEvents() function called by the KnxTpUart layer (callback)

void KonnektingDevice::GetTpUartEvents(e_KnxTpUartEvent event) {
    type_tx_action action;
    byte targetedComObjIndex; // index of the Com Object targeted by the event

    // Manage RECEIVED MESSAGES
    if (event == TPUART_EVENT_RECEIVED_KNX_TELEGRAM) {
        _state = IDLE;
        targetedComObjIndex = _tpuart->GetTargetedComObjectIndex();

        switch (_rxTelegram->GetCommand()) {
            case KNX_COMMAND_VALUE_READ:
                //DebugInfo("READ req.\n");
                // READ command coming from the bus
                // if the Com Object has read attribute, then add RESPONSE action in the TX action list
                if ((_comObjectsList[targetedComObjIndex].GetIndicator()) & KNX_COM_OBJ_R_INDICATOR) { // The targeted Com Object can indeed be read
                    action.command = KNX_RESPONSE_REQUEST;
                    action.index = targetedComObjIndex;
                    _txActionList.Append(action);
                }
                break;

            case KNX_COMMAND_VALUE_RESPONSE:
                //DebugInfo("RESP req.\n");
                // RESPONSE command coming from KNX network, we update the value of the corresponding Com Object.
                // We 1st check that the corresponding Com Object has UPDATE attribute
                if ((_comObjectsList[targetedComObjIndex].GetIndicator()) & KNX_COM_OBJ_U_INDICATOR) {
                    _comObjectsList[targetedComObjIndex].UpdateValue(*(_rxTelegram));
                    //We notify the upper layer of the update
                    knxEvents(targetedComObjIndex);
                }
                break;


            case KNX_COMMAND_VALUE_WRITE:
                //DebugInfo("WRITE req.\n");
                // WRITE command coming from KNX network, we update the value of the corresponding Com Object.
                // We 1st check that the corresponding Com Object has WRITE attribute
                if ((_comObjectsList[targetedComObjIndex].GetIndicator()) & KNX_COM_OBJ_W_INDICATOR) {
                    _comObjectsList[targetedComObjIndex].UpdateValue(*(_rxTelegram));
                    //We notify the upper layer of the update

                    // if it's not a internal com object, route back to knxEvents()
                    if (!_prog->internalComObject(targetedComObjIndex)) {
                        knxEvents(targetedComObjIndex);
                    }
                }
                break;

                // case KNX_COMMAND_MEMORY_WRITE : break; // Memory Write not handled

            default: break; // not supposed to happen
        }
    }

    // Manage RESET events
    if (event == TPUART_EVENT_RESET) {
        while (_tpuart->Reset() == KNX_TPUART_ERROR);
        _tpuart->Init();
        _state = IDLE;
    }
}


// Static TxTelegramAck() function called by the KnxTpUart layer (callback)

void KonnektingDevice::TxTelegramAck(e_TpUartTxAck value) {
    _state = IDLE;
}


// Functions to convert a standard C type to a DPT format
// NB : only the usual DPT formats are supported (U16, V16, U32, V32, F16 and F32 (not yet implemented)

template <typename T> e_KonnektingDeviceStatus ConvertFromDpt(const byte dptOriginValue[], T& resultValue, byte dptFormat) {
    switch (dptFormat) {
        case KNX_DPT_FORMAT_U16:
        case KNX_DPT_FORMAT_V16:
            resultValue = (T) ((unsigned int) dptOriginValue[0] << 8);
            resultValue += (T) (dptOriginValue[1]);
            return KNX_DEVICE_OK;
            break;

        case KNX_DPT_FORMAT_U32:
        case KNX_DPT_FORMAT_V32:
            resultValue = (T) ((unsigned long) dptOriginValue[0] << 24);
            resultValue += (T) ((unsigned long) dptOriginValue[1] << 16);
            resultValue += (T) ((unsigned long) dptOriginValue[2] << 8);
            resultValue += (T) (dptOriginValue[3]);
            return KNX_DEVICE_OK;
            break;

        case KNX_DPT_FORMAT_F16:
        {
            // Get the DPT sign, mantissa and exponent
            int signMultiplier = (dptOriginValue[0] & 0x80) ? -1 : 1;
            word absoluteMantissa = dptOriginValue[1] + ((dptOriginValue[0]&0x07) << 8);
            if (signMultiplier == -1) { // Calculate absolute mantissa value in case of negative mantissa
                // Abs = 2's complement + 1
                absoluteMantissa = ((~absoluteMantissa)& 0x07FF) + 1;
            }
            byte exponent = (dptOriginValue[0]&0x78) >> 3;
            resultValue = (T) (0.01 * ((long) absoluteMantissa << exponent) * signMultiplier);
            return KNX_DEVICE_OK;
        }
            break;

        case KNX_DPT_FORMAT_F32:
            return KNX_DEVICE_NOT_IMPLEMENTED;
            break;

        default: KNX_DEVICE_ERROR;
    }
}

template e_KonnektingDeviceStatus ConvertFromDpt <unsigned char>(const byte dptOriginValue[], unsigned char&, byte dptFormat);
template e_KonnektingDeviceStatus ConvertFromDpt <char>(const byte dptOriginValue[], char&, byte dptFormat);
template e_KonnektingDeviceStatus ConvertFromDpt <unsigned int>(const byte dptOriginValue[], unsigned int&, byte dptFormat);
template e_KonnektingDeviceStatus ConvertFromDpt <int>(const byte dptOriginValue[], int&, byte dptFormat);
template e_KonnektingDeviceStatus ConvertFromDpt <unsigned long>(const byte dptOriginValue[], unsigned long&, byte dptFormat);
template e_KonnektingDeviceStatus ConvertFromDpt <long>(const byte dptOriginValue[], long&, byte dptFormat);
template e_KonnektingDeviceStatus ConvertFromDpt <float>(const byte dptOriginValue[], float&, byte dptFormat);
template e_KonnektingDeviceStatus ConvertFromDpt <double>(const byte dptOriginValue[], double&, byte dptFormat);


// Functions to convert a standard C type to a DPT format
// NB : only the usual DPT formats are supported (U16, V16, U32, V32, F16 and F32 (not yet implemented)

template <typename T> e_KonnektingDeviceStatus ConvertToDpt(T originValue, byte dptDestValue[], byte dptFormat) {
    switch (dptFormat) {
        case KNX_DPT_FORMAT_U16:
        case KNX_DPT_FORMAT_V16:
            dptDestValue[0] = (byte) ((unsigned int) originValue >> 8);
            dptDestValue[1] = (byte) (originValue);
            return KNX_DEVICE_OK;
            break;

        case KNX_DPT_FORMAT_U32:
        case KNX_DPT_FORMAT_V32:
            dptDestValue[0] = (byte) ((unsigned long) originValue >> 24);
            dptDestValue[1] = (byte) ((unsigned long) originValue >> 16);
            dptDestValue[2] = (byte) ((unsigned long) originValue >> 8);
            dptDestValue[3] = (byte) (originValue);
            return KNX_DEVICE_OK;
            break;

        case KNX_DPT_FORMAT_F16:
        {
            long longValuex100 = (long) (100.0 * originValue);
            boolean negativeSign = (longValuex100 & 0x80000000) ? true : false;
            byte exponent = 0;
            byte round = 0;

            if (negativeSign) {
                while (longValuex100 < (long) (-2048)) {
                    exponent++;
                    round = (byte) (longValuex100) & 1;
                    longValuex100 >>= 1;
                    longValuex100 |= 0x80000000;
                }
            } else {
                while (longValuex100 > (long) (2047)) {
                    exponent++;
                    round = (byte) (longValuex100) & 1;
                    longValuex100 >>= 1;
                }

            }
            if (round) longValuex100++;
            dptDestValue[1] = (byte) longValuex100;
            dptDestValue[0] = (byte) (longValuex100 >> 8) & 0x7;
            dptDestValue[0] += exponent << 3;
            if (negativeSign) dptDestValue[0] += 0x80;
            return KNX_DEVICE_OK;
        }
            break;

        case KNX_DPT_FORMAT_F32:
            return KNX_DEVICE_NOT_IMPLEMENTED;
            break;

        default: KNX_DEVICE_ERROR;
    }
}

template e_KonnektingDeviceStatus ConvertToDpt <unsigned char>(unsigned char, byte dptDestValue[], byte dptFormat);
template e_KonnektingDeviceStatus ConvertToDpt <char>(char, byte dptDestValue[], byte dptFormat);
template e_KonnektingDeviceStatus ConvertToDpt <unsigned int>(unsigned int, byte dptDestValue[], byte dptFormat);
template e_KonnektingDeviceStatus ConvertToDpt <int>(int, byte dptDestValue[], byte dptFormat);
template e_KonnektingDeviceStatus ConvertToDpt <unsigned long>(unsigned long, byte dptDestValue[], byte dptFormat);
template e_KonnektingDeviceStatus ConvertToDpt <long>(long, byte dptDestValue[], byte dptFormat);
template e_KonnektingDeviceStatus ConvertToDpt <float>(float, byte dptDestValue[], byte dptFormat);
template e_KonnektingDeviceStatus ConvertToDpt <double>(double, byte dptDestValue[], byte dptFormat);


// --------------------------------------------

KonnektingProg* KonnektingDevice::getProg() {
    return _prog;
}

void KonnektingDevice::setDebugSerial(Print* debugSerial){
    _debugSerial = debugSerial;
}

boolean KonnektingDevice::hasDebugSerial() {
    return _debugSerial != NULL;
}

Print* KonnektingDevice::getDebugSerial(){
    return _debugSerial;
}

// EOF
