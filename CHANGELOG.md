# Changelog

Add a section per release, headed with the tag. The release workflow puts the section
matching the tag in the GitHub release text (`tools/release_notes.sh`), so keep the
heading exactly the tag name.

## v0.1-alpha

First pre-release. Quake II runs on the Adafruit Fruit Jam, but this is an early
version: a lot remains to be fixed, and problems are to be expected.

### Downloads

- `pico-quake2_fruitjam.uf2`: the firmware for the Adafruit Fruit Jam.
- `pico-quake2_demo_sdcard.zip`: the Quake II demo (three levels) for the SD card.
  Unpack it to the root of the card. It includes id Software's licence agreement for
  the demo, which must stay with the data.

Installation is described in the
[README](https://github.com/PicoPlus-devel/pico-quake2#installing).

### What works

- The three demo levels, the attract demo and the menus
- Video over HDMI, and sound over HDMI or the headphone jack
- USB gamepads, tested with a DualShock 4
- Saving, loading and settings on the SD card

### Known limitations

- About 20 to 25 frames per second, with the view at 70% of the screen
- The first load of each level is slow
- Keyboards, mice and gamepads other than the DualShock 4 have not been tested yet
- The full game has not been tested
- No music, no multiplayer, and no pico-bootLoader version yet

See [Status and limitations](https://github.com/PicoPlus-devel/pico-quake2#status-and-limitations)
for details.
