#ifndef DEADFLASH_WIN32_STUB_WINIOCTL_H
#define DEADFLASH_WIN32_STUB_WINIOCTL_H

#include "windows.h"

typedef struct GET_LENGTH_INFORMATION {
    LARGE_INTEGER Length;
} GET_LENGTH_INFORMATION;

typedef struct STORAGE_HOTPLUG_INFO {
    DWORD Size;
    BOOL MediaRemovable;
    BOOL MediaHotplug;
    BOOL DeviceHotplug;
    BOOL WriteCacheEnableOverride;
} STORAGE_HOTPLUG_INFO;

typedef struct DISK_GEOMETRY {
    LARGE_INTEGER Cylinders;
    int MediaType;
    DWORD TracksPerCylinder;
    DWORD SectorsPerTrack;
    DWORD BytesPerSector;
} DISK_GEOMETRY;

typedef struct DISK_GEOMETRY_EX {
    DISK_GEOMETRY Geometry;
    LARGE_INTEGER DiskSize;
    BYTE Data[1];
} DISK_GEOMETRY_EX;

typedef struct DISK_EXTENT {
    DWORD DiskNumber;
    LARGE_INTEGER StartingOffset;
    LARGE_INTEGER ExtentLength;
} DISK_EXTENT;

typedef struct VOLUME_DISK_EXTENTS {
    DWORD NumberOfDiskExtents;
    DISK_EXTENT Extents[1];
} VOLUME_DISK_EXTENTS;

typedef enum STORAGE_PROPERTY_ID {
    StorageDeviceProperty = 0,
    StorageAccessAlignmentProperty = 6
} STORAGE_PROPERTY_ID;

typedef enum STORAGE_QUERY_TYPE {
    PropertyStandardQuery = 0
} STORAGE_QUERY_TYPE;

typedef struct STORAGE_PROPERTY_QUERY {
    STORAGE_PROPERTY_ID PropertyId;
    STORAGE_QUERY_TYPE QueryType;
    BYTE AdditionalParameters[1];
} STORAGE_PROPERTY_QUERY;

typedef struct STORAGE_DEVICE_DESCRIPTOR {
    DWORD Version;
    DWORD Size;
    BYTE DeviceType;
    BYTE DeviceTypeModifier;
    BOOL RemovableMedia;
    BOOL CommandQueueing;
    DWORD VendorIdOffset;
    DWORD ProductIdOffset;
    DWORD ProductRevisionOffset;
    DWORD SerialNumberOffset;
    int BusType;
    DWORD RawPropertiesLength;
    BYTE RawDeviceProperties[1];
} STORAGE_DEVICE_DESCRIPTOR;

typedef struct STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR {
    DWORD Version;
    DWORD Size;
    DWORD BytesPerCacheLine;
    DWORD BytesOffsetForCacheAlignment;
    DWORD BytesPerLogicalSector;
    DWORD BytesPerPhysicalSector;
    DWORD BytesOffsetForSectorAlignment;
} STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR;

#define IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS 0x00560000u
#define FSCTL_LOCK_VOLUME 0x00090018u
#define FSCTL_UNLOCK_VOLUME 0x0009001cu
#define FSCTL_DISMOUNT_VOLUME 0x00090020u
#define IOCTL_DISK_GET_LENGTH_INFO 0x0007405cu
#define IOCTL_DISK_GET_DRIVE_GEOMETRY_EX 0x000700a0u
#define IOCTL_STORAGE_GET_HOTPLUG_INFO 0x002d0c14u
#define IOCTL_STORAGE_QUERY_PROPERTY 0x002d1400u
#define IOCTL_DISK_IS_WRITABLE 0x00070024u

#endif
