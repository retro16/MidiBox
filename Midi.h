#ifndef _MIDIBOX_MIDI_H
#define _MIDIBOX_MIDI_H

#include <USBComposite.h>
#include <MidiSpecs.h>
#include <HardwareSerial.h>
#include <SPI.h>
#include <SD.h>

#define MIDI_BAUD_RATE 31250

// MIDI message values
// Message types (lower nibble is the channel)
const byte MIDI_NOTE_OFF = 0x80;
const byte MIDI_NOTE_ON = 0x90;
const byte MIDI_ATOUCH = 0xA0;
const byte MIDI_CTL = 0xB0;
const byte MIDI_PGM = 0xC0;
const byte MIDI_PRES = 0xD0;
const byte MIDI_BEND = 0xE0;
// Extended messages
const byte MIDI_SYSEX_START = 0xF0;
const byte MIDI_TIME_CODE = 0xF1;
const byte MIDI_SPP = 0xF2;
const byte MIDI_SONG_SEL = 0xF3;
const byte MIDI_TUNE_REQ = 0xF6;
const byte MIDI_SYSEX_STOP = 0xF7;
const byte MIDI_CLOCK = 0xF8;
const byte MIDI_START = 0xFA;
const byte MIDI_CONT = 0xFB;
const byte MIDI_STOP = 0xFC;
const byte MIDI_SENSE = 0xFE;
const byte MIDI_RESET = 0xFF;

// A simple FIFO of fixed size.
template<int size>
struct MidiBuffer {
  int available() const {
    return fill;
  }

  int availableForWrite() const {
    return size - fill;
  }

  void write(byte b) {
    bytes[writePtr] = b;
    ++writePtr;
    if(writePtr >= size)
      writePtr = 0;
    ++fill;
  }

  void writeHead(byte b) {
    --readPtr;
    if(readPtr < 0)
      readPtr = size - 1;
    bytes[readPtr] = b;
    ++fill;
  }

  byte read() {
    byte b = bytes[readPtr];
    ++readPtr;
    if(readPtr >= size)
      readPtr = 0;
    --fill;
    return b;
  }

  byte peek() {
    return bytes[readPtr];
  }

private:
  byte bytes[size];
  int writePtr = 0;
  int readPtr = 0;
  int fill = 0;
};

struct MidiTracker {
  static int extraBytes(byte message);
  static bool realtime(byte message) {
      return message >= 0xF8;
  }
  int extraBytes() const;
  // Track last message and remaining bytes
  void track(byte message);
  void reset();
  byte message() const {
    return (lastMessage & 0xF0) == 0xF0 ? lastMessage : (lastMessage & 0xF0);
  }
  byte channel() const {
    return (lastMessage & 0xF0) == 0xF0 ? 0 : (lastMessage & 0x0F) + 1;
  }
  bool allNotesOff() const {
    return messageComplete() && (lastMessage & 0xF0) == MIDI_CTL && value == 123;
  }
  // Return true if the last command byte was omitted
  // (chained command)
  bool chained() const {
    return state == CHAIN;
  }
  // Return true if processing a system exclusive message
  bool sysex() const {
    return lastMessage == MIDI_SYSEX_START || lastMessage == MIDI_SYSEX_STOP;
  }
  bool messageComplete() const {
    return messageRemainingBytes == 0;
  }
  byte lastMessage = 0;
  byte value; // Value of the byte immediately after the message
  byte messageRemainingBytes = 0;
  enum State: byte {
    NONE = 0,
    COMMAND,
    CHAIN,
    DATA
  };
  State state = NONE;
};

struct MidiOut {
  MidiOut(const char *name_): name(name_) {}
  const char *name;
  virtual void init() {}
  int availableForWrite(byte b, void *source) const;
  void write(byte b, void *source);
  byte lastSentMessage() const;

  // Return true if the last message was sent completely
  bool messageComplete() const;

protected:
  static const int sourceTimeout = 400; // Just above active sense threshold
  virtual int availableForWrite() const = 0;
  virtual void write(byte b) = 0;
  MidiTracker tracker;
  void *currentSource = NULL;
  // Used to compute source reservation timeout
  int sourceReserveMillis;
};

struct MidiRoute {
  MidiOut *out = NULL;

  // Reset settings
  void reset();

  // Reset channel processing settings
  void resetProcessing();

  int availableForWrite() const;

  // Route a byte
  void route(byte b);

  // Send processed bytes to the output
  void write();

  // Route settings
  void setFilter(int mask);
  int getFilter() const;
  void setSyncDivider(int newDivider);
  int getSyncDivider() const;
  void setChannelMapping(int fromChannel, int toChannel);
  int getChannelMapping(int fromChannel) const;
  void transpose(int channel, int semitones);
  int getTransposition(int channel) const;
  void setVelocityScale(int channel, int scale);
  int getVelocityScale(int channel) const;
  void setVelocityOffset(int channel, int offset);
  int getVelocityOffset(int channel) const;

  bool processingEnabled() const;
  bool processingEnabled(int channel) const {
    return processingEnabled() && channelProcessing[channel - 1].enabled();
  }
  // Return true if the route is enabled
  bool active() const {
    return out != NULL;
  }

protected:
  static const int bufferSize = 24;
  bool filtered(byte b);
  int filter;
  byte syncDivider;
  byte syncDividerCounter;
  struct ChannelProcessing {
    signed char channelMapping; // Remap channel to another
    signed char transpose; // Transpose notes
    signed char velocityScale;
    signed char velocityOffset;
    void reset();
    bool enabled() const;
  };
  void enableProcessing();
  char processing; // Set to true if any processing is enabled
  ChannelProcessing channelProcessing[16];
  MidiTracker tracker;
  MidiBuffer<bufferSize> buffer;
};

struct MidiIn {
  // Maximum routes per input
  static const int maxRoutes = 8;
  int inRouteCount = 0;
  // Maximum routes overall
  static const int maxRouteCount = 48;

  MidiIn(const char *name_): name(name_) {}
  const char *name;

  virtual byte read() = 0;
  virtual int available() const = 0;
  virtual bool eof() const {
    return false;
  }

  // Return a route
  MidiRoute & getRoute(int r);

  // Clear all routes
  void clearRoutes();

  // Append a route. Returns false if no more route is available.
  MidiRoute * createRoute(MidiOut *out);

  // Remove a route
  void deleteRoute(int r);

  // Consume and route as many bytes as possible
  // Call routeAll once route() has been called for
  // all inputs to route to outputs
  void route();

  // Route bytes accumulated in buffers to outputs
  static void routeAll();

  // Return the number of active routes
  static int countRoutes();

private:
  // Global route table
  static MidiRoute routes[maxRouteCount];

  // Index in the "routes" global table
  char inRoutes[maxRoutes];
};

struct MidiSerialPort: public MidiIn, public MidiOut {
  MidiSerialPort(const char *inName, const char *outName, HardwareSerial &serial_): MidiIn(inName), MidiOut(outName), serial(serial_) {}
  virtual void init();
  virtual int available() const;
  virtual byte read();
protected:
  virtual int availableForWrite() const;
  virtual void write(byte b);
  HardwareSerial &serial;
};

struct MidiUSBPort: public MidiIn, public MidiOut, public USBMIDI {
  MidiUSBPort(const char *inName, const char *outName, const int cableId_): MidiIn(inName), MidiOut(outName), cableId(cableId_) {}

  // MidiOut
  virtual void init();
  // MidiIn
  virtual int available() const;
  virtual byte read();
protected:
  // MidiOut
  virtual int availableForWrite() const;
  virtual void write(byte b);

  static int usbPacketSize();
  static void poll();
  static int cableFilter; // Bitfield
  static MIDI_EVENT_PACKET_t inPacket;
  static int inPos;
  static int inSize;

  MIDI_EVENT_PACKET_t outPacket; // Output packet buffer
  int outPos = 0; // Pointer in outBuf
  int cableId = 0;
};

struct MidiSerialMuxPort;
struct MidiSerialMux {
  static const int maxPorts = 8;
  MidiSerialMux(HardwareSerial &serial_): serial(serial_) {}
  void init();
  
  // Called by MidiSerialMuxPort
  void dispatchInput();

  int availableForWrite();
  void write(byte b, byte address);
  void declare(MidiSerialMuxPort *port, int address);
protected:
  HardwareSerial &serial;
  byte byteBuffer[maxPorts];
  MidiSerialMuxPort *ports[maxPorts];
};

struct MidiSerialMuxPort: public MidiIn, public MidiOut {
  MidiSerialMuxPort(const char *inName, const char *outName, MidiSerialMux &mux_, byte address_): MidiIn(inName), MidiOut(outName), mux(mux_), address(address_) {}
  virtual void init();
  virtual int available() const;
  virtual byte read();
protected:
  virtual int availableForWrite() const;
  virtual void write(byte b);
  MidiSerialMux &mux;
  byte address;
  byte bufferPos; // Pointer in the sentMicros array
  static const int remoteBuffer = 4;
  unsigned long sentMicros[remoteBuffer]; // micros() when the n-th last byte was sent. Circular buffer.
  static const unsigned long MIDI_MICROS_PER_BYTE = 1000000 * 10.55 / MIDI_BAUD_RATE;
  friend class MidiSerialMux;
  MidiBuffer<24> inBuf; // Input buffer
};

struct SysExFilePlayer: public MidiIn {
  SysExFilePlayer(): MidiIn("SYSEX PLAYER") {}
  void setFile(File *file);
  virtual int available() const;
  virtual byte read();
  virtual bool eof() const {
    return !available();
  }
protected:
  File *file;
};

struct SysExFileRecorder: public MidiOut {
  SysExFileRecorder(): MidiOut("SYSEX RECORDER") {}
  void setFile(File *file);
protected:
  virtual int availableForWrite() const;
  virtual void write(byte b);
  File *file;
};

struct MidiLoopback: public MidiIn, public MidiOut {
  MidiLoopback(const char *name): MidiIn(name), MidiOut(name) {}
  virtual int available() const;
  virtual byte read();
protected:
  virtual int availableForWrite() const;
  virtual void write(byte b);
  static const int bufferSize = 96;
  MidiBuffer<bufferSize> buffer;
};

struct MidiParaphonyMapper: public MidiLoopback {
  static const int maxPoly = 16;
  MidiParaphonyMapper(const char *name): MidiLoopback(name) {
    init();
  }

  void init();
  void setPolyphony(int channel, int newPoly);
  int getPolyphony(int channel);
  void setNextChannel(int channel, int newNextChannel);
  int getNextChannel(int channel);

protected:
  virtual void write(byte b);
  void resetNotes();
  int findNoteSlot(byte channel, byte note);
  void noteOn(byte channel, byte note, byte velocity);
  void noteOff(byte channel, byte note, byte velocity);
  void allNotesOff();

  byte polyphony[16]; // Polyphony of each channel
  byte nextChannel[16]; // Next channel that offloads notes for each channel
  byte currentNote[16][maxPoly]; // 255 if not playing
};

struct MidiGpioGate: public MidiOut {
  template<typename... pinList>
  MidiGpioGate(const char *name, pinList... pins): MidiOut(name) {
    mapPins(0, pins...);
  }

protected:
  static const int maxNotes = 16; // Maximum number of assigned pins

  virtual int availableForWrite() const;
  virtual void write(byte b);
  void allNotesOff();

  void mapPins(int) {}
  
  template<typename... pinList>
  void mapPins(int position, int pin, pinList... pins) {
    noteMapping[position].pin = pin;
    noteMapping[position].triggered = false;
    noteMapping[position].noteOn = MIDI_NOTE_ON;
    noteMapping[position].noteOff = MIDI_NOTE_OFF;
    noteMapping[position].note = 60 + position; // Assign gates to C-4 and above by default
    pinMode(pin, OUTPUT);
    digitalWrite(pin, 0);
    mapPins(position + 1, pins...);
  }

  struct {
    int pin;
    byte triggered;
    byte noteOn = 0;
    byte noteOff = 0;
    byte note;
  } noteMapping[maxNotes];
};

#endif
