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
3. **U-Boot** runs in 32-bit protected mode: scans all storage controllers
   (IDE, SATA, NVMe, USB) and searches every ESP for an EFI boot image.
4. **U-Boot** loads and runs the first ``/EFI/BOOT/BOOTIA32.EFI`` it finds,
   making the BIOS PC behave as a UEFI machine. If no EFI image is found,
   U-Boot drops to the interactive prompt.

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

How U-Boot boots (generic EFI)
------------------------------

U-Boot uses the **bootstd** framework (``CONFIG_BOOTSTD_FULL``) to automatically
scan all storage controllers and find a bootable EFI image. No disk interface
is hardcoded; the boot flow is fully generic:

1. Early PCI scan discovers all IDE, AHCI/SATA, SCSI, and NVMe controllers.
2. ``arch_early_init_r()`` probes all storage controllers so block devices
   are available immediately.
3. The default ``bootcmd`` (``bootflow scan``) iterates over all discovered
   block devices by priority.
4. For each partition, the **EFI bootmeth** checks for
   ``/EFI/BOOT/BOOTIA32.EFI`` (the standard EFI boot binary for IA-32).
5. The first valid EFI bootflow found is booted.

This means U-Boot will find and boot an EFI application from the first EFI
System Partition (ESP) it encounters, regardless of whether the disk is on
IDE, SATA, NVMe, or USB.

The environment is not stored on disk (``CONFIG_ENV_IS_NOWHERE``); U-Boot
always starts with compiled-in defaults. This avoids any dependency on a
specific disk interface being present.

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

- **Disks**: Early PCI scan (``SYS_EARLY_PCI_INIT``) enumerates the PCI bus
  and binds IDE, AHCI, and NVMe drivers. Then ``arch_early_init_r()`` probes
  all storage controllers so their block devices are available before the
  environment or EFI subsystems need them.

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
- **FAT**: U-Boot's FAT driver is used for loading files (kernels, UEFI apps)
  from any discovered disk.
- **UEFI**: With ``CONFIG_EFI_LOADER`` and ``CONFIG_BOOTSTD_FULL``, the bootflow
  scanner automatically finds ``/EFI/BOOT/BOOTIA32.EFI`` on any ESP across all
  storage controllers (IDE, SATA, NVMe, USB). The BIOS PC behaves as a UEFI
  machine from the OS point of view.

Testing
-------

Quick test with QEMU (no disk, serial only)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To quickly check that the binary runs without creating a disk image::

   $ make pc_bios_defconfig
   $ make
   $ qemu-system-i386 -nographic -kernel u-boot-dtb.bin -m 128

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

The environment is stored in RAM only (``CONFIG_ENV_IS_NOWHERE``). U-Boot
always starts with compiled-in defaults. This keeps the board generic—no
dependency on a specific disk interface being present at boot.

To persistently save environment variables, you can switch to
``CONFIG_ENV_IS_IN_FAT`` and set ``CONFIG_ENV_FAT_INTERFACE`` /
``CONFIG_ENV_FAT_DEVICE_AND_PART`` to match your disk setup.
