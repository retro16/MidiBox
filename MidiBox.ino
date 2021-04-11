/*
   MIDI box for STM32

Pin mapping:

3.3 : SD VDD (SD pin 4)
GND : SD VSS (SD pin 3)

PA2 : MIDI 1 TX
PA3 : MIDI 1 RX
PA4 : SD CS (SD pin 1)
PA5 : SD SCK (SD pin 5)
PA6 : SD MISO (SD pin 7)
PA7 : SD MOSI (SD pin 2)
PA8 : Gate 1
PA9 : MIDI MUX TX
PA10: MIDI MUX RX
PA11: USB-
PA12: USB+
PA15: Gate 3

PB1 : Gate 2
PB3 : Gate 4
PB4 : Gate 5
PB5 : Gate 6
PB6 : SCL (LCD)
PB7 : SDA (LCD)
PB8 : Gate 7
PB9 : Gate 8
PB10: MIDI 2 TX
PB11: MIDI 2 RX
PB12: UP
PB13: DOWN
PB14: LEFT
PB15: RIGHT

PC13: LED
*/

// Compile-time settings
#define MIDIBOX_VERSION "1.0"
#define MIDIBOX_USB_SERIAL 0
#define MIDIBOX_USB_MIDI 1
#define MIDIBOX_EXT_COUNT 0
#define MIDIBOX_GATES 0

static const int SD_CS = PA4;
static const char *sysExPath = "/JMC_MIDI/SYSEX/";
static const char *settingsPath = "/JMC_MIDI/SETTINGS/";

// Includes
#include <USBComposite.h>
#include "Midi.h"
#include "Menu.h"

// Globals

#if MIDIBOX_USB_SERIAL || MIDIBOX_USB_MIDI
#define MIDIBOX_USB 1
#endif

MidiSerialPort midi1("MIDI IN 1", "MIDI OUT 1", Serial2);
MidiSerialPort midi2("MIDI IN 2", "MIDI OUT 2", Serial3);
#if MIDIBOX_EXT_COUNT == 0
MidiSerialPort midi3("MIDI IN 3", "MIDI OUT 3", Serial1);
#endif
#if MIDIBOX_EXT_COUNT >= 1
MidiSerialMux serialMux(Serial1);
MidiSerialMuxPort midiMux1("EXT1 MIDI IN 1", "EXT1 MIDI OUT 1", serialMux, 0);
MidiSerialMuxPort midiMux2("EXT1 MIDI IN 2", "EXT1 MIDI OUT 2", serialMux, 1);
#endif
#if MIDIBOX_EXT_COUNT >= 2
MidiSerialMuxPort midiMux3("EXT2 MIDI IN 3", "EXT2 MIDI OUT 3", serialMux, 2);
MidiSerialMuxPort midiMux4("EXT2 MIDI IN 4", "EXT2 MIDI OUT 4", serialMux, 3);
#endif
#if MIDIBOX_EXT_COUNT >= 3
MidiSerialMuxPort midiMux5("EXT3 MIDI IN 5", "EXT3 MIDI OUT 5", serialMux, 4);
MidiSerialMuxPort midiMux6("EXT3 MIDI IN 6", "EXT3 MIDI OUT 6", serialMux, 5);
#endif
#if MIDIBOX_EXT_COUNT >= 4
MidiSerialMuxPort midiMux7("EXT4 MIDI IN 7", "EXT4 MIDI OUT 7", serialMux, 6);
MidiSerialMuxPort midiMux8("EXT4 MIDI IN 8", "EXT4 MIDI OUT 8", serialMux, 7);
#endif
#if MIDIBOX_USB_MIDI
MidiUSBPort usb1("USB PORT", "USB PORT", 0); // USB-MIDI interface
#endif
MidiLoopback bus1("LOOPBACK BUS 1"); // Internal bus
MidiLoopback bus2("LOOPBACK BUS 2"); // Internal bus
MidiParaphonyMapper paraBus("PARAPHONIC MAP."); // Paraphonic mapper
#if MIDIBOX_GATES
MidiGpioGate gates("ANALOG GATES", PA8, PB1, PA15, PB3, PB4, PB5, PB8, PB9);
#endif
MidiIn *inputs[] = {
  &midi1,
  &midi2,
#if MIDIBOX_EXT_COUNT == 0
  &midi3,
#endif
#if MIDIBOX_EXT_COUNT >= 1
  &midiMux1,
  &midiMux2,
#endif
#if MIDIBOX_EXT_COUNT >= 2
  &midiMux3,
  &midiMux4,
#endif
#if MIDIBOX_EXT_COUNT >= 3
  &midiMux5,
  &midiMux6,
#endif
#if MIDIBOX_EXT_COUNT >= 4
  &midiMux7,
  &midiMux8,
#endif
#if MIDIBOX_USB_MIDI
  &usb1,
#endif
  &bus1,
  &bus2,
  &paraBus
};
const int inputCount = sizeof(inputs)/sizeof(inputs[0]);
const int repeaterInputCount = 10;
MidiOut *outputs[] = {
  &midi1,
  &midi2,
#if MIDIBOX_EXT_COUNT >= 1
  &midiMux1,
  &midiMux2,
#endif
#if MIDIBOX_EXT_COUNT >= 2
  &midiMux3,
  &midiMux4,
#endif
#if MIDIBOX_EXT_COUNT >= 3
  &midiMux5,
  &midiMux6,
#endif
#if MIDIBOX_EXT_COUNT >= 4
  &midiMux7,
  &midiMux8,
#endif
#if MIDIBOX_USB_MIDI
  &usb1,
#endif
  &bus1,
  &bus2,
#if MIDIBOX_GATES
  &gates,
#endif
  &paraBus
};
const int outputCount = sizeof(outputs)/sizeof(outputs[0]);
#if MIDIBOX_EXT_COUNT
const int repeaterOutputCount = (MIDIBOX_EXT_COUNT) * 2 + 2;
#endif

// Global variables
MidiIn *sysExSource = NULL;
MidiOut *sysExTarget = NULL;
char currentProfile = 1;
#if MIDIBOX_EXT_COUNT
char repeater = 0;
#endif
int pollMillis = 0;
int blinkPhase = 0;

#if MIDIBOX_USB_SERIAL
// USB serial for firmware and configuration upload
USBCompositeSerial usbSerial;
#endif

// Global functions
void routeMidi();

// Select a MIDI input or output
template<typename StreamType>
struct MenuStreamSelect: public MenuItem {
  MenuStreamSelect(const char *name_, MenuItem &subMenu_, StreamType **streams_, int streamCount_):
    MenuItem(name_),
    subMenu(subMenu_),
    streams(streams_),
    streamCount(streamCount_) {
    if(&subMenu != this)
      subMenu.parent = this;
  }
  virtual void onEnter() {
    stream = 0;
  }
  virtual const char * line2() {
    return streams[stream]->name;
  }
  virtual MenuItem * onKeyPressed(int keys) {
    if(keys == Menu::KEY_LEFT && stream > 0)
      --stream;
    else if(keys == Menu::KEY_RIGHT && stream < streamCount - 1)
      ++stream;
    else if(keys == Menu::KEY_DOWN)
      return &subMenu;
    return this;
  }
  MenuItem &subMenu;
  StreamType **streams;
  int streamCount;
  int stream;
};

static const char *msgTypeName[] = {
  "CHANNEL 1",
  "CHANNEL 2",
  "CHANNEL 3",
  "CHANNEL 4",
  "CHANNEL 5",
  "CHANNEL 6",
  "CHANNEL 7",
  "CHANNEL 8",
  "CHANNEL 9",
  "CHANNEL 10",
  "CHANNEL 11",
  "CHANNEL 12",
  "CHANNEL 13",
  "CHANNEL 14",
  "CHANNEL 15",
  "CHANNEL 16",
  "SYSTEM EXCL.", // F0
  "TIME CODE", // F1
  "SONG POSITION", // F2
  "SONG SELECT", // F3
  "TYPE F4", // F4
  "TYPE F5", // F5
  "TUNE REQUEST", // F6
  "SYSTEM EX END", // F7
  "MIDI CLOCK", // F8
  "TYPE F9", // F9
  "START", // FA
  "CONTINUE", // FB
  "STOP", // FC
  "TYPE FD", // FD
  "ACTIVE SENSE", // FE
  "RESET" // FF
};

const char *getSettingsPath() {
  static char fileName[256];
  strcpy(fileName, settingsPath);
  int l = strlen(fileName);
  fileName[l] = currentProfile + '0';
  strcpy(&fileName[l+1], ".TXT");
  return fileName;
}

struct MenuFilterEdit: public MenuItem {
  MenuFilterEdit(const char *name_): MenuItem(name_), mask(0) {}

  virtual void onEnter() {
    msgType = -2;
  }

  virtual const char * line2() {
    if(msgType == -2)
      return "ENABLE ALL";
    if(msgType == -1)
      return "DISABLE ALL";
    bool copyName = true;
    for(int i = 0; i < 15; ++i) {
      if(copyName) {
        char c = msgTypeName[msgType][i];
        if(c) {
          displayLine[i] = c;
        } else {
          displayLine[i] = ' ';
          copyName = false;
        }
      } else {
        displayLine[i] = ' ';
      }
    }
    displayLine[15] = (mask & (1 << msgType)) ? (char)0x7E /* -> */ : 'x';
    displayLine[16] = '\0';
    return displayLine;
  }

  virtual MenuItem * onKeyPressed(int keys) {
    if(keys == Menu::KEY_LEFT && msgType > -2)
      --msgType;
    else if(keys == Menu::KEY_RIGHT && msgType < 31)
      ++msgType;
    else if(keys == Menu::KEY_DOWN) {
      if(msgType == -2)
        mask = ~0;
      else if(msgType == -1)
        mask = 0;
      else
        mask ^= (1 << msgType);
    }
    return this;
  }

  char displayLine[17];
  int msgType; // 0-31
  int mask;
};

struct MenuProfileSelector: public MenuNumberSelect {
  MenuProfileSelector(const char *name_, MenuItem &subMenu_):
    MenuNumberSelect(name_, subMenu_, 1, 9) {
    strcpy(l2, "PROFILE ?");
  }
  virtual void onEnter() {
    number = currentProfile;
    l2[8] = '0' + currentProfile;
    loadSettings();
  }
  virtual MenuItem * onKeyPressed(int keys) {
    MenuItem *nextMenu = MenuNumberSelect::onKeyPressed(keys);
    currentProfile = number;
    if(keys != Menu::KEY_DOWN)
      onEnter();
    return nextMenu;
  }
  virtual const char * line2() {
    return l2;
  }
  char l2[17];
};

struct MenuSaveSettings: public MenuConfirm {
  MenuSaveSettings(const char *name_): MenuConfirm(name_, "DOWN TO SAVE") {}
  virtual void onConfirmed() {
    if(SD.begin(SD_CS)) {
      SD.mkdir(settingsPath);
      SD.remove(getSettingsPath());
      File f = SD.open(getSettingsPath(), FILE_WRITE);
      if(f)
        saveSettings(&f);
      f.close();
      SD.end();
    }
  }
};

struct MenuSyncDivider: public MenuNumberSelect {
  MenuSyncDivider(const char *name_):
    MenuNumberSelect(name_, *this, 1, 64) {}
  virtual void onEnter() {
    MenuNumberSelect *routeMenu = (MenuNumberSelect *)(parent->parent);
    MenuStreamSelect<MidiIn> *inputMenu = (MenuStreamSelect<MidiIn> *)(routeMenu->parent);
    MidiRoute &stream = inputs[inputMenu->stream]->getRoute(routeMenu->number - 1);
    number = stream.getSyncDivider();
  }
  virtual MenuItem * onKeyPressed(int keys) {
    MenuItem *nextMenu = MenuNumberSelect::onKeyPressed(keys);
    MenuNumberSelect *routeMenu = (MenuNumberSelect *)(parent->parent);
    MenuStreamSelect<MidiIn> *inputMenu = (MenuStreamSelect<MidiIn> *)(routeMenu->parent);
    MidiRoute &stream = inputs[inputMenu->stream]->getRoute(routeMenu->number - 1);
    stream.setSyncDivider(number);
    return nextMenu;
  }
};

struct MenuChannelMap: public MenuNumberSelect {
  MenuChannelMap(const char *name_):
    MenuNumberSelect(name_, *this, 0, 16) {}
  virtual void onEnter() {
    MenuNumberSelect *channelMenu = (MenuNumberSelect *)(parent->parent);
    MenuNumberSelect *routeMenu = (MenuNumberSelect *)(channelMenu->parent->parent);
    MenuStreamSelect<MidiIn> *inputMenu = (MenuStreamSelect<MidiIn> *)(routeMenu->parent);
    MidiRoute &stream = inputs[inputMenu->stream]->getRoute(routeMenu->number - 1);
    number = stream.getChannelMapping(channelMenu->number);
  }
  virtual MenuItem * onKeyPressed(int keys) {
    MenuItem *nextMenu = MenuNumberSelect::onKeyPressed(keys);
    MenuNumberSelect *channelMenu = (MenuNumberSelect *)(parent->parent);
    MenuNumberSelect *routeMenu = (MenuNumberSelect *)(channelMenu->parent->parent);
    MenuStreamSelect<MidiIn> *inputMenu = (MenuStreamSelect<MidiIn> *)(routeMenu->parent);
    MidiRoute &stream = inputs[inputMenu->stream]->getRoute(routeMenu->number - 1);
    stream.setChannelMapping(channelMenu->number, number);
    return nextMenu;
  }
};

struct MenuTranspose: public MenuNumberSelect {
  MenuTranspose(const char *name_):
    MenuNumberSelect(name_, *this, -127, 127) {}
  virtual void onEnter() {
    MenuNumberSelect *channelMenu = (MenuNumberSelect *)(parent->parent);
    MenuNumberSelect *routeMenu = (MenuNumberSelect *)(channelMenu->parent->parent);
    MenuStreamSelect<MidiIn> *inputMenu = (MenuStreamSelect<MidiIn> *)(routeMenu->parent);
    MidiRoute &stream = inputs[inputMenu->stream]->getRoute(routeMenu->number - 1);
    number = stream.getTransposition(channelMenu->number);
  }
  virtual MenuItem * onKeyPressed(int keys) {
    MenuItem *nextMenu = MenuNumberSelect::onKeyPressed(keys);
    MenuNumberSelect *channelMenu = (MenuNumberSelect *)(parent->parent);
    MenuNumberSelect *routeMenu = (MenuNumberSelect *)(channelMenu->parent->parent);
    MenuStreamSelect<MidiIn> *inputMenu = (MenuStreamSelect<MidiIn> *)(routeMenu->parent);
    MidiRoute &stream = inputs[inputMenu->stream]->getRoute(routeMenu->number - 1);
    stream.transpose(channelMenu->number, number);
    return nextMenu;
  }
};

struct MenuVelocityScale: public MenuNumberSelect {
  MenuVelocityScale(const char *name_):
    MenuNumberSelect(name_, *this, 0, 100) {}
  virtual void onEnter() {
    MenuNumberSelect *channelMenu = (MenuNumberSelect *)(parent->parent);
    MenuNumberSelect *routeMenu = (MenuNumberSelect *)(channelMenu->parent->parent);
    MenuStreamSelect<MidiIn> *inputMenu = (MenuStreamSelect<MidiIn> *)(routeMenu->parent);
    MidiRoute &stream = inputs[inputMenu->stream]->getRoute(routeMenu->number - 1);
    number = stream.getVelocityScale(channelMenu->number);
  }
  virtual MenuItem * onKeyPressed(int keys) {
    MenuItem *nextMenu = MenuNumberSelect::onKeyPressed(keys);
    MenuNumberSelect *channelMenu = (MenuNumberSelect *)(parent->parent);
    MenuNumberSelect *routeMenu = (MenuNumberSelect *)(channelMenu->parent->parent);
    MenuStreamSelect<MidiIn> *inputMenu = (MenuStreamSelect<MidiIn> *)(routeMenu->parent);
    MidiRoute &stream = inputs[inputMenu->stream]->getRoute(routeMenu->number - 1);
    stream.setVelocityScale(channelMenu->number, number);
    return nextMenu;
  }
};

struct MenuVelocityOffset: public MenuNumberSelect {
  MenuVelocityOffset(const char *name_):
    MenuNumberSelect(name_, *this, -127, 127) {}
  virtual void onEnter() {
    MenuNumberSelect *channelMenu = (MenuNumberSelect *)(parent->parent);
    MenuNumberSelect *routeMenu = (MenuNumberSelect *)(channelMenu->parent->parent);
    MenuStreamSelect<MidiIn> *inputMenu = (MenuStreamSelect<MidiIn> *)(routeMenu->parent);
    MidiRoute &stream = inputs[inputMenu->stream]->getRoute(routeMenu->number - 1);
    number = stream.getVelocityOffset(channelMenu->number);
  }
  virtual MenuItem * onKeyPressed(int keys) {
    MenuItem *nextMenu = MenuNumberSelect::onKeyPressed(keys);
    MenuNumberSelect *channelMenu = (MenuNumberSelect *)(parent->parent);
    MenuNumberSelect *routeMenu = (MenuNumberSelect *)(channelMenu->parent->parent);
    MenuStreamSelect<MidiIn> *inputMenu = (MenuStreamSelect<MidiIn> *)(routeMenu->parent);
    MidiRoute &stream = inputs[inputMenu->stream]->getRoute(routeMenu->number - 1);
    stream.setVelocityOffset(channelMenu->number, number);
    return nextMenu;
  }
};

struct MenuChannelFilter: public MenuFilterEdit {
  MenuChannelFilter(const char *name_): MenuFilterEdit(name_) {}
  virtual void onEnter() {
    MenuFilterEdit::onEnter();
    MenuNumberSelect *routeMenu = (MenuNumberSelect *)(parent->parent);
    MenuStreamSelect<MidiIn> *inputMenu = (MenuStreamSelect<MidiIn> *)(routeMenu->parent);
    MidiRoute &stream = inputs[inputMenu->stream]->getRoute(routeMenu->number - 1);
    mask = stream.getFilter();
  }
  virtual MenuItem * onKeyPressed(int keys) {
    MenuItem *nextMenu = MenuFilterEdit::onKeyPressed(keys);
    MenuNumberSelect *routeMenu = (MenuNumberSelect *)(parent->parent);
    MenuStreamSelect<MidiIn> *inputMenu = (MenuStreamSelect<MidiIn> *)(routeMenu->parent);
    MidiRoute &stream = inputs[inputMenu->stream]->getRoute(routeMenu->number - 1);
    stream.setFilter(mask);
    return nextMenu;
  }
};

struct MenuOutputRoute: public MenuStreamSelect<MidiOut> {
  MenuOutputRoute(const char *name_): MenuStreamSelect(name_, *this, outputs, outputCount) {}
  virtual void onEnter() {
    MenuNumberSelect *routeMenu = (MenuNumberSelect *)(parent->parent);
    MenuStreamSelect<MidiIn> *inputMenu = (MenuStreamSelect<MidiIn> *)(routeMenu->parent);
    MidiRoute &mux = inputs[inputMenu->stream]->getRoute(routeMenu->number - 1);
    for(stream = 0; stream < outputCount; ++stream) {
      if(mux.out == outputs[stream])
        return;
    }
    stream = 0;
  }
  virtual MenuItem * onKeyPressed(int keys) {
    MenuItem *nextMenu = MenuStreamSelect::onKeyPressed(keys);
    MenuNumberSelect *routeMenu = (MenuNumberSelect *)(parent->parent);
    MenuStreamSelect<MidiIn> *inputMenu = (MenuStreamSelect<MidiIn> *)(routeMenu->parent);
    MidiRoute &mux = inputs[inputMenu->stream]->getRoute(routeMenu->number - 1);
    mux.out = outputs[stream];
    return nextMenu;
  }
};

struct MenuSysExRecord: public MenuItem {
  MenuSysExRecord(const char *name_): MenuItem(name_) {}

  virtual void onEnter() {
    MenuStreamSelect<MidiIn> *streamSelect = (MenuStreamSelect<MidiIn> *)parent;
    MidiIn *port = inputs[streamSelect->stream];
    if(!SD.begin(SD_CS)) {
      return;
    }
    SD.mkdir(sysExPath);
    char fileName[128];

    // Find an available file
    int i;
    for(i = 0; i < 10000; ++i) {
      snprintf(fileName, sizeof(fileName), "%sREC%04d.SYX", sysExPath, i);
      if(!SD.exists(fileName)) {
        // File found
        break;
      }
    }
    if(i >= 10000) {
      // Directory full !
      SD.end();
      return;
    }
    file = SD.open(fileName, FILE_WRITE);
    fileStream.setFile(&file);
    sysExSource = port;
    sysExTarget = &fileStream;
  }

  virtual const char * line2() {
    if(!file) {
      return "SD CARD ERROR";
    }
    return "ANY KEY TO STOP";
  }

  virtual MenuItem * onKeyPressed(int keys) {
    return parent;
  }

  virtual void onExit() {
    sysExSource = NULL;
    sysExTarget = NULL;
    if(file) {
      file.close();
      SD.end();
    }
  }

  File file;
  SysExFileRecorder fileStream;
};

struct MenuSysExReplay: public MenuItem {
  MenuSysExReplay(const char *name_): MenuItem(name_) {}
  virtual void onEnter() {
    MenuStreamSelect<MidiOut> *streamSelect = (MenuStreamSelect<MidiOut> *)(parent->parent);
    MidiOut *port = outputs[streamSelect->stream];
    fileStream.setFile(&((MenuFileSelect *)parent)->file);
    sysExSource = &fileStream;
    sysExTarget = port;
  }

  virtual const char * line2() {
    return "SENDING ...";
  }

  virtual MenuItem * onKeyPressed(int keys) {
    return parent;
  }

  virtual void onExit() {
    sysExSource = NULL;
    sysExTarget = NULL;
  }

  SysExFilePlayer fileStream;
};

struct MenuPanic: public MenuConfirm {
  MenuPanic(const char *name_): MenuConfirm(name_, "DOWN: ALL NOTEOFF") {}
  virtual void onConfirmed() {
    for(int i = 0; i < outputCount; ++i) {
      MidiOut *output = outputs[i];
      for(int c = 0; c < 16; ++c) {
        // Wait until the port is ready ...
        // It's done that way because it's an emergency situation
        while(!output->availableForWrite(0, this));
          routeMidi();

        // Send the actual "Note Off" message
        output->write(MIDI_CTL | c, this);
        output->write(123, this);
        output->write(0, this);
      }
    }
  }
};

struct MenuResetChannelProcessing: public MenuConfirm {
  MenuResetChannelProcessing(const char *name_): MenuConfirm(name_, "DOWN TO RESET") {}
  virtual void onConfirmed() {
    MenuNumberSelect *routeMenu = (MenuNumberSelect *)(parent->parent);
    MenuStreamSelect<MidiIn> *inputMenu = (MenuStreamSelect<MidiIn> *)(routeMenu->parent);
    MidiRoute &stream = inputs[inputMenu->stream]->getRoute(routeMenu->number - 1);
    stream.resetProcessing();
  }
};

struct MenuResetProfile: public MenuConfirm {
  MenuResetProfile(const char *name_): MenuConfirm(name_, "DOWN TO RESET") {}
  virtual void onConfirmed() {
    resetSettings();
  }
};

struct MenuParaTargetChannel: public MenuNumberSelect {
  MenuParaTargetChannel(const char *name_):
    MenuNumberSelect(name_, *this, 1, 16) {}
  virtual void onEnter() {
    MenuNumberSelect *channelMenu = (MenuNumberSelect *)(parent->parent);
    number = paraBus.getNextChannel(channelMenu->number);
  }
  virtual MenuItem * onKeyPressed(int keys) {
    MenuItem *nextMenu = MenuNumberSelect::onKeyPressed(keys);
    MenuNumberSelect *channelMenu = (MenuNumberSelect *)(parent->parent);
    paraBus.setNextChannel(channelMenu->number, number);
    return nextMenu;
  }
};

struct MenuParaPolyphony: public MenuNumberSelect {
  MenuParaPolyphony(const char *name_):
    MenuNumberSelect(name_, *this, 1, MidiParaphonyMapper::maxPoly) {}
  virtual void onEnter() {
    MenuNumberSelect *channelMenu = (MenuNumberSelect *)(parent->parent);
    number = paraBus.getPolyphony(channelMenu->number);
  }
  virtual MenuItem * onKeyPressed(int keys) {
    MenuItem *nextMenu = MenuNumberSelect::onKeyPressed(keys);
    MenuNumberSelect *channelMenu = (MenuNumberSelect *)(parent->parent);
    paraBus.setPolyphony(channelMenu->number, number);
    return nextMenu;
  }
};

struct MenuDeleteRoute: public MenuConfirm {
  MenuDeleteRoute(const char *name_): MenuConfirm(name_, "DOWN TO DELETE") {}
  virtual MenuItem * onKeyPressed(int keys) {
    if(keys == Menu::KEY_DOWN) {
      MenuNumberSelect *routeMenu = (MenuNumberSelect *)(parent->parent);
      MenuStreamSelect<MidiIn> *inputMenu = (MenuStreamSelect<MidiIn> *)(routeMenu->parent);
      inputs[inputMenu->stream]->deleteRoute(routeMenu->number - 1);
      return parent->parent->parent;
    }
    return MenuConfirm::onKeyPressed(keys);
  }
};

MenuConfirm menuNoMoreRoute(" CANNOT CREATE","      ROUTE");
struct MenuRouteSelect: public MenuNumberSelect {
  MenuRouteSelect(const char *name_, MenuItem &subMenu_):
    MenuNumberSelect(name_, subMenu_, 1, 1) {}

  virtual void onEnter() {
    MenuNumberSelect::onEnter();
    MidiIn *in = inputs[((MenuStreamSelect<MidiIn> *)parent)->stream];
    maximum = in->inRouteCount;
    if(MidiIn::countRoutes() < MidiIn::maxRouteCount && maximum < MidiIn::maxRoutes)
      ++maximum;
  }

  virtual MenuItem * onKeyPressed(int keys) {
    if(keys == Menu::KEY_DOWN) {
      MidiIn *in = inputs[((MenuStreamSelect<MidiIn> *)parent)->stream];
      if(number > in->inRouteCount) {
        // Create a new route
        if(!in->createRoute(&midi1))
          return &menuNoMoreRoute;
      }
      return &subMenu;
    }
    return MenuNumberSelect::onKeyPressed(keys);
  }

};

MenuParaPolyphony paraPolyphony("CHAN. POLYPHONY");
MenuParaTargetChannel paraTargetChannel("NEXT CHANNEL");
MenuList paraSettings("PARA. SETTINGS", paraPolyphony, paraTargetChannel);
MenuNumberSelect connectionsParaChannel("PARA. CHANNEL", paraSettings, 1, 16);
MenuPanic menuPanic("ALL NOTES OFF");
MenuResetProfile menuResetProfile("RESET PROFILE");
MenuSaveSettings menuSaveSettings("SAVE PROFILE");
MenuSysExRecord sysExRecordStart("RECORDING SYSEX");
MenuSysExReplay sysExReplayStart("REPLAYING SYSEX");
MenuFileSelect sysExReplayFile("SYSEX REPLAY", sysExReplayStart, SD_CS, sysExPath);
MenuStreamSelect<MidiIn> sysExRecordPort("RECORD FROM", sysExRecordStart, inputs, inputCount);
MenuStreamSelect<MidiOut> sysExReplayPort("REPLAY TO", sysExReplayFile, outputs, outputCount);
MenuResetChannelProcessing resetChannelProcessing("RESET CHAN PROC.");
MenuTranspose transposeMenu("TRANSPOSE SEMI.");
MenuVelocityScale velocityScaleMenu("VELOCITY SCALE");
MenuVelocityOffset velocityOffsetMenu("VELOCITY OFFSET");
MenuChannelMap mapChannelMenu("MAP TO CHANNEL");
MenuList processingMenu("CHANNEL PROCESS.", mapChannelMenu, transposeMenu, velocityScaleMenu, velocityOffsetMenu);
MenuNumberSelect connectionsChannelProcessing("CHANNEL PROCESS.", processingMenu, 0, 16);
MenuSyncDivider connectionsSyncDivider("CLOCK DIVIDER");
MenuDeleteRoute deleteRoute("DELETE ROUTE");
MenuChannelFilter connectionsFilterMenu("ROUTE FILTER");
MenuOutputRoute connectionsOutputMenu("OUTPUT");
MenuList connectionsSetupMenu("ROUTE SETUP", connectionsOutputMenu, connectionsFilterMenu, deleteRoute, connectionsSyncDivider, connectionsChannelProcessing, resetChannelProcessing);
MenuRouteSelect connectionsRouteMenu("ROUTE", connectionsSetupMenu);
MenuStreamSelect<MidiIn> connectionsMenu("CONNECTIONS", connectionsRouteMenu, inputs, inputCount);
MenuList mainMenu("MAIN MENU", connectionsMenu, connectionsParaChannel, menuSaveSettings, menuResetProfile, sysExRecordPort, sysExReplayPort, menuPanic);
MenuProfileSelector profileSelector("DOWN FOR MENU", mainMenu);
Menu menu(profileSelector, PB12, PB13, PB14, PB15);

template<typename T>
void saveSettings(T *file) {
  bool hasOutput = false;

  file->println("VERSION:" MIDIBOX_VERSION);
  file->println("");
  for(int f = 0; f < inputCount; ++f) {
    if(hasOutput) {
      file->println("");
      file->println("");
    }
    hasOutput = false;
    for(int t = 0; t < inputs[f]->inRouteCount; ++t) {
      MidiRoute &m = inputs[f]->getRoute(t);
      if(!hasOutput)
      {
        hasOutput = true;
        file->print("INPUT:");
        file->println(inputs[f]->name);
      }
      file->println("");
      file->print("  OUTPUT:");
      file->println(m.out->name);
      file->print("    FILTER:");
      for(int bit = 0; bit < 32; ++bit) {
        file->print(m.getFilter() & (1 << bit) ? '1':'0');
      }
      file->println("");
      file->print("    DIVIDE:");
      file->println(m.getSyncDivider());
      for(int c = 1; c <= 16; ++c) {
         if(!m.processingEnabled(c))
           continue;
         file->print("    CHANNEL:");
         file->println(c);
         file->print("      MAP:");
         file->println(m.getChannelMapping(c));
         file->print("      TRANSPOSE:");
         file->println(m.getTransposition(c));
         file->print("      VELOC_SCALE:");
         file->println(m.getVelocityScale(c));
         file->print("      VELOC_OFFSET:");
         file->println(m.getVelocityOffset(c));
      }
    }
  }

  file->println("");
  hasOutput = false;
  for(int c = 1; c <= 16; ++c) {
    if(paraBus.getPolyphony(c) == 16 && paraBus.getNextChannel(c) == c)
      continue;
    hasOutput = true;
    file->println("");
    file->print("PARA_CHANNEL:");
    file->println(c);
    file->print("  POLYPHONY:");
    file->println(paraBus.getPolyphony(c));
    file->print("  NEXT_CHANNEL:");
    file->println(paraBus.getNextChannel(c));
  }
  if(hasOutput)
    file->println("");
}

int getInputStreamIndex(const char *name) {
  for(int i = 0; i < inputCount; ++i)
    if(strcmp(inputs[i]->name, name) == 0)
      return i;
  return -1;
}

MidiOut * getOutputByName(const char *name) {
  for(int i = 0; i < outputCount; ++i)
    if(strcmp(outputs[i]->name, name) == 0)
      return outputs[i];
  return NULL;
}

void resetSettings() {
  for(int i = 0; i < inputCount; ++i)
    inputs[i]->clearRoutes();
  paraBus.init();
}

template<typename T>
void loadSettingsFrom(T *file) {
  String key;
  String value;
  int from = -1;
  int channel = -1;
  int paraChannel = -1;
  MidiOut *to = NULL;
  MidiRoute *mux = NULL;

  resetSettings();

  while(file->available()) {
    while(file->available() && (file->peek() == ' ' || file->peek() == '\r' || file->peek() == '\n' || file->peek() == '\t'))
      file->read(); // Skip leading spaces

    key = file->readStringUntil(':');
    value = file->readStringUntil('\n');

    // Trim \r
    if(value.length() && value[value.length() - 1] == '\r')
      value = value.substring(0, value.length() - 1);

    if(key.length() < 1 || key.length() > 16 || value.length() < 1 || value.length() > 64)
      continue;
    if(key == "INPUT") {
      from = getInputStreamIndex(value.c_str());
      mux = NULL;
    } else if(key == "OUTPUT") {
      to = getOutputByName(value.c_str());
      if(from != -1 && to)
        mux = inputs[from]->createRoute(to);
      else
        mux = NULL;
    } else if(key == "FILTER") {
      int mask = 0;
      if(value.length() == 32) {
        for(int bit = 0; bit < 32; ++bit) {
          if(value[bit] == '1')
            mask |= (1 << bit);
        }
      }
      if(mux)
        mux->setFilter(mask);
    } else if(key == "DIVIDE") {
      if(mux)
        mux->setSyncDivider(value.toInt());
    } else if(key == "CHANNEL") {
      channel = value.toInt();
      if(channel < 1 || channel > 16)
        channel = -1;
    } else if(key == "MAP") {
      if(mux && channel > 0) {
        mux->setChannelMapping(channel, value.toInt());
      }
    } else if(key == "TRANSPOSE") {
      if(mux && channel > 0) {
        mux->transpose(channel, value.toInt());
      }
    } else if(key == "VELOC_SCALE") {
      if(mux && channel > 0) {
        mux->setVelocityScale(channel, value.toInt());
      }
    } else if(key == "VELOC_OFFSET") {
      if(mux && channel > 0) {
        mux->setVelocityOffset(channel, value.toInt());
      }
    } else if(key == "PARA_CHANNEL") {
      paraChannel = value.toInt();
    } else if(key == "POLYPHONY") {
      paraBus.setPolyphony(paraChannel, value.toInt());
    } else if(key == "NEXT_CHANNEL") {
      paraBus.setNextChannel(paraChannel, value.toInt());
    }
  }
}

void loadSettings() {
  resetSettings();

#if MIDIBOX_EXT_COUNT
  if(repeater)
    return;
#endif

  if(SD.begin(SD_CS)) {
    File file = SD.open(getSettingsPath());
    if(file) {
      loadSettingsFrom(&file);
      file.close();
      SD.end();
      return;
    }
    SD.end();
  }

  // Load failed
  for(int i = 0; i < inputCount; ++i) {
    // Default profile: route all inputs to the first output
    // This allows using this as a midi merger by default
    MidiRoute *r = inputs[i]->createRoute(outputs[0]);
    r->setFilter(~0);
  }
}

void routeMidi() {
  if(sysExSource && sysExTarget && sysExSource->available() && sysExTarget->availableForWrite(0, sysExSource)) {
    // Transmitting a sysEx file
    sysExTarget->write(sysExSource->read(), sysExSource);
    if(sysExSource->eof()) {
      // Finished transfer: reset and go back to main menu
      menu.switchToMain();
    }
  }

  for(int i = 0; i < inputCount; ++i)
    inputs[i]->route();

  MidiIn::routeAll();
}

#if MIDIBOX_EXT_COUNT
void routeRepeater() {
  byte b;

  // Handle MUX -> MIDI

#if MIDIBOX_EXT_COUNT >= 1
  if(midiMux1.available()) {
    b = midiMux1.read();
    if(repeater != 1)
      midiMux1.MidiOut::write(b, NULL);
    else
      midi1.MidiOut::write(b, NULL);
  }
  
  if(midiMux2.available()) {
    b = midiMux2.read();
    if(repeater != 1)
      midiMux2.MidiOut::write(b, NULL);
    else {
      midi2.MidiOut::write(b, NULL);
#if MIDIBOX_GATES
      gates.MidiOut::write(b, NULL);
#endif
    }
  }
#endif

#if MIDIBOX_EXT_COUNT >= 2
  if(midiMux3.available()) {
    b = midiMux3.read();
    if(repeater != 2)
      midiMux3.MidiOut::write(b, NULL);
    else
      midi1.MidiOut::write(b, NULL);
  }
    
  if(midiMux4.available()) {
    b = midiMux4.read();
    if(repeater != 2)
      midiMux4.MidiOut::write(b, NULL);
    else {
      midi2.MidiOut::write(b, NULL);
#if MIDIBOX_GATES
      gates.MidiOut::write(b, NULL);
#endif
    }
  }
#endif

#if MIDIBOX_EXT_COUNT >= 3
  if(midiMux5.available()) {
    b = midiMux5.read();
    if(repeater != 3)
      midiMux5.MidiOut::write(b, NULL);
    else
      midi1.MidiOut::write(b, NULL);
  }
    
    if(midiMux6.available()) {
    b = midiMux6.read();
    if(repeater != 3)
      midiMux6.MidiOut::write(b, NULL);
    else {
      midi2.MidiOut::write(b, NULL);
#if MIDIBOX_GATES
      gates.MidiOut::write(b, NULL);
#endif
    }
  }
#endif

#if MIDIBOX_EXT_COUNT >= 4
  if(midiMux7.available()) {
    b = midiMux7.read();
    if(repeater != 4)
      midiMux7.MidiOut::write(b, NULL);
    else
      midi1.MidiOut::write(b, NULL);
  }
    
  if(midiMux8.available()) {
    b = midiMux8.read();
    if(repeater != 4)
      midiMux8.MidiOut::write(b, NULL);
    else {
      midi2.MidiOut::write(b, NULL);
#if MIDIBOX_GATES
      gates.MidiOut::write(b, NULL);
#endif
    }
  }
#endif

  // Handle MIDI -> MUX
  if(midi1.available()) {
    b = midi1.read();
    switch(repeater) {
#if MIDIBOX_EXT_COUNT >= 1
      case 1:
        midiMux1.MidiOut::write(b, NULL);
        break;
#endif
#if MIDIBOX_EXT_COUNT >= 2
      case 2:
        midiMux3.MidiOut::write(b, NULL);
        break;
#endif
#if MIDIBOX_EXT_COUNT >= 3
      case 3:
        midiMux5.MidiOut::write(b, NULL);
        break;
#endif
#if MIDIBOX_EXT_COUNT >= 4
      case 4:
        midiMux7.MidiOut::write(b, NULL);
        break;
#endif
    }
  }

  if(midi2.available()) {
    b = midi2.read();
    switch(repeater) {
#if MIDIBOX_EXT_COUNT >= 1
      case 1:
        midiMux2.MidiOut::write(b, NULL);
        break;
#endif
#if MIDIBOX_EXT_COUNT >= 2
      case 2:
        midiMux4.MidiOut::write(b, NULL);
        break;
#endif
#if MIDIBOX_EXT_COUNT >= 3
      case 3:
        midiMux6.MidiOut::write(b, NULL);
        break;
#endif
#if MIDIBOX_EXT_COUNT >= 4
      case 4:
        midiMux8.MidiOut::write(b, NULL);
        break;
#endif
    }
  }
}
#endif

// Multiplexers
void setup() {
#if MIDIBOX_EXT_COUNT
  // Detect if repeater mode is enabled or not
  pinMode(PA1, INPUT_PULLUP);
  pinMode(PA0, INPUT_PULLUP);
  pinMode(PB14, INPUT_PULLUP);
  digitalWrite(PB15, 0);
  pinMode(PB15, OUTPUT);
  delay(10);
  if(!digitalRead(PB14)) {
    digitalWrite(PB14, 0);
    pinMode(PB14, OUTPUT);
    pinMode(PB15, INPUT_PULLUP);
    delay(10);
    if(!digitalRead(PB14)) {
      // Repeater mode is enabled
      // Read repeater address
      pinMode(PB14, INPUT_PULLUP);
      pinMode(PB15, INPUT_PULLUP);
      delay(10);
      if(digitalRead(PB14) && digitalRead(PB15)) {
        repeater = digitalRead(PB12) ? 3 : 1;
        if(digitalRead(PB13))
          ++repeater;
      }
    }
  }

  if(!repeater) {
#endif
#if MIDIBOX_USB
    // Initialize peripherals
    USBComposite.clear();
#endif

    // Initialize all ports
    for(int o = 0; o < outputCount; ++o)
      outputs[o]->init();

#if MIDIBOX_USB_SERIAL
    // Initialize USB serial port
    usbSerial.setTXPacketSize(8);
    usbSerial.registerComponent();
#endif

#if MIDIBOX_USB
    // Start USB engine
    USBComposite.begin();
#endif

    // Init menu system
    menu.init();
    loadSettings();
#if MIDIBOX_EXT_COUNT
  } else {
    for(int o = 0; o < repeaterOutputCount; ++o)
      outputs[o]->init();
    resetSettings();
  }
#endif

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, 0);
  pollMillis = millis();
}

void loop() {
#if MIDIBOX_EXT_COUNT
  if(!repeater) {
#endif
    routeMidi();

    // Poll keyboard and blink LED
    while(millis() - pollMillis >= 50) {
      menu.poll();

      // Blink at 0.5Hz
      digitalWrite(LED_BUILTIN, blinkPhase >= 20);
      if(blinkPhase >= 40)
        blinkPhase = 0;
      else
        ++blinkPhase;
      pollMillis += 50;

#if MIDIBOX_USB_SERIAL
      // Process USB serial commands
      if(usbSerial.available()) {
        char cmd = usbSerial.peek();
        if(cmd == '?') {
          // Flush buffer to avoid duplicates
          while(usbSerial.available())
            usbSerial.read();
          saveSettings(&usbSerial);
        } else {
          loadSettingsFrom(&usbSerial);
        }
      }
#endif
    }
#if MIDIBOX_EXT_COUNT
  } else {
    routeRepeater();

    // Blink LED with repeater ID
    while(millis() - pollMillis >= 100) {
      if(blinkPhase & 1) {
        digitalWrite(LED_BUILTIN, 1);
      } else {
        digitalWrite(LED_BUILTIN, blinkPhase / 2 >= repeater);
      }
      if(blinkPhase == 10)
        blinkPhase = 0;
      else
        ++blinkPhase;
      pollMillis += 100;
    }
  }
#endif
}
