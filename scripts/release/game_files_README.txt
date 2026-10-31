Put the extracted contents of YOUR OWN Amped 2 Xbox disc here.

This release contains no game data. You need a legally obtained copy of
Amped 2 for the original Xbox (title ID 4D530041), in the version this package
is built for: PAL/Europe or NTSC-U/North America (see the package name and
README.txt, section 1). NTSC-J is not supported.

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
