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

#include "Midi.h"
#include "Menu.h"

// Settings and globals

static const int SD_CS = PA4;
static const char *sysExPath = "/JMC_MIDI/SYSEX/";
static const char *settingsPath = "/JMC_MIDI/SETTINGS/";

HardwareSerial serial2(USART2); // TX:PA2, RX:PA3
HardwareSerial serial3(USART3); // TX:PB10, RX:PB11

typedef MidiLoopback<16> MidiBus;
MidiSerialPort midi1("MIDI PORT 1", serial2);
MidiSerialPort midi2("MIDI PORT 2", serial3);
MidiSerialMux midiMux1("MIDI EXT1 PORT 1", Serial1, 0);
MidiSerialMux midiMux2("MIDI EXT1 PORT 2", Serial1, 1);
MidiSerialMux midiMux3("MIDI EXT2 PORT 1", Serial1, 2);
MidiSerialMux midiMux4("MIDI EXT2 PORT 2", Serial1, 3);
MidiSerialMux midiMux5("MIDI EXT3 PORT 1", Serial1, 4);
MidiSerialMux midiMux6("MIDI EXT3 PORT 2", Serial1, 5);
MidiSerialMux midiMux7("MIDI EXT4 PORT 1", Serial1, 6);
MidiSerialMux midiMux8("MIDI EXT4 PORT 2", Serial1, 7);
MidiGpioGate gates("ANALOG GATES", PA8, PB1, PA15, PB3, PB4, PB5, PB8, PB9);
MidiBus bus1("LOOPBACK BUS 1"); // Internal bus
MidiBus bus2("LOOPBACK BUS 2"); // Internal bus
MidiBus bus3("LOOPBACK BUS 3"); // Internal bus
MidiParaphonyMapper paraBus("PARAPHONIC MAP."); // Paraphonic mapper
MidiStream *inputs[] = {
  &midi1,
  &midi2,
  &midiMux1,
  &midiMux2,
  &midiMux3,
  &midiMux4,
  &midiMux5,
  &midiMux6,
  &midiMux7,
  &midiMux8,
  &bus1,
  &bus2,
  &bus3,
  &paraBus
};
const int inputCount = sizeof(inputs)/sizeof(inputs[0]);
MidiStream *outputs[] = {
  &midi1,
  &midi2,
  &midiMux1,
  &midiMux2,
  &midiMux3,
  &midiMux4,
  &midiMux5,
  &midiMux6,
  &midiMux7,
  &midiMux8,
  &gates,
  &bus1,
  &bus2,
  &bus3,
  &paraBus
};
const int outputCount = sizeof(outputs)/sizeof(outputs[0]);
typedef MidiStreamMux<16> Mux;

// Each input can be routed to a fixed number of outputs
Mux routingMatrix[inputCount][3];
const int routeCount = sizeof(routingMatrix[0]) / sizeof(routingMatrix[0][0]);

MidiStream *sysExSource = NULL;
MidiStream *sysExTarget = NULL;

char currentProfile = 1;
char repeater = 0; 

void resetRoute(Mux &route) {
  route.reset();
  route.stream = outputs[0];
}

// Select a MIDI input from the list of inputs
struct MenuStreamSelect: public MenuItem {
  MenuStreamSelect(const char *name_, MenuItem &subMenu_, MidiStream **streams_, int streamCount_):
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
  MidiStream **streams;
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
    if(SD.begin(SD_CS)) {
      File f = SD.open(getSettingsPath());
      if(f)
        loadSettings(&f);
      else
        resetMux();
      f.close();
      SD.end();
    }
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

struct MenuSaveSettings: public MenuItem {
  MenuSaveSettings(const char *name_): MenuItem(name_) {}
  virtual const char * line2() {
    return "DOWN TO SAVE";
  }
  virtual MenuItem * onKeyPressed(int keys) {
    if(keys == Menu::KEY_DOWN) {
      if(SD.begin(SD_CS)) {
        SD.mkdir(settingsPath);
        SD.remove(getSettingsPath());
        File f = SD.open(getSettingsPath(), FILE_WRITE);
        if(f)
          saveSettings(&f);
        f.close();
        SD.end();
      }
      return parent;
    }

    return NULL;
  }
};

struct MenuSyncDivider: public MenuNumberSelect {
  MenuSyncDivider(const char *name_):
    MenuNumberSelect(name_, *this, 1, 64) {}
  virtual void onEnter() {
    MenuNumberSelect *routeMenu = (MenuNumberSelect *)(parent->parent);
    MenuStreamSelect *inputMenu = (MenuStreamSelect *)(routeMenu->parent);
    MidiStream &stream = routingMatrix[inputMenu->stream][routeMenu->number - 1];
    number = stream.syncDivider;
  }
  virtual MenuItem * onKeyPressed(int keys) {
    MenuItem *nextMenu = MenuNumberSelect::onKeyPressed(keys);
    MenuNumberSelect *routeMenu = (MenuNumberSelect *)(parent->parent);
    MenuStreamSelect *inputMenu = (MenuStreamSelect *)(routeMenu->parent);
    MidiStream &stream = routingMatrix[inputMenu->stream][routeMenu->number - 1];
    stream.syncDivider = number;
    return nextMenu;
  }
};

struct MenuChannelMap: public MenuNumberSelect {
  MenuChannelMap(const char *name_):
    MenuNumberSelect(name_, *this, 0, 16) {}
  virtual void onEnter() {
    MenuNumberSelect *channelMenu = (MenuNumberSelect *)(parent->parent);
    MenuNumberSelect *routeMenu = (MenuNumberSelect *)(channelMenu->parent->parent);
    MenuStreamSelect *inputMenu = (MenuStreamSelect *)(routeMenu->parent);
    MidiStream &stream = routingMatrix[inputMenu->stream][routeMenu->number - 1];
    number = stream.getChannelMapping(channelMenu->number);
  }
  virtual MenuItem * onKeyPressed(int keys) {
    MenuItem *nextMenu = MenuNumberSelect::onKeyPressed(keys);
    MenuNumberSelect *channelMenu = (MenuNumberSelect *)(parent->parent);
    MenuNumberSelect *routeMenu = (MenuNumberSelect *)(channelMenu->parent->parent);
    MenuStreamSelect *inputMenu = (MenuStreamSelect *)(routeMenu->parent);
    MidiStream &stream = routingMatrix[inputMenu->stream][routeMenu->number - 1];
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
    MenuStreamSelect *inputMenu = (MenuStreamSelect *)(routeMenu->parent);
    MidiStream &stream = routingMatrix[inputMenu->stream][routeMenu->number - 1];
    number = stream.getTransposition(channelMenu->number);
  }
  virtual MenuItem * onKeyPressed(int keys) {
    MenuItem *nextMenu = MenuNumberSelect::onKeyPressed(keys);
    MenuNumberSelect *channelMenu = (MenuNumberSelect *)(parent->parent);
    MenuNumberSelect *routeMenu = (MenuNumberSelect *)(channelMenu->parent->parent);
    MenuStreamSelect *inputMenu = (MenuStreamSelect *)(routeMenu->parent);
    MidiStream &stream = routingMatrix[inputMenu->stream][routeMenu->number - 1];
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
    MenuStreamSelect *inputMenu = (MenuStreamSelect *)(routeMenu->parent);
    MidiStream &stream = routingMatrix[inputMenu->stream][routeMenu->number - 1];
    number = stream.getVelocityScale(channelMenu->number);
  }
  virtual MenuItem * onKeyPressed(int keys) {
    MenuItem *nextMenu = MenuNumberSelect::onKeyPressed(keys);
    MenuNumberSelect *channelMenu = (MenuNumberSelect *)(parent->parent);
    MenuNumberSelect *routeMenu = (MenuNumberSelect *)(channelMenu->parent->parent);
    MenuStreamSelect *inputMenu = (MenuStreamSelect *)(routeMenu->parent);
    MidiStream &stream = routingMatrix[inputMenu->stream][routeMenu->number - 1];
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
    MenuStreamSelect *inputMenu = (MenuStreamSelect *)(routeMenu->parent);
    MidiStream &stream = routingMatrix[inputMenu->stream][routeMenu->number - 1];
    number = stream.getVelocityOffset(channelMenu->number);
  }
  virtual MenuItem * onKeyPressed(int keys) {
    MenuItem *nextMenu = MenuNumberSelect::onKeyPressed(keys);
    MenuNumberSelect *channelMenu = (MenuNumberSelect *)(parent->parent);
    MenuNumberSelect *routeMenu = (MenuNumberSelect *)(channelMenu->parent->parent);
    MenuStreamSelect *inputMenu = (MenuStreamSelect *)(routeMenu->parent);
    MidiStream &stream = routingMatrix[inputMenu->stream][routeMenu->number - 1];
    stream.setVelocityOffset(channelMenu->number, number);
    return nextMenu;
  }
};

struct MenuChannelFilter: public MenuFilterEdit {
  MenuChannelFilter(const char *name_): MenuFilterEdit(name_) {}
  virtual void onEnter() {
    MenuFilterEdit::onEnter();
    MenuNumberSelect *routeMenu = (MenuNumberSelect *)(parent->parent);
    MenuStreamSelect *inputMenu = (MenuStreamSelect *)(routeMenu->parent);
    MidiStream &stream = routingMatrix[inputMenu->stream][routeMenu->number - 1];
    mask = stream.filter.getMask();
  }
  virtual MenuItem * onKeyPressed(int keys) {
    MenuItem *nextMenu = MenuFilterEdit::onKeyPressed(keys);
    MenuNumberSelect *routeMenu = (MenuNumberSelect *)(parent->parent);
    MenuStreamSelect *inputMenu = (MenuStreamSelect *)(routeMenu->parent);
    MidiStream &stream = routingMatrix[inputMenu->stream][routeMenu->number - 1];
    stream.filter = mask;
    return nextMenu;
  }
};

struct MenuOutputRoute: public MenuStreamSelect {
  MenuOutputRoute(const char *name_, MidiStream **streams_, int streamCount_): MenuStreamSelect(name_, *this, streams_, streamCount_) {}
  virtual void onEnter() {
    MenuNumberSelect *routeMenu = (MenuNumberSelect *)(parent->parent);
    MenuStreamSelect *inputMenu = (MenuStreamSelect *)(routeMenu->parent);
    Mux &mux = routingMatrix[inputMenu->stream][routeMenu->number - 1];
    for(stream = 0; stream < streamCount; ++stream) {
      if(mux.stream == streams[stream])
        return;
    }
    stream = 0;
  }
  virtual MenuItem * onKeyPressed(int keys) {
    MenuItem *nextMenu = MenuStreamSelect::onKeyPressed(keys);
    MenuNumberSelect *routeMenu = (MenuNumberSelect *)(parent->parent);
    MenuStreamSelect *inputMenu = (MenuStreamSelect *)(routeMenu->parent);
    Mux &mux = routingMatrix[inputMenu->stream][routeMenu->number - 1];
    mux.stream = streams[stream];
    return nextMenu;
  }
};

struct MenuSysExRecord: public MenuItem {
  MenuSysExRecord(const char *name_): MenuItem(name_), fileStream("FILE") {}

  virtual void onEnter() {
    MenuStreamSelect *streamSelect = (MenuStreamSelect *)parent;
    MidiStream *port = streamSelect->streams[streamSelect->stream];
    if(!SD.begin(SD_CS)) {
      return;
    }
    SD.mkdir(sysExPath);
    char fileName[128];

    // Find an available file
    int i;
    for(i = 0; i < 100; ++i) {
      snprintf(fileName, sizeof(fileName), "%sRECORD%02d.SYX", sysExPath, i);
      if(!SD.exists(fileName)) {
        // File found
        break;
      }
    }
    if(i >= 100) {
      // Directory full !
      SD.end();
      return;
    }
    file = SD.open(fileName, FILE_WRITE);
    fileStream.file = &file;
    sysExSource = port;
    sysExTarget = &fileStream;
  }

  virtual const char * line2() {
    if(!fileStream.file) {
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
    if(fileStream.file) {
      file.close();
      SD.end();
    }
  }

  File file;
  SysExFileStream<File> fileStream;
};

struct MenuSysExReplay: public MenuItem {
  MenuSysExReplay(const char *name_): MenuItem(name_), fileStream("FILE") {}
  virtual void onEnter() {
    MenuStreamSelect *streamSelect = (MenuStreamSelect *)(parent->parent);
    MidiStream *port = streamSelect->streams[streamSelect->stream];
    fileStream.file = &((MenuFileSelect *)parent)->file;
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

  SysExFileStream<File> fileStream;
};

struct MenuPanic: public MenuItem {
  MenuPanic(const char *name_): MenuItem(name_) {}
  virtual const char * line2() {
    return "DOWN:ALL NOTEOFF";
  }
  virtual MenuItem * onKeyPressed(int keys) {
    if(keys == Menu::KEY_DOWN) {
      for(int i = 0; i < outputCount; ++i) {
        MidiStream *output = outputs[i];
        for(int c = 0; c < 16; ++c) {
          // Wait until the port is ready ...
          // Yes, we do it that way because it's an emergency situation
          // where we don't want anything disturbing the reset process
          while(!output->availableForWrite());
            MidiStream::flushAll();
          output->write(MIDI_CTL | c);
          output->write(123);
          output->write(0);
        }
      }
    }
    return NULL;
  }
};

struct MenuResetChannelProcessing: public MenuItem {
  MenuResetChannelProcessing(const char *name_): MenuItem(name_) {}
  virtual const char * line2() {
    return "DOWN TO RESET";
  }
  virtual MenuItem * onKeyPressed(int keys) {
    if(keys == Menu::KEY_DOWN) {
      MenuNumberSelect *routeMenu = (MenuNumberSelect *)(parent->parent);
      MenuStreamSelect *inputMenu = (MenuStreamSelect *)(routeMenu->parent);
      Mux &stream = routingMatrix[inputMenu->stream][routeMenu->number - 1];
      stream.resetProcessing();
    }
    return NULL;
  }
};

struct MenuResetRoute: public MenuItem {
  MenuResetRoute(const char *name_): MenuItem(name_) {}
  virtual const char * line2() {
    return "DOWN TO RESET";
  }
  virtual MenuItem * onKeyPressed(int keys) {
    if(keys == Menu::KEY_DOWN) {
      MenuNumberSelect *routeMenu = (MenuNumberSelect *)(parent->parent);
      MenuStreamSelect *inputMenu = (MenuStreamSelect *)(routeMenu->parent);
      Mux &stream = routingMatrix[inputMenu->stream][routeMenu->number - 1];
      stream.reset();
      stream.stream = outputs[0];
      if(routeMenu->number > 1)
        stream.filter = 0;
    }
    return NULL;
  }
};

struct MenuResetProfile: public MenuItem {
  MenuResetProfile(const char *name_): MenuItem(name_) {}
  virtual const char * line2() {
    return "DOWN TO RESET";
  }
  virtual MenuItem * onKeyPressed(int keys) {
    if(keys == Menu::KEY_DOWN) {
      resetMux();
    }
    return NULL;
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

MenuParaPolyphony paraPolyphony("CHAN. POLYPHONY");
MenuParaTargetChannel paraTargetChannel("NEXT CHANNEL");
MenuList paraSettings("PARA. SETTINGS", paraPolyphony, paraTargetChannel);
MenuNumberSelect connectionsParaChannel("PARA. CHANNEL", paraSettings, 0, 16);
MenuPanic menuPanic("ALL NOTES OFF");
MenuResetProfile menuResetProfile("RESET PROFILE");
MenuSaveSettings menuSaveSettings("SAVE PROFILE");
MenuSysExRecord sysExRecordStart("RECORDING SYSEX");
MenuSysExReplay sysExReplayStart("REPLAYING SYSEX");
MenuFileSelect sysExReplayFile("SYSEX REPLAY", sysExReplayStart, SD_CS, sysExPath);
MenuStreamSelect sysExRecordPort("RECORD FROM", sysExRecordStart, inputs, inputCount);
MenuStreamSelect sysExReplayPort("REPLAY TO", sysExReplayFile, outputs, outputCount);
MenuResetChannelProcessing resetChannelProcessing("RESET CHAN PROC.");
MenuResetRoute resetRouteMenu("RESET ROUTE");
MenuTranspose transposeMenu("TRANSPOSE SEMI.");
MenuVelocityScale velocityScaleMenu("VELOCITY SCALE");
MenuVelocityOffset velocityOffsetMenu("VELOCITY OFFSET");
MenuChannelMap mapChannelMenu("MAP TO CHANNEL");
MenuList processingMenu("CHANNEL PROCESS.", mapChannelMenu, transposeMenu, velocityScaleMenu, velocityOffsetMenu);
MenuNumberSelect connectionsChannelProcessing("CHANNEL PROCESS.", processingMenu, 0, 16);
MenuSyncDivider connectionsSyncDivider("CLOCK DIVIDER");
MenuChannelFilter connectionsFilterMenu("ROUTE FILTER");
MenuOutputRoute connectionsOutputMenu("OUTPUT", outputs, outputCount);
MenuList connectionsSetupMenu("ROUTE SETUP", connectionsOutputMenu, connectionsFilterMenu, connectionsSyncDivider, connectionsChannelProcessing, resetChannelProcessing, resetRouteMenu);
MenuNumberSelect connectionsRouteMenu("ROUTE", connectionsSetupMenu, 1, routeCount);
MenuStreamSelect connectionsMenu("CONNECTIONS", connectionsRouteMenu, inputs, inputCount);
MenuList mainMenu("MAIN MENU", connectionsMenu, connectionsParaChannel, menuSaveSettings, sysExRecordPort, sysExReplayPort, menuResetProfile, menuPanic);
MenuProfileSelector profileSelector("DOWN FOR MENU", mainMenu);
Menu menu(profileSelector, PB12, PB13, PB14, PB15);

void saveSettings(File *file) {
  for(int f = 0; f < inputCount; ++f) {
    if(f > 0) {
      file->println("");
      file->println("");
    }
    file->print("FROM:");
    file->println(inputs[f]->name);
    for(int t = 0; t < routeCount; ++t) {
      file->println("");
      file->print("  ROUTE:");
      file->println(t + 1);
      Mux &m = routingMatrix[f][t];
      if(m.stream) {
        file->print("  TO:");
        file->println(m.stream->name);
      }
      file->print("  FILTER:");
      for(int bit = 0; bit < 32; ++bit) {
        file->print(m.filter.getMask() & (1 << bit) ? '1':'0');
      }
      file->println("");
      file->print("  DIVIDE:");
      file->println(m.syncDivider);
      if(m.processingEnabled()) {
        for(int c = 1; c < 16; ++c) {
           file->print("  CHANNEL:");
           file->println(c);
           file->print("    MAP:");
           file->println(m.getChannelMapping(c));
           file->print("    TRANSPOSE:");
           file->println(m.getTransposition(c));
           file->print("    VELOC_SCALE:");
           file->println(m.getVelocityScale(c));
           file->print("    VELOC_OFFSET:");
           file->println(m.getVelocityOffset(c));
        }
      }
    }
  }

  file->println("");
  for(int c = 1; c < 16; ++c) {
    file->print("PARA_CHANNEL:");
    file->println(c);
    file->print("  POLYPHONY:");
    file->println(paraBus.getPolyphony(c));
    file->print("  NEXT_CHANNEL:");
    file->println(paraBus.getNextChannel(c));
  }
}

int getInputStreamIndex(const char *name) {
  MidiStream *s = MidiStream::getStreamByName(name);
  if(!s)
    return -1;
  for(int i = 0; i < inputCount; ++i)
    if(inputs[i] == s)
      return i;
  return -1;
}

void resetMux() {
  for(int i = 0; i < inputCount; ++i) {
    for(int r = 0; r < routeCount; ++r) {
      resetRoute(routingMatrix[i][r]);
      if(r > 0)
        // Block routes > 0 by default
        routingMatrix[i][r].filter = 0;
    }
  }
}

void loadSettings(File *file) {
  String key;
  String value;
  int from = -1;
  int route = -1;
  int channel = -1;
  int paraChannel = -1;
  MidiStream *to = NULL;
  Mux *mux = NULL;
  resetMux();
  while(file->available()) {
    while(file->available() && (file->peek() == ' ' || file->peek() == '\r' || file->peek() == '\n'))
      file->read(); // Skip leading spaces
    key = file->readStringUntil(':');
    value = file->readStringUntil('\n');
    if(value.length() && value[value.length() - 1] == '\r')
      value = value.substring(0, value.length() - 1);
    if(key.length() < 1 || key.length() > 16 || value.length() < 1 || value.length() > 64)
      continue;
    if(key == "FROM") {
      from = getInputStreamIndex(value.c_str());
      mux = NULL;
      route = -1;
    } else if(key == "ROUTE") {
      route = value.toInt() - 1;
      if(route < 0 || route >= routeCount)
        route = -1;
    } else if(key == "TO") {
      to = MidiStream::getStreamByName(value.c_str());
      if(from != -1 && route != -1 && to) {
        mux = &routingMatrix[from][route];
        mux->stream = to;
      }
    } else if(key == "FILTER") {
      int mask = 0;
      if(value.length() == 32) {
        for(int bit = 0; bit < 32; ++bit) {
          if(value[bit] == '1')
            mask |= (1 << bit);
        }
      }
      if(mux)
        mux->filter = mask;
    } else if(key == "DIVIDE") {
      if(mux) {
        mux->syncDivider = value.toInt();
        if(mux->syncDivider < 1 || mux->syncDivider > 64)
          mux->syncDivider = 1;
      }
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

void routeMidi() {
  if(sysExSource && sysExTarget) {
    // Transmitting a sysEx file
    int avail = sysExSource->available();
    int availForWrite = sysExTarget->availableForWrite();
    if(avail > availForWrite)
      avail = availForWrite;
    for(int i = 0; i < avail; ++i) {
      sysExTarget->write(sysExSource->read());
    }
    if(sysExSource->eof()) {
      // Finished transfer: reset and go back to main menu
      menu.switchToMain();
    }
    return;
  }

  for(int i = 0; i < inputCount; ++i) {
    int inAvail = inputs[i]->available();
    bool routeFull = false;

    // Check if any target route is full before starting consuming bytes
    for(int r = 0; r < routeCount; ++r) {
      Mux &route = routingMatrix[i][r];
      if(route.stream && !route.availableForWrite())
        routeFull = true;
    }

    // Route all available bytes from the input
    // until either the input or one of the outputs become full
    for(int bc = 0; bc < inAvail && !routeFull; ++bc) {
      byte b = inputs[i]->read();
      for(int r = 0; r < routeCount; ++r) {
        Mux &route = routingMatrix[i][r];
        route.write(b);
        if(!route.availableForWrite()) {
          // If a single route is full, don't consume any more bytes on this input.
          routeFull = true;
        }
      }
    }
  }

  MidiStream::flushAll();
}

void routeRepeater() {
  byte b;

  MidiSerialMux::dispatch();

  // Handle MUX -> MIDI

  if(midiMux1.available()) {
    b = midiMux1.read();
    if(repeater != 1)
      midiMux1.write(b);
    else
      midi1.write(b);
  }
  
  if(midiMux2.available()) {
    b = midiMux2.read();
    if(repeater != 1)
      midiMux2.write(b);
    else {
      midi2.write(b);
      gates.write(b);
    }
  }

  if(midiMux3.available()) {
    b = midiMux3.read();
    if(repeater != 2)
      midiMux3.write(b);
    else
      midi1.write(b);
  }
    
    if(midiMux4.available()) {
    b = midiMux4.read();
    if(repeater != 2)
      midiMux4.write(b);
    else {
      midi2.write(b);
      gates.write(b);
    }
  }

  if(midiMux5.available()) {
    b = midiMux5.read();
    if(repeater != 3)
      midiMux5.write(b);
    else
      midi1.write(b);
  }
    
    if(midiMux6.available()) {
    b = midiMux6.read();
    if(repeater != 3)
      midiMux6.write(b);
    else {
      midi2.write(b);
      gates.write(b);
    }
  }

  if(midiMux7.available()) {
    b = midiMux5.read();
    if(repeater != 4)
      midiMux5.write(b);
    else
      midi1.write(b);
  }
    
    if(midiMux8.available()) {
    b = midiMux6.read();
    if(repeater != 4)
      midiMux6.write(b);
    else {
      midi2.write(b);
      gates.write(b);
    }
  }

  // Handle MIDI -> MUX
  if(midi1.available()) {
    b = midi1.read();
    switch(repeater) {
      case 1:
        midiMux1.write(b);
        break;
      case 2:
        midiMux3.write(b);
        break;
      case 3:
        midiMux5.write(b);
        break;
      case 4:
        midiMux7.write(b);
        break;
    }
  }

  if(midi2.available()) {
    b = midi2.read();
    switch(repeater) {
      case 1:
        midiMux2.write(b);
        break;
      case 2:
        midiMux4.write(b);
        break;
      case 3:
        midiMux6.write(b);
        break;
      case 4:
        midiMux8.write(b);
        break;
    }
  }
}

// Multiplexers
void setup() {
  // Detect if repeater mode is enabled or not
  pinMode(PA1, INPUT_PULLUP);
  pinMode(PA0, INPUT_PULLUP);
  pinMode(PB14, INPUT_PULLUP);
  digitalWrite(PB14, 0);
  digitalWrite(PB15, 0);
  pinMode(PB15, OUTPUT);
  delay(1);
  if(!digitalRead(PB14)) {
    pinMode(PB14, OUTPUT);
    pinMode(PB15, INPUT_PULLUP);
    delay(1);
    if(!digitalRead(PB14)) {
      pinMode(PB14, INPUT_PULLUP);
      pinMode(PB15, INPUT_PULLUP);
      delay(1);
      if(digitalRead(PB14) && digitalRead(PB15)) {
        repeater = digitalRead(PA1) ? 3 : 1;
        if(digitalRead(PA0))
          ++repeater;
      }
    }
  }
  pinMode(PB14, INPUT);
  pinMode(PB15, INPUT);
  
  MidiStream::setupAll();
  if(!repeater) {
    resetMux();
    menu.init();
  }
}

void loop() {
  if(!repeater) {
    menu.poll();
    routeMidi();
  } else {
    routeRepeater();
  }
}
