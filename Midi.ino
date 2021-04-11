#include "Midi.h"
#undef true
#undef false
#include <wirish.h>
#include "usb_midi_device.h"
#include <libmaple/usb.h>
#include "usb_generic.h"

int MidiTracker::extraBytes(byte message) {
  switch (message & 0xF0) {
    case MIDI_NOTE_OFF:
    case MIDI_NOTE_ON:
    case MIDI_ATOUCH:
    case MIDI_CTL:
    case MIDI_BEND:
      return 2;
    case MIDI_PGM:
    case MIDI_PRES:
      return 1;
    case 0xF0:
      switch(message) {
        case MIDI_TIME_CODE:
        case MIDI_SONG_SEL:
          return 1;
        case MIDI_SPP:
          return 2;
      }
  }
  if (message == MIDI_SYSEX_START)
    return 0x3F; // No limit
  return 0;
}

int MidiTracker::extraBytes() const {
  return extraBytes(lastMessage);
}

void MidiTracker::track(byte b) {
  if(realtime(b))
    return;

  if(b & 0x80) {
    // Handle command bytes
    lastMessage = b;
    messageRemainingBytes = extraBytes();
    state = COMMAND;
  } else {
    // Handle data bytes
    if(lastMessage == MIDI_SYSEX_START) {
      // SYSEX data stream
      state = DATA;
      return;
    }
    if(messageRemainingBytes == 0) {
      value = b;
      messageRemainingBytes = extraBytes() - 1; // First byte already received
      state = CHAIN;
    } else {
      if(state == COMMAND)
        value = b;
      --messageRemainingBytes;
      state = DATA;
    }
  }
}

void MidiTracker::reset() {
  lastMessage = 0;
  value = 0;
  messageRemainingBytes = 0;
  state = NONE;
}

int MidiOut::availableForWrite(byte b, void *source) const {
  if(currentSource == NULL || currentSource == source || millis() - sourceReserveMillis > sourceTimeout || MidiTracker::realtime(b))
    return availableForWrite();

  return 0;
}

void MidiOut::write(byte b, void *source) {
  // Realtime message shortcut
  if(MidiTracker::realtime(b)) {
    write(b);
    return;
  }
  // Don't repeat useless message headers
  byte lastMessage = tracker.lastMessage;
  tracker.track(b);
  if(!(lastMessage == b && (b & 0xF0) != 0xF0))
    write(b);
  if(tracker.messageComplete()) {
    currentSource = NULL;
  } else {
    currentSource = source;
    sourceReserveMillis = millis();
  }
}

byte MidiOut::lastSentMessage() const {
  return tracker.lastMessage;
}

bool MidiOut::messageComplete() const {
  return tracker.messageComplete();
}

void MidiRoute::reset() {
  out = NULL;
  filter = 0;
  syncDivider = 1;
  syncDividerCounter = 0;
  resetProcessing();
}

void MidiRoute::resetProcessing() {
  processing = 0;
  for(int c = 0; c < 16; ++c)
    channelProcessing[c].reset();
}

int MidiRoute::availableForWrite() const {
  if(!filter)
    return 0x3F; // Disabled: always take packets in

  // Return one byte less than what's available because routing one byte can fill
  // 2 bytes in the buffer because of event byte regeneration
  int avail = buffer.availableForWrite();
  if(avail >= 1)
    return avail - 1;
  return 0;
}

void MidiRoute::route(byte b) {
  if(MidiTracker::realtime(b)) {
    if(filtered(b))
      buffer.writeHead(b);
    return;
  }

  tracker.track(b);

  // Channel mapping
  if(processing && (b & 0x80) && (b & 0xF0) != 0xF0) {
    int channel = (b & 0x0F);
    if(channelProcessing[channel].channelMapping) {
      byte newChannel = (byte)(channelProcessing[channel].channelMapping - 1);
      b = (b & 0xF0) | newChannel;
      tracker.lastMessage = (tracker.lastMessage & 0xF0) | newChannel;
    }
  }

  // Apply route processing
  if(filtered(tracker.lastMessage)) {
    if(b & 0x80) {
      // Midi clock divider
      if(b == MIDI_CLOCK) {
        ++syncDividerCounter;
        if(syncDividerCounter < syncDivider)
          return; // Ignore that synchronization pulse
        else
          syncDividerCounter = 0;
      } else if(b == MIDI_START) {
          // Reset divider on start
          syncDividerCounter = 0;
      }
    } else {
      // Regenerate an event byte if chaining events
      // That helps merging and processing
      if(tracker.chained()) {
        buffer.write(tracker.lastMessage);
      }

      // Apply channel processing
      if(processing && ((tracker.lastMessage & 0xF0) == MIDI_NOTE_ON) || ((tracker.lastMessage & 0xF0) == MIDI_NOTE_OFF)) {
        int channel = (tracker.lastMessage & 0x0F);
        if(tracker.messageRemainingBytes == 1) {
          // Transpose
          if(channelProcessing[channel].transpose) {
            int note = (int)b + (int)channelProcessing[channel].transpose;
            if(note < 0)
              note = 0;
            if(note > 127)
              note = 127;
            b = (byte)note;
            tracker.value = b;
          }
        } else if(tracker.messageRemainingBytes == 0) {
          // Velocity mapping
          int scale = channelProcessing[channel].velocityScale;
          int offset = channelProcessing[channel].velocityOffset;
          if( offset || scale != 10) {
            int velocity = (int)b;
            if(scale != 10) {
              velocity = velocity * scale / 10;
            }
            velocity += offset;
            if(velocity < 0)
              velocity = 0;
            if(velocity > 127)
              velocity = 127;
            b = (byte)velocity;
          }
        }
      }
    }
    buffer.write(b);
  }
}

void MidiRoute::write() {
  // Flush the buffer into the output
  while(buffer.available() && out->availableForWrite(buffer.peek(), this)) {
    out->write(buffer.read(), this);

    // Leave opportunities for other outputs to interleave messages
    if(out->messageComplete())
      break;
  }
}

void MidiRoute::setFilter(int mask) {
  filter = mask;
}

int MidiRoute::getFilter() const {
  return filter;
}

void MidiRoute::setSyncDivider(int newDivider) {
  if(newDivider >= 1 && newDivider <= 64) {
    syncDivider = (byte)newDivider;
    syncDividerCounter = 0;
  }
}

int MidiRoute::getSyncDivider() const {
  return syncDivider;
}

void MidiRoute::setChannelMapping(int fromChannel, int toChannel) {
  if(toChannel < 0)
    toChannel = 0;
  if(toChannel > 16)
    toChannel = 16;

  if(fromChannel == 0) {
    for(int c = 1; c <= 16; ++c) {
      setChannelMapping(c, toChannel);
    }
    return;
  }

  enableProcessing();
  channelProcessing[fromChannel - 1].channelMapping = (char)toChannel;
}

int MidiRoute::getChannelMapping(int channel) const {
  if(channel > 0)
    --channel;
  return channelProcessing[channel].channelMapping;
}

void MidiRoute::transpose(int channel, int value) {
  if(value < -127)
    value = -127;
  if(value > 127)
    value = 127;

  if(channel == 0) {
    for(int c = 1; c <= 16; ++c) {
      transpose(c, value);
    }
    return;
  }

  enableProcessing();
  channelProcessing[channel - 1].transpose = (char)value;
}

int MidiRoute::getTransposition(int channel) const {
  if(channel > 0)
    --channel;
  return channelProcessing[channel].transpose;
}

void MidiRoute::setVelocityScale(int channel, int value) {
  if(value < 0)
    value = 0;
  if(value > 100)
    value = 100;

  if(channel == 0) {
    for(int c = 1; c <= 16; ++c) {
      setVelocityScale(c, value);
    }
    return;
  }

  enableProcessing();
  channelProcessing[channel - 1].velocityScale = (char)value;
}

int MidiRoute::getVelocityScale(int channel) const {
  if(channel > 0)
    --channel;
  return channelProcessing[channel].velocityScale;
}

void MidiRoute::setVelocityOffset(int channel, int value) {
  if(value < -127)
    value = -127;
  if(value > 127)
    value = 127;

  if(channel == 0) {
    for(int c = 1; c <= 16; ++c) {
      setVelocityOffset(c, value);
    }
    return;
  }

  enableProcessing();
  channelProcessing[channel - 1].velocityOffset = (char)value;
}

int MidiRoute::getVelocityOffset(int channel) const {
  if(channel > 0)
    --channel;
  return channelProcessing[channel].velocityOffset;
}

bool MidiRoute::processingEnabled() const {
  return processing;
}

bool MidiRoute::filtered(byte b) {
  if((b & 0xF0) != 0xF0) {
    // Filter by channel
    return filter & (1 << (b & 0x0F));
  } else {
    // Filter by message type
    return filter & (1 << ((b & 0x0F) + 16));
  }
}

void MidiRoute::ChannelProcessing::reset() {
  channelMapping = 0;
  transpose = 0;
  velocityScale = 10;
  velocityOffset = 0;
}

bool MidiRoute::ChannelProcessing::enabled() const {
  return channelMapping != 0 || transpose != 0 || velocityScale != 10 || velocityOffset != 0;
}

void MidiRoute::enableProcessing() {
  if(processing)
    return;

  for(int c = 0; c < 16; ++c)
    channelProcessing[c].reset();

  processing = 1;
}

MidiRoute & MidiIn::getRoute(int r) {
  return routes[inRoutes[r]];
}

MidiRoute * MidiIn::createRoute(MidiOut *out) {
  if(!out)
    return NULL;

  for(int r = 0; r < maxRouteCount; ++r) {
    if(routes[r].out == NULL) {
      // Add a route
      inRoutes[inRouteCount] = r;
      ++inRouteCount;
      routes[r].reset();
      routes[r].out = out;
      return &routes[r];
    }
  }

  return NULL;
}

void MidiIn::clearRoutes() {
  for(int r = inRouteCount - 1; r >= 0; --r)
    deleteRoute(r);
}

void MidiIn::deleteRoute(int r) {
  if(r < 0 || r >= inRouteCount)
    return;

  getRoute(r).out = NULL;
  while(r < inRouteCount - 1) {
    inRoutes[r] = inRoutes[r + 1];
    ++r;
  }
  --inRouteCount;
}

void MidiIn::route() {
  for(int bytes = 0; available() && bytes < 8; ++bytes) {
    // Check that no route is saturated or the byte would be lost
    for(int r = 0; r < inRouteCount; ++r)
      if(!routes[inRoutes[r]].availableForWrite())
        break;

    // Read the byte
    byte b = read();

    // Route the byte
    for(int r = 0; r < inRouteCount; ++r)
      routes[inRoutes[r]].route(b);
  } 
}

void MidiIn::routeAll() {
  // Send processed bytes to the output
  for(int r = 0; r < maxRouteCount; ++r)
    if(routes[r].active())
      routes[r].write();
}

int MidiIn::countRoutes() {
  int count = 0;
  for(int r = 0; r < sizeof(routes)/sizeof(routes[0]); ++r)
    if(routes[r].active())
      ++count;
  return count;
}

MidiRoute MidiIn::routes[MidiIn::maxRouteCount];

void MidiSerialPort::init() {
  serial.begin(MIDI_BAUD_RATE);
}

int MidiSerialPort::available() const {
  return serial.available();
}

byte MidiSerialPort::read() {
  return serial.read();
}

int MidiSerialPort::availableForWrite() const {
  return serial.availableForWrite();
}

void MidiSerialPort::write(byte b) {
  serial.write(b);
}

int MidiUSBPort::cableFilter = 0;
MIDI_EVENT_PACKET_t MidiUSBPort::inPacket;
int MidiUSBPort::inPos = -1;
int MidiUSBPort::inSize;

void MidiUSBPort::init() {
  if(cableFilter == 0) {
    // First cable initialization.
    // Initialize the USB device.
    registerComponent();
  }
  cableFilter |= 1 << cableId;
}

int MidiUSBPort::available() const {
  while(!inSize && usb_midi_data_available()) {
    usb_midi_rx((uint32*)&inPacket, 1);
    if(cableFilter & (1 << inPacket.cable)) {
      inPos = 0;
      inSize = usbPacketSize();
    }
  }

  if(!inSize || inPacket.cable != cableId)
    return 0; // No data or data for another cable

  return inSize - inPos + 1;
}

byte MidiUSBPort::read() {
  char data = ((char*)&inPacket.midi0)[inPos];
  ++inPos;

  if(inPos == inSize)
    // End of packet
    inSize = 0;

  return data;
}

int MidiUSBPort::availableForWrite() const {
  return usb_midi_is_transmitting() ? 0 : 6;
}

void MidiUSBPort::write(byte b) {
  if(MidiTracker::realtime(b))
  {
    MIDI_EVENT_PACKET_t rtPacket;
    rtPacket.cable = cableId;
    rtPacket.cin = CIN_1BYTE;
    rtPacket.midi0 = b;
    rtPacket.midi1 = 0;
    rtPacket.midi2 = 0;
    usb_midi_tx((uint32*)&rtPacket, 1); // Send packet
    return;
  }
  if(tracker.sysex()) {
    // Processing system exclusive
    outPacket.cin = CIN_SYSEX;

    // Append to the output buffer
    ((char*)&outPacket.midi0)[outPos] = b;
    if(tracker.messageComplete()) {
      // SYSEX last packet
      outPacket.cable = cableId;
      outPacket.cin = CIN_SYSEX + outPos;
      for(int i = outPos; i < 3; ++i)
        // Fill packet with 0
        ((char*)&outPacket.midi0)[i] = 0;
      usb_midi_tx((uint32*)&outPacket, 1); // Send packet
      outPacket.cin = 0;
      outPos = 0;
    } else if(outPos == 3) {
      // SYSEX packet filled
      outPacket.cable = cableId;
      usb_midi_tx((uint32*)&outPacket, 1); // Send packet
      outPos = 0;
    } else {
      // Continue building the packet
      ++outPos;
    }
  } else {
    if(outPacket.cin == CIN_SYSEX) {
      // Ouch, SYSEX_STOP was missing.
      // Send remaining data as-is with the 0xF7 byte missing.
      outPacket.cable = cableId;
      outPacket.cin = CIN_SYSEX + outPos;
      for(int i = outPos; i < 3; ++i)
        // Fill packet with 0
        ((char*)&outPacket.midi0)[i] = 0;
      usb_midi_tx((uint32*)&outPacket, 1); // Send packet
      outPacket.cin = 0;
      outPos = 0;
    }
    // Processing other messages
    if(tracker.messageComplete()) {
      outPacket.cable = cableId;
      // Compute CIN
      if(tracker.lastMessage & 0xF0 == 0xF0) {
        switch(tracker.extraBytes()) {
          default:
          case 0:
            outPacket.cin = CIN_1BYTE;
            outPacket.midi0 = b;
            outPacket.midi1 = 0;
            outPacket.midi2 = 0;
            break;
          case 1:
            outPacket.cin = CIN_2BYTE_SYS_COMMON;
            outPacket.midi0 = tracker.lastMessage;
            outPacket.midi1 = b;
            outPacket.midi2 = 0;
            break;
          case 2:
            outPacket.cin = CIN_3BYTE_SYS_COMMON;
            outPacket.midi0 = tracker.lastMessage;
            outPacket.midi1 = tracker.value;
            outPacket.midi2 = b;
            break;
        }
      } else {
        outPacket.cin = tracker.lastMessage >> 4;
        switch(tracker.extraBytes()) {
          default:
          case 0:
            outPacket.midi0 = b;
            outPacket.midi1 = 0;
            outPacket.midi2 = 0;
            break;
          case 1:
            outPacket.midi0 = tracker.lastMessage;
            outPacket.midi1 = b;
            outPacket.midi2 = 0;
            break;
          case 2:
            outPacket.midi0 = tracker.lastMessage;
            outPacket.midi1 = tracker.value;
            outPacket.midi2 = b;
            break;
         }
      }
      // Send packet
      usb_midi_tx((uint32*)&outPacket, 1);
      outPos = 0;
    }
  }
}

int MidiUSBPort::usbPacketSize() {
  switch(inPacket.cin) {
    default:
    case CIN_MISC_FUNCTION:    // 0x00 /* Reserved for future extension. */
    case CIN_CABLE_EVENT:      // 0x01 /* Reserved for future extension. */
      break;
    case CIN_SYSEX_ENDS_IN_1:  // 0x05 /* 1Bytes */
    case CIN_1BYTE:            // 0x0F /* 1Bytes */
      return 1;
    case CIN_2BYTE_SYS_COMMON: // 0x02 /* 2Bytes -- MTC, SongSelect, etc. */ 
    case CIN_SYSEX_ENDS_IN_2:  // 0x06 /* 2Bytes */
    case CIN_PROGRAM_CHANGE:   // 0x0C /* 2Bytes */
    case CIN_CHANNEL_PRESSURE: // 0x0D /* 2Bytes */
      return 2;
    case CIN_3BYTE_SYS_COMMON: // 0x03 /* 3Bytes -- SPP, etc. */
    case CIN_SYSEX:            // 0x04 /* 3Bytes */
    case CIN_SYSEX_ENDS_IN_3:  // 0x07 /* 3Bytes */
    case CIN_NOTE_OFF:         // 0x08 /* 3Bytes */
    case CIN_NOTE_ON:          // 0x09 /* 3Bytes */
    case CIN_AFTER_TOUCH:      // 0x0A /* 3Bytes */
    case CIN_CONTROL_CHANGE:   // 0x0B /* 3Bytes */
    case CIN_PITCH_WHEEL:      // 0x0E /* 3Bytes */
      return 3;
  }
  return 0;
}

void MidiSerialMux::init() {
  serial.begin(MIDI_BAUD_RATE * 2 * 8); // 2x overhead and 8 channels
}

void MidiSerialMux::dispatchInput() {
  while(serial.available()) {
    byte b = serial.read();
    int portId = ((b >> 4) & 0x07);
    if(!ports[portId])
      continue;
    if(b & 0x80)
      // Store high half of the byte in the byte buffer
      byteBuffer[portId] = b << 4;
    else
      // Store the whole byte to the read buffer
      ports[portId]->inBuf.write((b & 0x0F) | byteBuffer[portId]);
  }
}

int MidiSerialMux::availableForWrite() {
  return serial.availableForWrite();
}

void MidiSerialMux::write(byte b, byte address) {
  serial.write(0x80 | (address << 4) | (b >> 4));
  serial.write((address << 4) | (b & 0x0F));
}

void MidiSerialMux::declare(MidiSerialMuxPort *port, int address) {
  ports[address] = port;
}

void MidiSerialMuxPort::init() {
  if(address == 0)
    mux.init();
  mux.declare(this, address);
  for(int i = 0; i < remoteBuffer; ++i)
    sentMicros[i] = micros();
}

int MidiSerialMuxPort::available() const {
  mux.dispatchInput();
  return inBuf.available();
}

byte MidiSerialMuxPort::read() {
  return inBuf.read();
}

int MidiSerialMuxPort::availableForWrite() const {
  if(mux.availableForWrite() < 2)
    // Hardware port is full, which cannot happen !
    return 0;

  unsigned long m = micros();
  if(m - sentMicros[bufferPos] > MIDI_MICROS_PER_BYTE * remoteBuffer)
    return 1;
  return 0;
}

void MidiSerialMuxPort::write(byte b) {
  mux.write(b, address);
  sentMicros[bufferPos] = micros();
  bufferPos = (bufferPos + 1) % remoteBuffer;
}

void SysExFilePlayer::setFile(File *file_) {
  file = file_;
}

int SysExFilePlayer::available() const {
  return file->available();
}

byte SysExFilePlayer::read() {
  return file->read();
}

void SysExFileRecorder::setFile(File *file_) {
  file = file_;
}

int SysExFileRecorder::availableForWrite() const {
  // More than you can ever think of
  return 65535;
}

void SysExFileRecorder::write(byte b) {
  file->write(b);
}

int MidiLoopback::available() const {
  return buffer.available();
}

byte MidiLoopback::read() {
  return buffer.read();
}

int MidiLoopback::availableForWrite() const {
  return buffer.availableForWrite();
}

void MidiLoopback::write(byte b) {
  // Short circuit realtime messages.
  if(MidiTracker::realtime(b)) {
    buffer.writeHead(b);
    return;
  }

  buffer.write(b);
}

void MidiParaphonyMapper::init() {
  // By default, set polyphony to maximum and disable paraphony chain
  // by looping each channel on itself
  for(int c = 0; c < 16; ++c) {
    polyphony[c] = maxPoly;
    nextChannel[c] = c;
  }
  resetNotes();
}

void MidiParaphonyMapper::setPolyphony(int channel, int newPoly) {
  resetNotes();
  if(channel > 0 && channel <= 16 && newPoly > 0 && newPoly <= maxPoly)
    polyphony[channel - 1] = newPoly;
}

int MidiParaphonyMapper::getPolyphony(int channel) {
  if(channel > 0 && channel <= 16)
    return polyphony[channel - 1];
  return 16;
}

void MidiParaphonyMapper::setNextChannel(int channel, int newNextChannel) {
  if(channel > 0 && channel <= 16 && newNextChannel > 0 && newNextChannel <= 16)
    nextChannel[channel - 1] = newNextChannel - 1;
}

int MidiParaphonyMapper::getNextChannel(int channel) {
  if(channel > 0 && channel <= 16)
    return nextChannel[channel - 1] + 1;
  return channel;
}

void MidiParaphonyMapper::write(byte b) {
  // Short circuit realtime messages.
  if(MidiTracker::realtime(b)) {
    buffer.writeHead(b);
    return;
  }

  byte message = tracker.message();

  if(tracker.allNotesOff()) {
    // Handle "all notes off"
    allNotesOff();
  } else if(message == MIDI_NOTE_ON
  || message == MIDI_NOTE_OFF) {
    // Handle note on and note off events
    if(tracker.messageComplete()) {
      // End of message: process it
      if(message == MIDI_NOTE_OFF || !b)
        noteOff(tracker.lastMessage & 0x0F, tracker.value, b);
      else
        noteOn(tracker.lastMessage & 0x0F, tracker.value, b);
    }
  } else {
    // Other message: pass through
    buffer.write(b);
  }
}

void MidiParaphonyMapper::resetNotes() {
  for(int c = 0; c < 16; ++c)
    for(int p = 0; p < maxPoly; ++p)
      currentNote[c][p] = 255;
  tracker.reset();
}

int MidiParaphonyMapper::findNoteSlot(byte channel, byte note) {
  for(int p = 0; p < polyphony[channel]; ++p) {
    if(currentNote[channel][p] == note)
      return p;
  }
  return -1;
}

void MidiParaphonyMapper::noteOn(byte channel, byte note, byte velocity) {
  int processedChannelMask = 0;

  while(!(processedChannelMask & (1 << channel))) {
    processedChannelMask |= (1 << channel);
    int slot = findNoteSlot(channel, 255);
    if(slot != -1) {
      // Slot found: declare the note as playing
      currentNote[channel][slot] = note;

      // Send note on to the channel
      buffer.write(MIDI_NOTE_ON | channel);
      buffer.write(note);
      buffer.write(velocity);
      return;
    }
    channel = nextChannel[channel];
  }

  // Note is lost, don't send it anywhere
}

void MidiParaphonyMapper::noteOff(byte channel, byte note, byte velocity) {
  int processedChannelMask = 0;

  while(!(processedChannelMask & (1 << channel))) {
    processedChannelMask |= (1 << channel);
    int slot = findNoteSlot(channel, note);
    if(slot != -1) {
      // Slot found: cancel the currently playing note
      currentNote[channel][slot] = 255;

      // Send note off to the channel
      buffer.write(MIDI_NOTE_OFF | channel);
      buffer.write(note);
      buffer.write(velocity);
      return;
    }
    channel = nextChannel[channel];
  }

  // Note was lost, don't do anything
}

void MidiParaphonyMapper::allNotesOff() {
  for(byte c = 0; c < 16; ++c) {
    for(int p = 0; p < polyphony[c]; ++p) {
      if(currentNote[c][p] != -1) {
        // Reset note state to off
        currentNote[c][p] = 255;

        // Note playing: send note off
        buffer.write(MIDI_NOTE_OFF | c);
        buffer.write(currentNote[c][p]);
        buffer.write(0);
      }
    }
  }
}

int MidiGpioGate::availableForWrite() const {
  // No buffer: always ready
  return 127;
}

void MidiGpioGate::write(byte b) {
  if(tracker.messageComplete()) {
    byte msgType = tracker.lastMessage & 0xF0;
    if(tracker.allNotesOff()) {
      allNotesOff();
    } else if(msgType == MIDI_NOTE_ON || msgType == MIDI_NOTE_OFF) {
      for(int i = 0; i < maxNotes; ++i) {
        auto & map = noteMapping[i];
        if(tracker.lastMessage == map.noteOn) {
          digitalWrite(map.pin, b > 0); // velocity == 0 means note off
        } else if(tracker.lastMessage == map.noteOff) {
          digitalWrite(map.pin, 0);
        }
      }
    }
  }
}

void MidiGpioGate::allNotesOff() {
  for(int i = 0; i < maxNotes; ++i) {
    auto & map = noteMapping[i];
    digitalWrite(map.pin, 0);
  }
}
