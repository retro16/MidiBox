#ifndef _MIDIBOX_MIDI_H
#define _MIDIBOX_MIDI_H

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
const byte MIDI_SYSEX_STOP = 0xF7;
const byte MIDI_CLOCK = 0xF8;
const byte MIDI_START = 0xFA;
const byte MIDI_CONT = 0xFB;
const byte MIDI_STOP = 0xFC;
const byte MIDI_SENSE = 0xFE;
const byte MIDI_RESET = 0xFF;


struct MidiFilter {
public:
  MidiFilter(): mask(0xFFFFFFFF) {}
  MidiFilter(int mask_): mask(mask_) {}

  MidiFilter& operator=(MidiFilter& other) {
    mask = other.mask;
    return *this;
  }

  MidiFilter& operator=(int newMask) {
    mask = newMask;
    return *this;
  }

  int getMask() const {
    return mask;
  }

  bool operator()(byte message) {
    if(!(message & 0x80))
      return false;
    if((message & 0xF0) != 0xF0)
      return mask & (1 << (message & 0x0F));
    return mask & (1 << (16 + (message & 0x0F)));
  }
private:
  int mask;
};

int midiMessageLength(byte message);

// A simple FIFO of fixed size.
template<int size>
struct MidiBuffer {
  byte bytes[size];
  int writePtr = 0;
  int readPtr = 0;

  int available() const {
    return (writePtr >= readPtr) ? (writePtr - readPtr) : (size + writePtr - readPtr);
  }

  int availableForWrite() const {
    return size - available();
  }

  void write(byte b) {
    bytes[writePtr] = b;
    ++writePtr;
    if(writePtr >= size)
      writePtr = 0;
  }

  byte read() {
    byte b = bytes[readPtr];
    ++readPtr;
    if(readPtr >= size)
      readPtr = 0;
    return b;
  }
};

struct MidiStream {
public:
  static const int maxStreams = 64;
  MidiStream();
  MidiStream(const char *name);

  // Reset all settings to neutral
  void reset();
  virtual byte read();
  virtual void write(byte b);
  virtual int available() = 0;
  virtual int availableForWrite() = 0;
  virtual void init() {}
  virtual bool eof() {
    return false;
  }
  // Returns the last message being read (with channel information)
  byte lastReadMessage() {
     return readMessage;
  }
  bool readingMessage() {
    return (bool)readRemainingBytes;
  }
  int channel() {
    if(readingMessage()) {
      byte message = lastReadMessage();
      if((message & 0xF0) != 0xF0)
        return (message & 0x0F) + 1;
    }
    return 0;
  }
  // Returns the last message being written (with channel information)
  byte lastWrittenMessage() {
    return writtenMessage;
  }
  bool writingMessage() {
    return writeRemainingBytes;
  }

  // Call this in your setup
  static void setupAll();

  // Call this in your main loop to push all buffered data around
  static void flushAll();

  const char *name;
  MidiFilter filter;

  void resetProcessing() {
    processing = 0;
  }
  bool processingEnabled() const {
    return processing;
  }
  void setChannelMapping(int fromChannel, int toChannel);
  int getChannelMapping(int fromChannel) const;
  void transpose(int channel, int semitones);
  int getTransposition(int channel) const;
  void setVelocityScale(int channel, int scale);
  int getVelocityScale(int channel) const;
  void setVelocityOffset(int channel, int offset);
  int getVelocityOffset(int channel) const;

  static MidiStream *getStreamByName(const char *name);
  byte syncDivider = 1;
  byte syncDividerCounter = 0;
protected:
  virtual byte linkRead() = 0;
  virtual void linkWrite(byte b) = 0;
  template<int>
  friend class MidiStreamMux;
  MidiStream *currentWriter = NULL; // Used by MidiStreamMux
  // Try to flush all buffered bytes
  virtual void flush() {}
  byte readMessage;
  byte writtenMessage;
  int readRemainingBytes = 0;
  int writeRemainingBytes = 0;
private:
  struct ChannelProcessing {
    signed char channelMapping; // Remap channel to another
    signed char transpose; // Transpose notes
    signed char velocityScale;
    signed char velocityOffset;
    void reset();
  };
  void enableProcessing();
  char processing; // Set to true if any processing is enabled
  ChannelProcessing channelProcessing[16];
  static MidiStream *streams[maxStreams];
  static int count;
  void registerStream();
};

template<typename F>
struct SysExFileStream: public MidiStream {
  SysExFileStream(const char *name_): MidiStream(name_), file(NULL)
  {}

  virtual int available() {
    return file->available();
  }

  virtual int availableForWrite() {
    return 65535;
  }

  virtual bool eof() {
    return file->available() == 0;
  }

  F *file;

protected:
  virtual byte linkRead() {
    return file->read();
  }

  virtual void linkWrite(byte b) {
    // Only write SysEx events
    byte m = lastWrittenMessage();
    if(m == MIDI_SYSEX_START || m == MIDI_SYSEX_STOP) {
      file->write(b);
    }
  }
};

template<int size>
struct MidiStreamMux: public MidiStream {
  MidiStreamMux() {}
  MidiStreamMux(const char *name): MidiStream(name) {}
  
  virtual byte read() {
    return stream->read();
  }

  virtual int available() {
    return stream ? stream->available() : 0;
  }

  virtual int availableForWrite() {
    return stream ? (buf.availableForWrite() + stream->availableForWrite()) : 0;
  }
  MidiStream *stream = NULL;
protected:
  virtual byte linkRead() { return 0; }
  virtual void linkWrite(byte b) {
    if(!stream)
      return;
    if(stream->currentWriter != this) {
      if(stream->currentWriter) {
        // Somebody is already sending: buffer for later
        buf.write(b);
        return;
      }
      // No writer: take ownership
      // FIXME: Need a timeout for ownership
      stream->currentWriter = this;
    }
    while(buf.available() && stream->availableForWrite())
      stream->write(buf.read());
    if(stream->availableForWrite())
      stream->write(b);
    else
      buf.write(b);
    if(!stream->writingMessage())
      stream->currentWriter = NULL;
  }

  // Call this from time to time to push buffered bytes into the destination stream
  virtual void flush() {
    if(!stream)
      return;
    if(!stream->currentWriter && buf.available() && stream->availableForWrite())
      // FIXME: Need a timeout for ownership
      stream->currentWriter = this;
    while(stream->currentWriter == this && buf.available() && stream->availableForWrite()) {
      stream->write(buf.read());
      if(!stream->writingMessage()) {
        // Finished sending one message.
        // Unlock the target stream to give a chance to other multiplexers.
        // Waiting messages will be flushed on next call (which should happen ASAP).
        stream->currentWriter = NULL;
        break;
      }
    }
  }
private:
  MidiBuffer<size> buf;
};

template<int size>
struct MidiLoopback: public MidiStream {
public:
  MidiLoopback(const char *name_): MidiStream(name_) {}

  int available() {
    return buf.available();
  }

  int availableForWrite() {
    return buf.availableForWrite();
  }

  byte read() {
    return buf.read();
  }

  void write(byte b) {
    buf.write(b);
  }
  
protected:

  byte linkRead() {
    return 0;
  }

  void linkWrite(byte) {
  }

private:
  MidiBuffer<size> buf;
};

struct MidiSerialPort: public MidiStream {
  MidiSerialPort(const char *name_, HardwareSerial &serial_): MidiStream(name_), serial(serial_) {
  }

  void init() {
    serial.begin(MIDI_BAUD_RATE);
  }

  int available() {
    return serial.available();
  }

  int availableForWrite() {
    return serial.availableForWrite();
  }

protected:
  byte linkRead() {
    byte b = serial.read();
    return b;
  }

  void linkWrite(byte b) {
    serial.write(b);
  }

private:
  HardwareSerial &serial;
};

// Midi port multiplexer
//
// Serial port at 16 times the speed of MIDI. Up to 8 ports can be connected to
// this multiplexer.
//
// The connection is at 500000 bauds (32150*16)
//
// Protocol:
//
// Each MIDI byte is sent in 2 halves:
//
//  ---------------------------------------
// | b7 | b6 | b5 | b4 | b3 | b2 | b1 | b0 |
// |----|--------------|-------------------|
// |HALF|   Port ID    | 4 bits of data    |
//  ---------------------------------------
//
// When HALF is set to 1, b3..b0 contain the 4 most significant bits
// When HALF is set to 0, b3..b0 contain the 4 least significant bits
//
// The upper half (HALF=1) is always sent before the lower half.
// It is allowed to interleave bytes from multiple ports in the same stream.
struct MidiSerialMux: public MidiStream {
  static const int BUF_SIZE = 16;
  static const int MAX_PORTS = 8;
  static const int EXT_BAUD_RATE = MIDI_BAUD_RATE * 16; // 8 ports sending double the amount of data

  MidiSerialMux(const char *name_, HardwareSerial &serial_, int id_): MidiStream(name_), id(id_) {
    port.serial = &serial_;
  }

  void init() {
    if(id == 0)
      port.serial->begin(EXT_BAUD_RATE);
  }

  int available() {
    return port.buf[id].available();
  }

  int availableForWrite() {
    // Sending one byte takes 2 bytes
    return port.serial->availableForWrite() / 2;
  }

  void flush() {
    if(id == 0)
      dispatch();
  }

  // Call this in the main loop before reading individual ports
  // Called by MidiStream::flushAll for convenience.
  static void dispatch();

protected:
  byte linkRead() {
    return port.buf[id].read();
  }

  void linkWrite(byte b);

private:
  struct Port {
    HardwareSerial *serial;
    byte byteBuffer[MAX_PORTS];
    MidiBuffer<BUF_SIZE> buf[MAX_PORTS];
  };

  byte id;
  static Port port;
};

struct MidiGpioGate: public MidiStream {
  static const int maxNotes = 8; // Maximum number of assigned pins

  template<typename... pinList>
  MidiGpioGate(const char *name, pinList... pins): MidiStream(name) {
    mapPins(0, pins...);
  }

  int available() {
    return 0; // Never returns anything
  }

  int availableForWrite() {
    return 65535; // Always accepts messages
  }

protected:
  byte linkRead() {
    return 0xFE; // Never returns anything
  }

  void linkWrite(byte b) {
    for(int i = 0; i < maxNotes; ++i) {
      auto & map = noteMapping[i];
      if(writtenMessage == map.noteOn || writtenMessage == map.noteOff) {
        if(writeRemainingBytes == 2) {
          // Process note number
          map.triggered = (b == map.note);
        } else if(writeRemainingBytes == 1 && map.triggered) {
          // Process velocity (note: velocity == 0 means note off)
          digitalWrite(map.pin, writtenMessage == map.noteOn && b > 0);
        }
      }
    }
  }

private:
  void mapPins(int) {}
  
  template<typename... pinList>
  void mapPins(int position, int pin, pinList... pins) {
    noteMapping[position].pin = pin;
    noteMapping[position].triggered = false;
    noteMapping[position].noteOn = MIDI_NOTE_ON;
    noteMapping[position].noteOff = MIDI_NOTE_OFF;
    noteMapping[position].note = 60 + position; // Assign gates to C-4 and above by default
    pinMode(pin, OUTPUT);
    mapPins(position + 1, pins...);
  }

  struct {
    int pin;
    byte triggered;
    byte noteOn;
    byte noteOff;
    byte note;
  } noteMapping[maxNotes];
};

#endif
