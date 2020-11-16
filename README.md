Description
===========

A smart MIDI box based on a STM32 "blue pill" microcontroller (STM32F103C8T6).

It provides many features for its size and cost, including:

 * 2 on-board MIDI ports with proper opto-isolators and transistor output.
 * USB-MIDI support on the integrated STM32 USB port.
 * Up to 8 extra MIDI ports by using a round-robin high-speed serial protocol.
   The MIDI extenders can be built from the same hardware for cost saving.
 * 8 analog gates (5V v-trig).
 * Up to 48 internal routes (virtual "cables" between an internal in and out).
 * Many routing options with internal loopback busses.
 * Port splitting/thru/mirroring.
 * Port merging with internal buffering and proper message interleaving.
 * Channel/message type filtering for each route.
 * Channel remapping for each route.
 * Per route and per channel transposition and velocity adjustments.
 * Low latency (approx 150us), operation is not interrupted when browsing menus
   (browsing menus introduces lag though).
 * Easy to use menu using a LCD screen and 4 directional buttons.
 * 9 routing profiles stored on a SD card.
 * Ability to save and replay MIDI system exclusive dumps to/from SD card
   (SYX files).
 * Per-route MIDI clock divider.
   (reduces the tempo for that particular output or bus)
 * Paraphonic mapper: dispatch simultaneous notes coming from a single channel
   on multiple synths (multiple channels) in real time. Allows combining
   multiple similar synths together to increase their polyphony.
 * On-the-fly MIDI optimization: strips header bytes when sending multiple
   events of the same type.
 * SD and LCD are optional. By default, all traffic is routed to a single
   output so it acts as a MIDI merge box with no interface.
 * USB serial port to load/save configuration on the fly.

Help wanted to implement "MID" file playback.

Keyboard split features would also be cool.

Building the code
=================

This code has been tested on the Roger Clark STM32duino core available here:

https://github.com/rogerclarkmelbourne/Arduino_STM32/

It requires a "blue pill" STM32F103C8T6 board (you can buy them cheap online).

You will need the 128k version to enable all features at the same time.

To have more than 2 MIDI ports, you need to build extenders that you can
daisy-chain on a multiplexed serial link.

Compile-time settings
---------------------

The file MidiBox.ino starts with a few defines that you can change to suit
your needs:

 * MIDIBOX_USB_SERIAL: Set to 1 to enable the USB serial port to load/save
   configuration on the fly. Set to 0 to disable.
 * MIDIBOX_USB_MIDI: Set to 1 to enable USB-MIDI support. Set to 0 to
   disable.
 * MIDIBOX_EXT_COUNT: Maximum number of MIDI extenders supported. Each
   extender adds 2 MIDI ports. Set to 0 to disable the feature completely.
   Up to 4 extenders can be added.
 * MIDIBOX_GATES: Set to 1 to enable hardware gate outputs. Pins can be
   configured by the "gates" global variable. Set to 0 to disable.

Hardware
========

The hardware part of the project is available on EasyEDA:

(WARNING: the PCB is wrong because a 3rd party part is wrong)

https://easyeda.com/jmclabexperience/midi-box

How MIDI routing works
======================

This MIDI box has a very powerful router built-in. Each input can be routed to
4 different outputs, each with its own processing parameters. Multiple routes
can point to a single output, in that case messages will be interleaved
properly.

The schematic below gives you an overview of how messages are processed:

                                                             -----------------
                                          ------------      |                 |
                                    ---->| Processing |---->|  MIDI OUTPUT 1  |
                                   |      ------------      |                 |
                                   |                         -----------------
                                   |
                                   |
                                   |
     ----------------    Route 1   |
    |                |-------------                          ----------------- 
    |                |   Route 2          ------------      |                 |
    |   MIDI INPUT   |------------------>| Processing |---->|  MIDI OUTPUT 2  |
    |                |   Route 3          ------------      |                 |
    |                |-------------                          ----------------- 
     ----------------              |
                                   |
                                   |
                                   |
                                   |                         ----------------- 
                                   |      ------------      |                 |
                                    ---->| Processing |---->|  MIDI OUTPUT 3  |
                                          ------------      |                 |
                                                             ----------------- 

The processing engine for each route can do various manipulations. Some of them
can be applied per-channel and some others are global.

By default, all MIDI inputs have one route connected to the MIDI output 1 with
all processing disabled (pass through).

To delete a route, go to ROUTE SETUP, and select DELETE ROUTE.

Detailed schematic of MIDI processing:

                                  Ch 1    ---------------
         --------------          ------->| Chan. process |----
    --->| Route filter |----    |         ---------------     |
         --------------     |   |                             |
                            |   | Ch 2    ---------------     |
      ----------------------    |------->| Chan. process |----|
     |                          |         ---------------     |
     |      ---------------    /                               \      --------
      ---->| Clock divider |---                                 ---->| Output |
            ---------------    \             [...]             /      --------
                                |                             |
                                |                             |
                                | Ch 16   ---------------     |
                                 ------->| Chan. process |----
                                          ---------------

Channel processing occurs independently for each channel. You can apply the
same processing on all channels by selecting channel 0 in the menu.

Detailed schematic of channel processing:

         ----------------       ----------- 
    --->| MAP TO CHANNEL |---->| TRANSPOSE |-------
         ----------------       -----------        |
                                                   |
               ------------------------------------ 
              |
              |      ----------------       -----------------     
               ---->| VELOCITY SCALE |---->| VELOCITY OFFSET |--->
                     ----------------       -----------------     

Note: Enabling channel processing increases CPU usage and may introduce a lag
if used on too many routes at the same time. For best performance, use the
RESET CHAN PROC. entry in the route menu: that will skip all processing
features in a very efficient way.

Loopback busses
===============

This MIDI box provides loopback busses. This can be useful in some cases:

 * Apply a common processing to multiple streams.
 * Route a single input to more outputs.
 * Do advanced manipulations like channel splitting.

All MIDI messages routed to a loopback bus will appear on the matching bus
input and passing through its routes. Loopback busses behave exactly like
hardware inputs and outputs.

Loopback busses have an internal buffer to ease message merging. It can be
useful to use loopbacks to increase buffering when merging a lot of busy
MIDI ports together.

Loopback busses increase latency, but not as much as a hardware port.

Paraphonic mapping loopback bus
===============================

This bus is a special kind of bus. It allows paraphonic operation of multiple
synthesizers by remapping MIDI channels in real time.

Example: you have a monophonic synth on channel 1, and a 6-voice polyphony
synth on channel 2 of the same port. All of them emit a similar sound.

 * Route the keyboard to the channel 1 of the paraphonic mapper.
 * Go to the paraphonic mapping configuration
 * Configure channel 1
 * Set polyphony to 1 (because your first synth is monophonic)
 * Set next channel to 2 (because your second synth is on channel 2)
 * Configure channel 2
 * Set polyphony to 6 (because your second synth is monophonic)
 * Set next channel to 2 (it terminates the chain)

If the channels of your various synths differ, you can use channel processing
to remap channels in the CONNECTIONS menu.

Now, playing a single note will play on the channel 1 and extra polyphony will
be dispatched to channel 2.

Notes:

 * You can chain more than 2 synths by setting "next channel" accordingly.
   The only limit is the number of MIDI channels available (16).
 
 * You can send notes on a channel in the middle of the channel list.

 * If you organize the channel list in a loop, the program will cycle all
   channels until it finds a channel it has already processed. You can abuse
   this behavior to set various priorities and fallbacks for different event
   sources.

 * If you need more than 1 synth ensemble, just use independent mappings for
   different channel spans. Use channel mapping at your advantage.

 * Channel processing options in the paraphonic mapper route are applied after
   paraphonic mapping.

 * Paraphonic mapping bus latency is higher because it needs to wait for the
   whole MIDI "note on" to be received before routing it. This can be a
   problem when playing notes on a paraphonic output and on a normal output at
   the same time: timing won't be as tight.

 * The paraphonic mapping bus responds to MIDI CC 123 "all notes off" messages
   properly.

 * This feature can probably be abused for more creative purposes, especially
   when combined with channel processing. Try things, be creative !

Menu
====

 * PROFILE: Select the current profile with left and right. Press up to reload
   from SD card (unsaved modifications will be lost).
   * MAIN MENU
     * CONNECTIONS <INPUT>: Route data coming from that input.
       * ROUTE <ROUTE>: Each input can be routed to multiple outputs or busses.
         Select the route here. To add a new route, select the last one: that
         will be created when you enter the submenu.
         * OUTPUT <OUTPUT>: Select the output port to which data will be routed.
         * ROUTE FILTER: Allow to filter data per-channel or filter special
          MIDI messages.
           * ENABLE ALL: Allow all types of messages through this route.
           * DISABLE ALL: Block all types of messages through this route.
           * CHANNEL n / MIDI message type: Press down to pass (mark) or block
             (cross) this kind of message through this route.
         * DELETE ROUTE: Delete the route.
         * CLOCK DIVIDER <NUMBER>: Let pass every <NUMBER> MIDI clock messages.
           This allows reducing the tempo of the equipment connected to this
           output.
         * CHANNEL PROCESS. <NUMBER>: Allows processing of notes per-channel.
           Selecting channel 0 will modify setting for all channels.
           * MAP TO CHANNEL <NUMBER>: Remap all MIDI messages passing through
             this channel to another channel. Set to 0 to disable remapping.
           * TRANSPOSE SEMI. <NUMBER>: Transpose notes of <NUMBER> semitones.
             Positive values make the pitch go up, negative values make the
             pitch go down.
           * VELOCITY SCALE <NUMBER>: Scale velocity values. 10 means 100%, 0
             means 0% (effectively muting the channel) and 100 means 1000%.
           * VELOCITY OFFSET <NUMBER>: Adds an offset to velocity values. This
             is applied after velocity scale.
         * RESET CHAN PROC.: Reset all channel processing. Channel processing
           will be bypassed, reducing CPU load.
     * PARA. CHANNEL <CHANNEL>: Configure paraphonic mapping for the given MIDI
       channel of the paraphonic bus.
         * CHAN. POLYPHONY: Set the maximum polyphony for this channel. All
           notes that play beyond that amount of polyphony will be routed to
           "NEXT CHANNEL".
         * NEXT CHANNEL: Notes that exceed the maximum polyphony of the channel
           will be routed to this channel (or to its next channel if it's also
           full, etc...)
     * SAVE PROFILE: Save the current profile to the SD card.
     * RESET PROFILE: Set the current profile to default values (it's not saved
       on the SD card).
     * REPLAY TO <OUTPUT> <FILE>: Send a SYX file to the selected output.
     * RECORD FROM <INPUT>: Create a file named RECORDnn.SYX (nn is an
       incremented number) and record all SysEx messages coming from the
       selected input into it.
     * ALL NOTES OFF: Send the MIDI CC 123 ("all notes off") to all outputs.
       Allows to correct "stuck" notes.
