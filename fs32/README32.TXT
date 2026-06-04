LeonOS FAT32 HDD fixture

The files in this directory are written verbatim into the FAT32 partition
of dist32/leonos32-hdd.img by tools/build32-hdd.ps1.

The 32-bit kernel parses the MBR, finds the FAT32 partition, parses the
BPB/FSInfo, walks the root-directory cluster chain, and follows FAT32
cluster chains to open files. This path is read-only.
