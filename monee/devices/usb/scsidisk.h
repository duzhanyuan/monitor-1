#ifndef DEVICES_SCSIDISK_H
#define DEVICES_SCSIDISK_H

#define HD_SCSICMD      28

#define HD_WIDESCSI      8    /* Used as part of unit number when a wide SCSI
                               * address is used */

struct SCSICmd
{
  uint16_t    *scsi_Data;   /* Points to data used in data phase of command */
  uint32_t    scsi_Length;  /* Length of Data */
  uint32_t    scsi_Actual;
  uint8_t     *scsi_Command;/* SCSI command */
  uint16_t    scsi_CmdLength; /* length of SCSI command */
  uint16_t    scsi_CmdActual;
  uint8_t     scsi_Flags;
  uint8_t     scsi_Status;
  uint8_t     *scsi_SenseData;
  uint16_t    scsi_SenseLength;
  uint16_t    scsi_SenseActual;
};

/* SCSI flags */

#define SCSIF_WRITE         0
#define SCSIF_READ          1
#define SCSIB_READ_WRITE    0

#define SCSIF_NOSENSE       0
#define SCSIF_AUTOSENSE     2
#define SCSIB_AUTOSENSE     1

#define SCSIF_OLDAUTOSENSE  6
#define SCSIB_OLDAUTOSENSE  2

/* SCSI io_Error values */

#define HFERR_SelfUnit      40
#define HFERR_DMA           41
#define HFERR_Phase         42
#define HFERR_Parity        43
#define HFERR_SelTimeout    44
#define HFERR_BadStatus     45

/* SCSI OpenDevice() io_Error values */

#define HFERR_NoBoard       50

#endif /* DEVICES_SCSIDISK_H */
