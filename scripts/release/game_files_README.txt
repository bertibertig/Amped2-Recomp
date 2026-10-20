Put the extracted contents of YOUR OWN Amped 2 Xbox disc here.

This release contains no game data. You need a legally obtained copy of
Amped 2 for the original Xbox, PAL/EU version (title ID 4D530041), which is the
build this port was made from. Other versions (NTSC-U, NTSC-J) are not tested
and most likely do not work.

After extracting the disc image this folder must look like this:

  game_files/
    default.xbe
    Audio/
    Levels/
    Movies/
    ...

To extract an .iso image use extract-xiso (https://github.com/XboxDev/extract-xiso)
or xdvdfs (https://github.com/antangelo/xdvdfs):

  extract-xiso -x "Amped 2.iso" -d game_files
  xdvdfs unpack "Amped 2.iso" game_files
