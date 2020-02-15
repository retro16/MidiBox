#include "Midi.h"

int midiChannel(byte message) {
  if((message & 0xF0) == 0xF0) {
    return 0;
  }
  return message & 0x0F;
}

int midiMessageLength(byte message) {
  switch (message & 0xF0) {
    case MIDI_NOTE_OFF:
    case MIDI_NOTE_ON:
    case MIDI_ATOUCH:
    case MIDI_CTL:
    case MIDI_BEND:
      return 3;
    case MIDI_PGM:
    case MIDI_PRES:
      return 2;
  }
  if (message == MIDI_SYSEX_START)
    return 0x7FFFFFFF; // No limit
  return 1;
}

byte MidiStream::read() {
  byte b = linkRead();
  if(b & 0x80) {
    readRemainingBytes = midiMessageLength(b);
    readMessage = b;
  } else if(readRemainingBytes > 0) {
    --readRemainingBytes;
  }
  return b;
}

void MidiStream::write(byte b) {
  if(b & 0x80) {
    if(filter(b)) {
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

      // Channel mapping
      if(processing && (b & 0xF0) != 0xF0) {
        int channel = (b & 0x0F);
        if(channelProcessing[channel].channelMapping)
          b = (b & 0xF0) | (byte)(channelProcessing[channel].channelMapping - 1);
      }
        
      writeRemainingBytes = midiMessageLength(b);
      writtenMessage = b;
    } else {
      writeRemainingBytes = 0;
    }
  } else {
    if(processing && ((writtenMessage & 0xF0) == MIDI_NOTE_ON) || ((writtenMessage & 0xF0) == MIDI_NOTE_OFF)) {
      int channel = (writtenMessage & 0x0F);
      if(writeRemainingBytes == 2) {
        // Transpose
        if(channelProcessing[channel].transpose) {
          int note = (int)b + (int)channelProcessing[channel].transpose;
          if(note < 0)
            note = 0;
          if(note > 127)
            note = 127;
          b = (byte)note;
        }
      } else if(writeRemainingBytes == 1) {
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
  if(writeRemainingBytes > 0) {
    linkWrite(b);
    --writeRemainingBytes;
  }
}

void MidiStream::registerStream() {
  streams[count] = this;
  if(count < sizeof(streams)/sizeof(streams[0]) - 1)
    ++count;
}

void MidiStream::reset() {
  filter = ~0;
  for(int c = 0; c < 16; ++c)
    channelProcessing[c].reset();
  processing = 0;
  syncDivider = 1;
}

void MidiStream::ChannelProcessing::reset() {
  channelMapping = 0;
  transpose = 0;
  velocityScale = 10;
  velocityOffset = 0;
}

void MidiStream::enableProcessing() {
  if(processing)
    return;

  for(int c = 0; c < 16; ++c)
    channelProcessing[c].reset();

  processing = 1;
}

void MidiStream::setChannelMapping(int fromChannel, int toChannel) {
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

int MidiStream::getChannelMapping(int channel) const {
  if(channel > 0)
    --channel;
  return channelProcessing[channel].channelMapping;
}

void MidiStream::transpose(int channel, int value) {
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

int MidiStream::getTransposition(int channel) const {
  if(channel > 0)
    --channel;
  return channelProcessing[channel].transpose;
}

void MidiStream::setVelocityScale(int channel, int value) {
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

int MidiStream::getVelocityScale(int channel) const {
  if(channel > 0)
    --channel;
  return channelProcessing[channel].velocityScale;
}

void MidiStream::setVelocityOffset(int channel, int value) {
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

int MidiStream::getVelocityOffset(int channel) const {
  if(channel > 0)
    --channel;
  return channelProcessing[channel].velocityOffset;
}

MidiStream::MidiStream(): name("?") {
  reset();
  registerStream();
}

MidiStream::MidiStream(const char *name_): name(name_) {
  reset();
  registerStream();
}

void MidiStream::setupAll() {
  for(int s = 0; s < count; ++s) {
    streams[s]->init();
  }
}

void MidiStream::flushAll() {
  for(int s = 0; s < count; ++s) {
    streams[s]->flush();
  }
}

MidiStream *MidiStream::getStreamByName(const char *name) {
  for(int s = 0; s < count; ++s)
    if(strcmp(streams[s]->name, name) == 0)
      return streams[s];
  return NULL;
}

MidiStream *MidiStream::streams[MidiStream::maxStreams];
int MidiStream::count = 0;

void MidiSerialMux::dispatch() {
  // Read hardware serial and dispatch to input buffers
  while(port.serial->available()) {
    byte b = port.serial->read();
    int portId = (b >> 4) & 0x07;
    int phase = b >> 7;
    if(phase)
      // Store high half of the byte in the byte buffer
      port.byteBuffer[portId] = b << 4;
    else
      // Store the whole byte to the read buffer
      port.buf[portId].write((b & 0x0F) | port.byteBuffer[portId]);
  }
}

void MidiSerialMux::linkWrite(byte b) {
  // Write high half
  port.serial->write((b >> 4) | (byte)(id << 4) | 0x80);

  // Write low half
  port.serial->write((b & 0x0F) | (byte)(id << 4));
}

MidiSerialMux::Port MidiSerialMux::port;