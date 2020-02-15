Description
===========

A smart MIDI box based on a STM32 "blue pill" microcontroller (STM32F103C8T6).

It provides many features for its size and cost, including:

 * 2 on-board MIDI ports with proper opto-isolators and transistor output.
 * Up to 8 extra MIDI ports by using a round-robin high-speed serial protocol.
   The MIDI extenders can be built from the same hardware for cost saving.
 * 8 analog gates (5V v-trig)
 * Many routing options with internal loopback busses
 * Port splitting/thru/mirroring
 * Port merging with internal buffering and proper message interleaving
 * Channel/message type filtering for each route
 * Channel remapping for each route
 * Per route and per channel transposition and velocity adjustments
 * Low latency (approx 100us), operation is not interrupted when browsing menus
   (browsing menus introduces lag though)
 * Easy to use menu using a LCD screen and 4 directional buttons
 * 9 routing profiles stored on a SD card
 * Ability to save and replay MIDI system exclusive dumps to/from SD card
   (SYX files)
 * Per-route MIDI clock divider
   (reduces the tempo for that particular output or bus)
 * SD and LCD are optional. By default, all traffic is routed to a single output
   so it acts as a MIDI merge box with no interface.

Help wanted to implement USB-MIDI and "MID" file playback on the STM32 for the
ultimate MIDI box.

Building the code
=================

This code has been tested on the official STM32duino core available here:

https://github.com/stm32duino/Arduino_Core_STM32

It requires a "blue pill" STM32F103C8T6 board (you can buy them cheap online).

To have more than 2 MIDI ports, you need to build extenders that you can
daisy-chain on a multiplexed serial link.

Hardware
========

The hardware part of the project is available on EasyEDA:

https://easyeda.com/jmclabexperience/midi-box

Optional parts
--------------

Some parts on the PCB are optional, depending on what you wish to do with the
board.

### Extender address

J1 and J2 are only useful for port extenders so it's useless to put them on the
main board.

J3 places the firmware in extender mode so it must be omitted on the main board.

### Optional inputs and outputs.

 * To remove MIDI IN 1, omit R1, R2, D1 and U1.
 * To remove MIDI OUT 1, omit R3, R4, R5, R6, Q1 and Q2.
 * To remove MIDI IN 2, omit R7, R8, D2 and U3.
 * To remove MIDI OUT 2, omit R9, R10, R11, R12, Q3 and Q4.
 * To remove analog gates, omit U4 and R13-R20.

### Optional SD, LCD and buttons.

If you remove the SD card, the LCD screen and buttons, all MIDI messages will be
routed to the first MIDI output. It acts like a buffered MIDI merger.

MIDI port extenders can be used in that configuration.

If you omit the SD card, you can omit C2 (decoupling capacitor).

Building a multiplexed extender
-------------------------------

 * Build the same PCB as the main board
 * Omit SD, LCD and buttons as they are unused
 * You can put analog gates on an extender, they will share MIDI OUT 2.
   Analog gates are mapped to C-4 and up on extenders.
 * Place a jumper on J3 (or link LT and RT pins together).
 * Set J1 and J2 to either open or closed to select the 2 MIDI ports it will
   route (use 0 ohm 0805 resistors or just solder a piece of wire between the
   pads). Each extender must have a different J1/J2 combination.
 * Connect "MUX TX" of the main board to "MUX RX" of the extender
 * Connect "MUX TX" of the extender to "MUX RX" of the next extender, and so on
   and so forth (up to 4 extenders, order is not important)
 * Connect "MUX TX" of the last extender to "MUX RX" of the main board

How MIDI routing works
======================

This MIDI box has a very powerful router built-in. Each input can be routed to
3 different outputs, each with its own processing parameters. Multiple routes
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

By default, route 1 of all MIDI inputs are connected to the MIDI output 1 with
all processing disabled (pass through). Other routes are disabled.

To disable a route, go to ROUTE FILTER, and select DISABLE ALL.

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

Channel processing occurs independently for each channel.

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
DISABLE entry in the channel processing menu that will skip all processing
features in a very efficient way.

Loopback busses
===============

This MIDI box provides loopback busses. This can be useful in some cases:

 * Apply a common processing to multiple streams.
 * Route a single input to more than 3 outputs.
 * Do advanced manipulations like channel splitting.

All MIDI messages routed to a loopback bus will appear on the matching bus
input and passing through its routes. Loopback busses behave exactly like
hardware inputs and outputs.

Loopback busses have an internal buffer to ease message merging. It can be
useful to use loopbacks to increase buffering when merging a lot of busy
MIDI ports together.

Loopback busses increase latency, but not as much as a hardware port.

Menu
====

 * PROFILE: Select the current profile with left and right. Press up to reload
   from SD card (unsaved modifications will be lost).
   * MAIN MENU
     * CONNECTIONS <INPUT>: Route data coming from that input.
       * ROUTE <ROUTE>: Each input can be routed to multiple outputs or busses.
         Select the route here.
         * OUTPUT <OUTPUT>: Select the output port to which data will be routed.
         * ROUTE FILTER: Allow to filter data per-channel or filter special MIDI
           messages.
           * ENABLE ALL: Allow all types of messages through this route.
           * DISABLE ALL: Block all types of messages through this route.
           * CHANNEL n / MIDI message type: Press down to pass (mark) or block
             (cross) this kind of message through this route.
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
         * RESET CH.PROCESS: Reset all channel processing. Channel processing
           will be bypassed, reducing CPU load.
         * RESET ROUTE: Reset all settings for this route.
     * REPLAY TO <OUTPUT> <FILE>: Send a SYX file to the selected output.
     * RECORD FROM <INPUT>: Create a file named RECORDnn.SYX (nn is an
       incremented number) and record all SysEx messages coming from the
       selected input into it.
     * SAVE PROFILE: Save the current profile to the SD card.
     * RESET PROFILE: Set the current profile to default values (it's not saved
       on the SD card).
     * ALL NOTES OFF: Send the MIDI CC 123 ("all notes off") to all outputs.
       Allows to correct "stuck" notes.
