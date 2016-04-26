// include KonnektingDevice library
#include <KonnektingDevice.h>

// for debugging via softserial, not required when using any HardwareSerial
#include <SoftwareSerial.h>


// #############################################################################
// ### KONNEKTING DEVICE SETTINGS

#define MANUFACTURER_ID 57005
#define DEVICE_ID 190
#define REVISION 175

// define programming  button + LED
#define PROG_BUTTON_PIN 3
#define PROG_LED_PIN LED_BUILTIN

// define KNX Transceiver serial port
#ifdef __AVR_ATmega328P__
    #define KNX_SERIAL Serial // Nano/ProMini etc. use Serial
#else
    #define KNX_SERIAL Serial1 // Leonardo/Micro etc. use Serial1
#endif

// create com objects
KnxComObject _comObjectsList[] = {
    /* Index 0 : */ KnxComObject(G_ADDR(0, 0, 1), KNX_DPT_1_001, COM_OBJ_LOGIC_IN),
    /* Index 1 : */ KnxComObject(G_ADDR(0, 0, 2), KNX_DPT_1_001, COM_OBJ_SENSOR),
};

// create parameter size definition
byte _paramSizeList[] = {
    /* Param Index 0 */ PARAM_UINT8,
    /* Param Index 1 */ PARAM_INT16,
    /* Param Index 2 */ PARAM_UINT32,
    /* Param Index 3 */ PARAM_UINT8,
    /* Param Index 4 */ PARAM_UINT8,
    /* Param Index 5 */ PARAM_UINT8,
    /* Param Index 6 */ PARAM_UINT8,
    /* Param Index 7 */ PARAM_UINT8,
    /* Param Index 8 */ PARAM_UINT8,
    /* Param Index 9 */ PARAM_UINT8,
};

// Create KONNEKTING Instance
KonnektingDevice konnekting;

// #############################################################################
// ### SKETCH

bool state = false;

// Callback function to handle com objects updates
void knxEvents(byte index) {
        
    // toggle led state, just for "visual testing"
    state = !state;

    if (state) {
        digitalWrite(PROG_LED_PIN, HIGH);
        konnekting.write(2, true );
    } else {
        digitalWrite(PROG_LED_PIN, LOW);
        konnekting.write(2, false );
    }
    
};

void setup() {
       
    // set well defined state for LED pin for this special sample. Can be skipped in other sketches.
    pinMode(PROG_LED_PIN, OUTPUT);
    digitalWrite(PROG_LED_PIN, LOW);
    
    // remove if not using debugging capabilities
    SoftwareSerial debugSerial(11, 10); // RX, TX
    debugSerial.begin(9600);
    konnekting.setDebugSerial(&debugSerial);
    
    // Initialize KNX enabled Arduino Board
    konnekting.init(/* KNX transceiver serial port */ KNX_SERIAL, 
            /* CONOBJ list */ _comObjectsList, 
            /* Parameter size list*/ _paramSizeList,
            /* Prog Button Pin */ PROG_BUTTON_PIN, 
            /* Prog LED Pin */ PROG_LED_PIN, 
            /* manufacturer */ MANUFACTURER_ID, 
            /* device */ DEVICE_ID, 
            /* revision */ REVISION);
    
    debugSerial.print("param #0: ");
    debugSerial.print(konnekting.getProg()->getUINT8Param(0));
    debugSerial.println("");
    
    debugSerial.print("param #1: ");
    debugSerial.print(konnekting.getProg()->getINT16Param(1));
    debugSerial.println("");
    
    debugSerial.print("param #2: ");
    debugSerial.print(konnekting.getProg()->getUINT32Param(2));
    debugSerial.println("");

}

void loop() {
    konnekting.task();
}
