# Death Track (dt)

**Death Track** is a simple operating system, it is a toy, an experiment that helps me to see how suitable or unsuitable, convenient or inconvenient, the *FORTH* system is for creating a simple but usable general-purpose operating system.

Currently, only the 32 bit x86 (i686) architecture is being targeted, with intentions to also support the ***Zynq*** platform (dual-core ARM Cortex A9 system with programmable logic fabric - FPGA) as well.

Here are notes how to bootstrap the **Death Track** on a ***VirtualBox*** virtual machine. ***MINGW*** and ***Linux*** systems can be used for development:

  - clone the **Death Track** repository:
```sh
$ git clone https://github.com/stoyan-shopov/dt.git
$ cd dt
```
  - initialize and update submodules:
```sh
$ git submodule init
$ git submodule update
```
  - build for the default target system (32 bit x86, i686)
```sh
$ make
```

If everything is fine, you should now have the ***dt.img*** file, which is a bootable floppy image for the **Death Track**.

Create a new virtual machine in ***VirtualBox***, make sure you have assigned at least 8 MB of RAM for the machine, and make the machine boot from the ***dt.img*** floppy image.

That should be enough to get you started!

Happy hacking on the ***Death Track***!

