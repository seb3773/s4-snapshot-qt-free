# Future Enhancements
Some suggestions aim to further improve performance, reduce dependencies, and modernize the snapshot generation workflow :-)

## 1. Eliminating  xorriso binary dependency
--> The application currently relies on the `xorriso` external binary for ISO generation, which introduces a runtime dependency and requires system calls for execution (and I'm not a huge fan of this)

### proposition:
I think it would be highly desirable to integrate `libisoburn` directly into the backend, eliminating the need for external binary invocation.

Two approaches:

#### "Preferred" Approach: Static Linking
--> statically link `libisoburn` (`.a` library) into the final binary, this would offer several advantages:

- near complete autonomy: executable would become fully self-contained with no external binary dependencies
- high portability: distribution across different systems would be simplified
- with minimal size Impact: after symbol stripping, the size increase would be approximately 600 KB to 1 MB I think. (to be verified)

Since the application would only utilize the ISO creation API (xorrisofs/mkisofs emulation mode) and would not require the physical CD/DVD burning layer (libburn), modern compilers (GCC/Clang) should perform effective dead-code elimination, so unused library functions are excluded from the final binary, keeping the size overhead minimal.

#### Alternative: dynamic linking
dynamic linking could be maintained, which would simply introduce a standard shared library dependency (such as libisoburn1) in the distribution's package. This would be more traditional but would retain an external dependency...

---

## 2. OverlayFS-Based snapshot generation

Now, we copy the entire system to a temporary directory before compression, this has several drawbacks:

- disk I/O: lots of read/write operations during the copy phase
- possible ssd wear because of extensive write operations
- storage overhead ! : requires at least double the storage space of the system being imaged (we can do better)
- impact: could be faster due to physical file duplication used now

### Proposition: OverlayFS

OverlayFS combined with an in-memory filesystem (tmpfs) to create a virtual, merged view of the system without physical file duplication.

this could operate as follows:

- step 1: RAM based temp fodler
---> temporary directory would be mounted directly in RAM:
mount -t tmpfs tmpfs /tmp/s4-overlay

- step 2: mount Configuration

OverlayFS mount point would be initialized with three distinct layers:

 **Lowerdir (Immutable Base)**
 The host's actual root filesystem (`/`), mounted strictly in read-only mode for absolute safety and serves as the unchangeable foundation

 **Upperdir (Writable Layer)**
   Empty directory residing inside the tmpfs, capturing all modifications and deletions, entirely in RAM.

 **Merged (Final View)**
   consolidated directory that presents the unified filesystem, "fed" directly to mksquashfs for compression; combines lowerdir and upperdir transparently.

#### virtual modifications

The application would perform all necessary filesystem modifications directly within the virtual merged directory. So:

- account sanitization: when creating a generic deployable distribution image, deleting user accounts, home directories, or system logs would simply generate "whiteout" markers in the tmpfs layer
- really zero host impact : actual underlying host disk would never be touched or altered
- instant operations because configuration modifications or live-script injections would be written instantaneously to RAM
- Total safety: no risk of corrupting or breaking the host system

#### direct compression

Once staging phase is complete, mksquashfs would be executed directly against the merged directory, after compression finishes, the entire structure would be unmounted, leaving the host system completely untouched :-)
This approach would offer fundamental improvements:

--- Simplicity for mksquashfs**
   - the compression tool would receive a single, clean, virtual source directory
   - files would be processed transparently without complex multi-source inclusion syntax
   - cleaner, more maintainable code

--- Total Isolation
   -  0 risks of corrupting the host system during account sanitization or configuration phases
   - all modifications isolated inside the virtual layer
   - complete rollback capability by simply unmounting

--- performance:
   -  no physical disk space overhead: By excluding the destination ISO file itself, the generation process would consume 0 bytes of physical disk space
   - faster: no time wasted on physical file copying

### Note: non-SquashFS ISO components
-  the root filesystem compression should be isolated from the rest of the ISO structure. Final ISO image requires additional boot assets that cannot be virtualized:

 Bootloader (Isolinux/GRUB), linux kernel (`vmlinuz`) and initial ramdisk (`initrd`)

These standalone files typically represent only a few hundred megabytes in total. A traditional, physical copy of these specific files into a minimal temporary workspace (for example /tmp/s4-iso-root/live/) would still be necessary prior to the final ISO generation call of course, but their storage footprint would be negligible compared to the massive space savings achieved by eliminating the root directory duplication.


This approach would represent a significant architectural improvement, offering better performance, enhanced safety, and dramatically reduced resource consumption.
---


Both enhancements are independent and could be implemented separately:

1. OverlayFS-based snapshot generation
   - Immediate, tangible benefits for all users, addresses current performance and resource limitations and modernizes the core workflow

2. libisoburn integration
   - Reduces external dependencies, improves portability, simplifies deployment

---

I think this would significantly improve S4 Snapshot Qt-free's efficiency, safety, and user experience, in particular the OverlayFS-based snapshot generation would represent a major advancement, bringing the application in line with modern system imaging best practices and improving reliability ;-)

** Need your opinion on these two points, if ok, I can do it **
