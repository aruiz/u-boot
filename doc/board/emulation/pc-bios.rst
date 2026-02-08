.. SPDX-License-Identifier: GPL-2.0+

PC (BIOS) target
================

This target builds a U-Boot binary intended to be **loaded from a FAT partition
by an MBR** on a BIOS-based PC. U-Boot then provides an interactive prompt,
reads the boot disk using its own MBR/GPT and FAT32 drivers, and can provide
UEFI so the machine behaves as a UEFI system.

Boot flow
---------

1. **BIOS** loads the MBR from the boot disk (e.g. first sector).
2. **MBR** (or a small stage0) loads the U-Boot binary from a FAT partition
   into RAM at a fixed address and jumps to it.
3. **U-Boot** runs in 32-bit protected mode: shows the prompt, accesses disk
   via IDE/AHCI using U-Boot's partition (MBR/GPT) and FAT drivers.
4. Optionally, **U-Boot** can load and run UEFI applications (e.g. GRUB, or a
   UEFI kernel) so the BIOS PC appears as a UEFI machine.

MBR → U-Boot contract
---------------------

The loader (MBR or equivalent) must follow this contract so U-Boot starts
correctly:

- **Load address**: Load the U-Boot binary (e.g. ``u-boot.bin`` or
  ``u-boot-dtb.bin``) to physical address **0x100000** (1 MiB, CONFIG_TEXT_BASE).
- **Entry point**: Jump to that same address. U-Boot's 32-bit entry
  (_x86boot_start) is at the start of the image.
- **Stack**: The loader must have set up a valid 32-bit stack (BIOS typically
  leaves a usable stack; U-Boot does not use Cache-As-RAM when loaded this way).
- **Boot drive (optional)**: To tell U-Boot which disk it was loaded from, the
  loader can leave the BIOS drive number in **DL** (e.g. 0x80 for first hard
  disk). Passing this into U-Boot for default boot device is board-specific and
  may be added later.

U-Boot will relocate itself later; the load address above is where the image
must be placed by the MBR.

How U-Boot finds the disk
-------------------------

U-Boot does **not** receive the boot disk or partition from the MBR today. It
uses **build-time and environment defaults**:

- **Compiled-in defaults**: ``CONFIG_ENV_FAT_INTERFACE`` (e.g. ``ide`` or
  ``sata``) and ``CONFIG_ENV_FAT_DEVICE_AND_PART`` (e.g. ``0:1``) in the
  defconfig tell U-Boot which block interface and which device/partition to use
  for the FAT env and for commands like ``load``. So you configure the same disk
  (and partition) from which the MBR loads U-Boot—typically first disk, first
  partition.
- **Runtime**: If a valid env has been saved on that FAT partition, the saved
  ``env_fat_interface`` and ``env_fat_device_and_part`` override the compiled-in
  values. The user can also set them at the prompt.

So the MBR and U-Boot agree by **convention**: you put U-Boot's binary on a FAT
partition (e.g. first partition on the first disk), build U-Boot with that same
disk/partition as the default (e.g. ``ide`` / ``0:1`` or ``sata`` / ``0:1``), and
the MBR loads from that partition. No boot-drive information is passed from MBR
to U-Boot in the current design.

To make U-Boot truly "know" the boot disk, the MBR could pass the BIOS drive
number in **DL** (and optionally partition number elsewhere); U-Boot would need
code to read that and set the default interface and device/partition accordingly
(board-specific, not yet implemented).

Build
-----

::

   $ make pc_bios_defconfig
   $ make

This produces ``u-boot.bin`` (or ``u-boot-dtb.bin``). Put this file on a FAT
partition (e.g. first partition on the same disk the MBR boots from). Your MBR
must load this file to 0x100000 (1 MiB) and jump to it (see contract above).

How U-Boot enumerates disks, keyboard, and display
---------------------------------------------------

Once U-Boot is running, it does the following (all implemented for this target):

- **Disks**: Early in the post-relocation init (``board_r.c``), ``pci_init()``
  runs (``CONFIG_PCI_INIT_R``). That scans the PCI bus; the IDE (PIIX) and
  AHCI drivers bind to the disk controllers and register block devices. U-Boot's
  MBR/GPT and FAT code then operate on those block devices. So disk enumeration
  is via normal PCI scan; no extra step is required.

- **Keyboard**: The x86 build implies ``DM_KEYBOARD`` and defaults
  ``I8042_KEYB`` (Intel 8042 PS/2 controller). The device tree includes a
  keyboard node (``intel,i8042-keyboard``), so the i8042 driver probes and
  registers the keyboard for the console. You can type at the prompt.

- **Display**: The config uses ``VIDEO_VESA`` (VESA BIOS Extension) only. U-Boot
  uses the BIOS VBE interface to set a framebuffer mode and then prints to it.
  This works on both real BIOS PCs and in QEMU (with SeaBIOS). Serial console
  is also available.

The console is set up from the device tree (``stdout-path`` and the chosen
serial/video devices) and ``stdio_add_devices()`` / ``console_init_r()``, so
both serial and (when available) video are used for output.

Features used
-------------

- **Partition tables**: U-Boot's MBR and GPT parsers (``part_dos``, ``part_gpt``)
  are used to read the disk layout.
- **FAT**: U-Boot's FAT driver is used for the environment (e.g. ``env.fat``)
  and for loading files (kernels, UEFI apps).
- **UEFI**: With ``CONFIG_EFI_LOADER`` and ``CONFIG_CMD_BOOTEFI``, you can run
  ``bootefi`` to load and execute UEFI applications (e.g. GRUB, or a UEFI
  kernel), making the BIOS PC behave as a UEFI machine from the OS point of view.

Testing
-------

Quick test with QEMU (no disk, serial only)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To quickly check that the binary runs without creating a disk image::

   $ make pc_bios_defconfig
   $ make
   $ qemu-system-i386 -nographic -kernel u-boot -m 128 -serial stdio

The default TEXT_BASE (0x100000 = 1 MiB) matches QEMU's ``-kernel`` load
address, so no config changes are needed. You should see U-Boot start and get
a prompt. VESA and disk will not work in this setup (no BIOS has run); use the
disk-image test for full behavior.

Full test with QEMU (disk + SeaBIOS)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To test the real boot flow (BIOS → MBR → U-Boot from disk):

1. **Create a disk image** with:

   - An MBR that loads the U-Boot binary from the first partition to
     **0x100000** (1 MiB) and jumps there (see MBR → U-Boot contract above).
     The MBR (or a small stage it loads) must switch to protected mode (or
     use unreal mode) before performing the load.
   - A FAT partition containing ``u-boot.bin`` (or ``u-boot-dtb.bin``).

   Tools to build such an image are outside U-Boot; options include a custom
   MBR/stage1, or a bootloader like GRUB that can chainload a file to a given
   address.

2. **Run QEMU** with SeaBIOS and that disk as the boot drive::

   $ qemu-system-i386 -drive if=ide,file=pcbios.img,format=raw -boot c \\
     -m 128 -nographic -serial stdio

   Use ``-serial stdio`` so you see serial output. For VESA you can add
   ``-display gtk`` or leave the default SDL window.

   Ensure the disk image is the first IDE drive so the BIOS boots from it (C:).

Testing on real hardware
~~~~~~~~~~~~~~~~~~~~~~~~

Write the disk image (MBR + FAT partition with ``u-boot.bin``) to a USB stick
or hard disk and set the machine to boot from it. The same MBR contract and
env defaults apply.

Environment
-----------

Typical env for disk and FAT (adjust interface to match your disk)::

   env_fat_interface=ide
   env_fat_device_and_part=0:1

For SATA::

   env_fat_interface=sata
   env_fat_device_and_part=0:1
