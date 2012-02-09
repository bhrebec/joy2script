Joy2script
==========

What it is
----------
joy2script is a program to translate joystick events into arbitrary commands.

Requirements
------------
- Support for the /dev/input/js interface.
- Linux - if someone wants this for BSD, let me know and I'll fix it

COMPILING & INSTALLATION
------------------------
From the tarball:
    ./configure && make 
As root:
    make install 

From git:
    ./autogen.sh && ./configure && make
As root:
    make install 

See the sample config in joy2scriptrc.example. For details, see the man page.

TODO
----
1. Add a logrithmic mode for axis
2. Fix a possible signal race caused by me not knowing enough about select()
3. Add support for different modes

COPYING, LEGAL STUFF 
--------------------
This software is under the GNU general public license (see COPYING in
this archive.)  This basically means you can do whatever you want with
it, and to it.  

The latest version of joy2script can be found at http://github.com/bhrebec/joy2script

If you wish to contact me directly, I can be found at brianh32@gmail.com

joy2script is based on the excellent joy2key by Peter Amstutz.

--Brian Hrebec
