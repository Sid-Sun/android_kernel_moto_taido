/*
 * Copyright (c) International Business Machines Corp., 2006
 * Copyright (c) Nokia Corporation, 2006, 2007
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
 * the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Author: Artem Bityutskiy (Битюцкий Артём)
 */

#ifndef __UBI_UBI_H__
#define __UBI_UBI_H__

#include <linux/types.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/notifier.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/ubi.h>
#include <asm/pgtable.h>

#include "ubi-media.h"

#ifdef CONFIG_MTK_HIBERNATION
#define IPOH_VOLUME_NANE "ipoh"
#endif
#if 0
#define MTK_TMP_DEBUG_LOG
#endif

/* Maximum number of supported UBI devices */
#define UBI_MAX_DEVICES 32

/* UBI name used for character devices, sysfs, etc */
#define UBI_NAME_STR "ubi"

/* Normal UBI messages */
#define ubi_msg(fmt, ...) pr_notice("UBI: " fmt "\n", ##__VA_ARGS__)
/* UBI warning messages */
#define ubi_warn(fmt, ...) pr_warn("UBI warning: %s: " fmt "\n",  \
				   __func__, ##__VA_ARGS__)
/* UBI error messages */
#define ubi_err(fmt, ...) pr_err("UBI error: %s: " fmt "\n",      \
				 __func__, ##__VA_ARGS__)

/* Background thread name pattern */
#define UBI_BGT_NAME_PATTERN "ubi_bgt%dd"

/*
 * This marker in the EBA table means that the LEB is um-mapped.
 * NOTE! It has to have the same value as %UBI_ALL.
 */
#define UBI_LEB_UNMAPPED -1

/*
 * In case of errors, UBI tries to repeat the operation several times before
 * returning error. The below constant defines how many times UBI re-tries.
 */
#define UBI_IO_RETRIES 3

/*
 * Length of the protection queue. The length is effectively equivalent to the
 * number of (global) erase cycles PEBs are protected from the wear-leveling
 * worker.
 */
#define UBI_PROT_QUEUE_LEN 10

/* The volume ID/LEB number/erase counter is unknown */
#define UBI_UNKNOWN -1

/*
 * The UBI debugfs directory name pattern and maximum name length (3 for "ubi"
 * + 2 for the number plus 1 for the trailing zero byte.
 */
#define UBI_DFS_DIR_NAME "ubi%d"
#define UBI_DFS_DIR_LEN  (3 + 2 + 1)

/*
 * Error codes returned by the I/O sub-system.
 *
 * UBI_IO_FF: the read region of flash contains only 0xFFs
 * UBI_IO_FF_BITFLIPS: the same as %UBI_IO_FF, but also also there was a data
 *                     integrity error reported by the MTD driver
 *                     (uncorrectable ECC error in case of NAND)
 * UBI_IO_BAD_HDR: the EC or VID header is corrupted (bad magic or CRC)
 * UBI_IO_BAD_HDR_EBADMSG: the same as %UBI_IO_BAD_HDR, but also there was a
 *                         data integrity error reported by the MTD driver
 *                         (uncorrectable ECC error in case of NAND)
 * UBI_IO_BITFLIPS: bit-flips were detected and corrected
 *
 * Note, it is probably better to have bit-flip and ebadmsg as flags which can
 * be or'ed with other error code. But this is a big change because there are
 * may callers, so it does not worth the risk of introducing a bug
 */
enum {
	UBI_IO_FF = 1,
	UBI_IO_FF_BITFLIPS,
	UBI_IO_BAD_HDR,
	UBI_IO_BAD_HDR_EBADMSG,
	UBI_IO_BITFLIPS,
};

/*
 * Return codes of the 'ubi_eba_copy_leb()' function.
 *
 * MOVE_CANCEL_RACE: canceled because the volume is being deleted, the source
 *                   PEB was put meanwhile, or there is I/O on the source PEB
 * MOVE_SOURCE_RD_ERR: canceled because there was a read error from the source
 *                     PEB
 * MOVE_TARGET_RD_ERR: canceled because there was a read error from the target
 *                     PEB
 * MOVE_TARGET_WR_ERR: canceled because there was a write error to the target
 *                     PEB
 * MOVE_TARGET_BITFLIPS: canceled because a bit-flip was detected in the
 *                       target PEB
 * MOVE_RETRY: retry scrubbing the PEB
 */
enum {
	MOVE_CANCEL_RACE = 1,
	MOVE_SOURCE_RD_ERR,
	MOVE_TARGET_RD_ERR,
	MOVE_TARGET_WR_ERR,
	MOVE_TARGET_BITFLIPS,
	MOVE_RETRY,
};

/*
 * Return codes of the fastmap sub-system
 *
 * UBI_NO_FASTMAP: No fastmap super block was found
 * UBI_BAD_FASTMAP: A fastmap was found but it's unusable
 */
enum {
	UBI_NO_FASTMAP = 1,
	UBI_BAD_FASTMAP,
};

/**
 * struct ubi_wl_entry - wear-leveling entry.
 * @u.rb: link in the corresponding (free/used) RB-tree
 * @u.list: link in the protection queue
 * @ec: erase counter
 * @pnum: physical eraseblock number
 *
 * This data structure is used in the WL sub-system. Each physical eraseblock
 * has a corresponding &struct wl_entry object which may be kept in different
 * RB-trees. See WL sub-system for details.
 */
struct ubi_wl_entry {
	union {
		struct rb_node rb;
		struct list_head list;
	} u;
	int ec;
	int pnum;
#ifdef CONFIG_MTK_SLC_BUFFER_SUPPORT
	int tlc;
#endif
};

/**
 * struct ubi_ltree_entry - an entry in the lock tree.
 * @rb: links RB-tree nodes
 * @vol_id: volume ID of the locked logical eraseblock
 * @lnum: locked logical eraseblock number
 * @users: how many tasks are using this logical eraseblock or wait for it
 * @mutex: read/write mutex to implement read/write access serialization to
 *         the (@vol_id, @lnum) logical eraseblock
 *
 * This data structure is used in the EBA sub-system to implement per-LEB
 * locking. When a logical eraseblock is being locked - corresponding
 * &struct ubi_ltree_entry object is inserted to the lock tree (@ubi->ltree).
 * See EBA sub-system for details.
 */
struct ubi_ltree_entry {
	struct rb_node rb;
	int vol_id;
	int lnum;
	int users;
	struct rw_semaphore mutex;
};

/**
 * struct ubi_rename_entry - volume re-name description data structure.
 * @new_name_len: new volume name length
 * @new_name: new volume name
 * @remove: if not zero, this volume should be removed, not re-named
 * @desc: descriptor of the volume
 * @list: links re-name entries into a list
 *
 * This data structure is utilized in the multiple volume re-name code. Namely,
 * UBI first creates a list of &struct ubi_rename_entry objects from the
 * &struct ubi_rnvol_req request object, and then utilizes this list to do all
 * the job.
 */
struct ubi_rename_entry {
	int new_name_len;
	char new_name[UBI_VOL_NAME_MAX + 1];
	int remove;
	struct ubi_volume_desc *desc;
	struct list_head list;
};

struct ubi_volume_desc;

/**
 * struct ubi_fastmap_layout - in-memory fastmap data structure.
 * @e: PEBs used by the current fastmap
 * @to_be_tortured: if non-zero tortured this PEB
 * @used_blocks: number of used PEBs
 * @max_pool_size: maximal size of the user pool
 * @max_wl_pool_size: maximal size of the pool used by the WL sub-system
 */
struct ubi_fastmap_layout {
	struct ubi_wl_entry *e[UBI_FM_MAX_BLOCKS];
	int to_be_tortured[UBI_FM_MAX_BLOCKS];
	int used_blocks;
	int max_pool_size;
	int max_wl_pool_size;
};

/**
 * struct ubi_fm_pool - in-memory fastmap pool
 * @pebs: PEBs in this pool
 * @used: number of used PEBs
 * @size: total number of PEBs in this pool
 * @max_size: maximal size of the pool
 *
 * A pool gets filled with up to max_size.
 * If all PEBs within the pool are used a new fastmap will be written
 * to the flash and the pool gets refilled with empty PEBs.
 *
 */
struct ubi_fm_pool {
	int pebs[UBI_FM_MAX_POOL_SIZE];
	int used;
	int size;
	int max_size;
};

/**
 * struct ubi_volume - UBI volume description data structure.
 * @dev: device object to make use of the the Linux device model
 * @cdev: character device object to create character device
 * @ubi: reference to the UBI device description object
 * @vol_id: volume ID
 * @ref_count: volume reference count
 * @readers: number of users holding this volume in read-only mode
 * @writers: number of users holding this volume in read-write mode
 * @exclusive: whether somebody holds this volume in exclusive mode
 *
 * @reserved_pebs: how many physical eraseblocks are reserved for this volume
 * @vol_type: volume type (%UBI_DYNAMIC_VOLUME or %UBI_STATIC_VOLUME)
 * @usable_leb_size: logical eraseblock size without padding
 * @used_ebs: how many logical eraseblocks in this volume contain data
 * @last_eb_bytes: how many bytes are stored in the last logical eraseblock
 * @used_bytes: how many bytes of data this volume contains
 * @alignment: volume alignment
 * @data_pad: how many bytes are not used at the end of physical eraseblocks to
 *            satisfy the requested alignment
 * @name_len: volume name length
 * @name: volume name
 *
 * @upd_ebs: how many eraseblocks are expected to be updated
 * @ch_lnum: LEB number which is being changing by the atomic LEB change
 *           operation
 * @upd_bytes: how many bytes are expected to be received for volume update or
 *             atomic LEB change
 * @upd_received: how many bytes were already received for volume update or
 *                atomic LEB change
 * @upd_buf: update buffer which is used to collect update data or data for
 *           atomic LEB change
 *
 * @eba_tbl: EBA table of this volume (LEB->PEB mapping)
 * @checked: %1 if this static volume was checked
 * @corrupted: %1 if the volume is corrupted (static volumes only)
 * @upd_marker: %1 if the update marker is set for this volume
 * @updating: %1 if the volume is being updated
 * @changing_leb: %1 if the atomic LEB change ioctl command is in progress
 * @direct_writes: %1 if direct writes are enabled for this volume
 *
 * The @corrupted field indicates that the volume's contents is corrupted.
 * Since UBI protects only static volumes, this field is not relevant to
 * dynamic volumes - it is user's responsibility to assure their data
 * integrity.
 *
 * The @upd_marker flag indicates that this volume is either being updated at
 * the moment or is damaged because of an unclean reboot.
 */
struct ubi_volume {
	struct device dev;
	struct cdev cdev;
	struct ubi_device *ubi;
	int vol_id;
	int ref_count;
	int readers;
	int writers;
	int exclusive;

	int reserved_pebs;
	int vol_type;
	int usable_leb_size;
	int used_ebs;
	int last_eb_bytes;
	long long used_bytes;
	int alignment;
	int data_pad;
	int name_len;
	char name[UBI_VOL_NAME_MAX + 1];

	int upd_ebs;
	int ch_lnum;
	long long upd_bytes;
	long long upd_received;
	void *upd_buf;

	int *eba_tbl;
	unsigned int checked:1;
	unsigned int corrupted:1;
	unsigned int upd_marker:1;
	unsigned int updating:1;
	unsigned int changing_leb:1;
	unsigned int direct_writes:1;
};

/**
 * struct ubi_volume_desc - UBI volume descriptor returned when it is opened.
 * @vol: reference to the corresponding volume description object
 * @mode: open mode (%UBI_READONLY, %UBI_READWRITE, or %UBI_EXCLUSIVE)
 */
struct ubi_volume_desc {
	struct ubi_volume *vol;
	int mode;
};

struct ubi_wl_entry;

/**
 * struct ubi_debug_info - debugging information for an UBI device.
 *
 * @chk_gen: if UBI general extra checks are enabled
 * @chk_io: if UBI I/O extra checks are enabled
 * @disable_bgt: disable the background task for testing purposes
 * @emulate_bitflips: emulate bit-flips for testing purposes
 * @emulate_io_failures: emulate write/erase failures for testing purposes
 * @dfs_dir_name: name of debugfs directory containing files of this UBI device
 * @dfs_dir: direntry object of the UBI device debugfs directory
 * @dfs_chk_gen: debugfs knob to enable UBI general extra checks
 * @dfs_chk_io: debugfs knob to enable UBI I/O extra checks
 * @dfs_disable_bgt: debugfs knob to disable the background task
 * @dfs_emulate_bitflips: debugfs knob to emulate bit-flips
 * @dfs_emulate_io_failures: debugfs knob to emulate write/erase failures
 */
struct ubi_debug_info {
	unsigned int chk_gen:1;
	unsigned int chk_io:1;
	unsigned int disable_bgt:1;
	unsigned int emulate_bitflips:1;
	unsigned int emulate_io_failures:1;
	char dfs_dir_name[UBI_DFS_DIR_LEN + 1];
	struct dentry *dfs_dir;
	struct dentry *dfs_chk_gen;
	struct dentry *dfs_chk_io;
	struct dentry *dfs_disable_bgt;
	struct dentry *dfs_emulate_bitflips;
	struct dentry *dfs_emulate_io_failures;
};

/**
 * struct ubi_device - UBI device description structure
 * @dev: UBI device object to use the the Linux device model
 * @cdev: character device object to create character device
 * @ubi_num: UBI device number
 * @ubi_name: UBI device name
 * @vol_count: number of volumes in this UBI device
 * @volumes: volumes of this UBI device
 * @volumes_lock: protects @volumes, @rsvd_pebs, @avail_pebs, beb_rsvd_pebs,
 *                @beb_rsvd_level, @bad_peb_count, @good_peb_count, @vol_count,
 *                @vol->readers, @vol->writers, @vol->exclusive,
 *                @vol->ref_count, @vol->mapping and @vol->eba_tbl.
 * @ref_count: count of references on the UBI device
 * @image_seq: image sequence number recorded on EC headers
 *
 * @rsvd_pebs: count of reserved physical eraseblocks
 * @avail_pebs: count of available physical eraseblocks
 * @beb_rsvd_pebs: how many physical eraseblocks are reserved for bad PEB
 *                 handling
 * @beb_rsvd_level: normal level of PEBs reserved for bad PEB handling
 *
 * @autoresize_vol_id: ID of the volume which has to be auto-resized at the end
 *                     of UBI initialization
 * @vtbl_slots: how many slots are available in the volume table
 * @vtbl_size: size of the volume table in bytes
 * @vtbl: in-RAM volume table copy
 * @device_mutex: protects on-flash volume table and serializes volume
 *                creation, deletion, update, re-size, re-name and set
 *                property
 *
 * @max_ec: current highest erase counter value
 * @mean_ec: current mean erase counter value
 *
 * @global_sqnum: global sequence number
 * @ltree_lock: protects the lock tree and @global_sqnum
 * @ltree: the lock tree
 * @alc_mutex: serializes "atomic LEB change" operations
 *
 * @fm_disabled: non-zero if fastmap is disabled (default)
 * @fm: in-memory data structure of the currently used fastmap
 * @fm_pool: in-memory data structure of the fastmap pool
 * @fm_wl_pool: in-memory data structure of the fastmap pool used by the WL
 *		sub-system
 * @fm_mutex: serializes ubi_update_fastmap() and protects @fm_buf
 * @fm_buf: vmalloc()'d buffer which holds the raw fastmap
 * @fm_size: fastmap size in bytes
 * @fm_sem: allows ubi_update_fastmap() to block EBA table changes
 * @fm_work: fastmap work queue
 *
 * @used: RB-tree of used physical eraseblocks
 * @erroneous: RB-tree of erroneous used physical eraseblocks
 * @free: RB-tree of free physical eraseblocks
 * @free_count: Contains the number of elements in @free
 * @scrub: RB-tree of physical eraseblocks which need scrubbing
 * @pq: protection queue (contain physical eraseblocks which are temporarily
 *      protected from the wear-leveling worker)
 * @pq_head: protection queue head
 * @wl_lock: protects the @used, @free, @pq, @pq_head, @lookuptbl, @move_from,
 *	     @move_to, @move_to_put @erase_pending, @wl_scheduled, @works,
 *	     @erroneous, and @erroneous_peb_count fields
 * @move_mutex: serializes eraseblock moves
 * @work_sem: used to wait for all the scheduled works to finish and prevent
 * new works from being submitted
 * @wl_scheduled: non-zero if the wear-leveling was scheduled
 * @lookuptbl: a table to quickly find a &struct ubi_wl_entry object for any
 *             physical eraseblock
 * @move_from: physical eraseblock from where the data is being moved
 * @move_to: physical eraseblock where the data is being moved to
 * @move_to_put: if the "to" PEB was put
 * @works: list of pending works
 * @works_count: count of pending works
 * @bgt_thread: background thread description object
 * @thread_enabled: if the background thread is enabled
 * @bgt_name: background thread name
 *
 * @flash_size: underlying MTD device size (in bytes)
 * @peb_count: count of physical eraseblocks on the MTD device
 * @peb_size: physical eraseblock size
 * @bad_peb_limit: top limit of expected bad physical eraseblocks
 * @bad_peb_count: count of bad physical eraseblocks
 * @good_peb_count: count of good physical eraseblocks
 * @corr_peb_count: count of corrupted physical eraseblocks (preserved and not
 *                  used by UBI)
 * @erroneous_peb_count: count of erroneous physical eraseblocks in @erroneous
 * @max_erroneous: maximum allowed amount of erroneous physical eraseblocks
 * @min_io_size: minimal input/output unit size of the underlying MTD device
 * @hdrs_min_io_size: minimal I/O unit size used for VID and EC headers
 * @ro_mode: if the UBI device is in read-only mode
 * @leb_size: logical eraseblock size
 * @leb_start: starting offset of logical eraseblocks within physical
 *             eraseblocks
 * @ec_hdr_alsize: size of the EC header aligned to @hdrs_min_io_size
 * @vid_hdr_alsize: size of the VID header aligned to @hdrs_min_io_size
 * @vid_hdr_offset: starting offset of the volume identifier header (might be
 *                  unaligned)
 * @vid_hdr_aloffset: starting offset of the VID header aligned to
 * @hdrs_min_io_size
 * @vid_hdr_shift: contains @vid_hdr_offset - @vid_hdr_aloffset
 * @bad_allowed: whether the MTD device admits of bad physical eraseblocks or
 *               not
 * @nor_flash: non-zero if working on top of NOR flash
 * @max_write_size: maximum amount of bytes the underlying flash can write at a
 *                  time (MTD write buffer size)
 * @mtd: MTD device descriptor
 *
 * @peb_buf: a buffer of PEB size used for different purposes
 * @buf_mutex: protects @peb_buf
 * @ckvol_mutex: serializes static volume checking when opening
 *
 * @dbg: debugging information for this UBI device
 */
struct ubi_device {
	struct cdev cdev;
	struct device dev;
	int ubi_num;
	char ubi_name[sizeof(UBI_NAME_STR)+5];
	int vol_count;
	struct ubi_volume *volumes[UBI_MAX_VOLUMES+UBI_INT_VOL_COUNT];
	spinlock_t volumes_lock;
	int ref_count;
	int image_seq;

	int rsvd_pebs;
	int avail_pebs;
	int beb_rsvd_pebs;
	int beb_rsvd_level;
	int bad_peb_limit;

	int autoresize_vol_id;
	int vtbl_slots;
	int vtbl_size;
	struct ubi_vtbl_record *vtbl;
	struct mutex device_mutex;

	int max_ec;
	/* Note, mean_ec is not updated run-time - should be fixed */
	int mean_ec;
/*MTK start: wl/ec status*/
	uint64_t ec_sum;
	int wl_count;
	uint64_t wl_size;
	int scrub_count;
	uint64_t scrub_size;
	int wl_th;
	int torture;
	atomic_t ec_count;
	atomic_t move_retry;
	atomic_t lbb;
/*MTK end*/

	/* EBA sub-system's stuff */
	unsigned long long global_sqnum;
	spinlock_t ltree_lock;
	struct rb_root ltree;
	struct mutex alc_mutex;

	/* Fastmap stuff */
	int fm_disabled;
	struct ubi_fastmap_layout *fm;
	struct ubi_fm_pool fm_pool;
	struct ubi_fm_pool fm_wl_pool;
	struct rw_semaphore fm_sem;
	struct mutex fm_mutex;
	void *fm_buf;
	size_t fm_size;
	struct work_struct fm_work;

	/* Wear-leveling sub-system's stuff */
	struct rb_root used;
	struct rb_root erroneous;
	struct rb_root free;
	int free_count;
	struct rb_root scrub;
	struct list_head pq[UBI_PROT_QUEUE_LEN];
	int pq_head;
	spinlock_t wl_lock;
	struct mutex move_mutex;
	struct rw_semaphore work_sem;
	int wl_scheduled;
	struct ubi_wl_entry **lookuptbl;
	struct ubi_wl_entry *move_from;
	struct ubi_wl_entry *move_to;
	int move_to_put;
	struct list_head works;
	int works_count;
	struct task_struct *bgt_thread;
	int thread_enabled;
	char bgt_name[sizeof(UBI_BGT_NAME_PATTERN)+2];

	/* I/O sub-system's stuff */
	long long flash_size;
	int peb_count;
	int peb_size;
	int bad_peb_count;
	int good_peb_count;
	int corr_peb_count;
	int erroneous_peb_count;
	int max_erroneous;
	int min_io_size;
	int hdrs_min_io_size;
	int ro_mode;
	int leb_size;
	int leb_start;
	int ec_hdr_alsize;
	int vid_hdr_alsize;
	int vid_hdr_offset;
	int vid_hdr_aloffset;
	int vid_hdr_shift;
	unsigned int bad_allowed:1;
	unsigned int nor_flash:1;
	int max_write_size;
	struct mtd_info *mtd;

	void *peb_buf;
#ifndef CONFIG_UBI_SHARE_BUFFER
	struct mutex buf_mutex;
#endif
	struct mutex ckvol_mutex;

	struct ubi_debug_info dbg;
#ifdef CONFIG_MTD_UBI_LOWPAGE_BACKUP
	int next_offset[2];
	int leb_scrub[2];
	struct mutex blb_mutex;
	void *databuf;
	void *oobbuf;
	int scanning;
#endif
#ifdef CONFIG_MTK_SLC_BUFFER_SUPPORT
	atomic_t tlc_ec_count;
	int tlc_max_ec;
	int tlc_mean_ec;
	uint64_t tlc_ec_sum;
	int tlc_wl_th;
	int mtbl_slots;
	int mtbl_size;
	int mtbl_count;
	struct mutex mtbl_mutex;
	struct ubi_mtbl_record *mtbl;
	struct ubi_mtbl_record *empty_mtbl_record;
	struct rb_root tlc_used;
	struct rb_root tlc_free;
	struct rb_root archive;
	int tlc_free_count;
	int archive_count;
#endif

#ifdef CONFIG_MTK_HIBERNATION
	int ipoh_ops;
#endif

};

/**
 * struct ubi_ainf_peb - attach information about a physical eraseblock.
 * @ec: erase counter (%UBI_UNKNOWN if it is unknown)
 * @pnum: physical eraseblock number
 * @vol_id: ID of the volume this LEB belongs to
 * @lnum: logical eraseblock number
 * @scrub: if this physical eraseblock needs scrubbing
 * @copy_flag: this LEB is a copy (@copy_flag is set in VID header of this LEB)
 * @sqnum: sequence number
 * @u: unions RB-tree or @list links
 * @u.rb: link in the per-volume RB-tree of &struct ubi_ainf_peb objects
 * @u.list: link in one of the eraseblock lists
 *
 * One object of this type is allocated for each physical eraseblock when
 * attaching an MTD device. Note, if this PEB does not belong to any LEB /
 * volume, the @vol_id and @lnum fields are initialized to %UBI_UNKNOWN.
 */
struct ubi_ainf_peb {
	int ec;
	int pnum;
	int vol_id;
	int lnum;
	unsigned int scrub:1;
	unsigned int copy_flag:1;
#ifdef CONFIG_MTK_SLC_BUFFER_SUPPORT
	unsigned int tlc:1;
#endif
	unsigned long long sqnum;
	union {
		struct rb_node rb;
		struct list_head list;
	} u;
};

/**
 * struct ubi_ainf_volume - attaching information about a volume.
 * @vol_id: volume ID
 * @highest_lnum: highest logical eraseblock number in this volume
 * @leb_count: number of logical eraseblocks in this volume
 * @vol_type: volume type
 * @used_ebs: number of used logical eraseblocks in this volume (only for
 *            static volumes)
 * @last_data_size: amount of data in the last logical eraseblock of this
 *                  volume (always equivalent to the usable logical eraseblock
 *                  size in case of dynamic volumes)
 * @data_pad: how many bytes at the end of logical eraseblocks of this volume
 *            are not used (due to volume alignment)
 * @compat: compatibility flags of this volume
 * @rb: link in the volume RB-tree
 * @root: root of the RB-tree containing all the eraseblock belonging to this
 *        volume (&struct ubi_ainf_peb objects)
 *
 * One object of this type is allocated for each volume when attaching an MTD
 * device.
 */
struct ubi_ainf_volume {
	int vol_id;
	int highest_lnum;
	int leb_count;
	int vol_type;
	int used_ebs;
	int last_data_size;
	int data_pad;
	int compat;
	struct rb_node rb;
	struct rb_root root;
};

/**
 * struct ubi_attach_info - MTD device attaching information.
 * @volumes: root of the volume RB-tree
 * @corr: list of corrupted physical eraseblocks
 * @free: list of free physical eraseblocks
 * @erase: list of physical eraseblocks which have to be erased
 * @alien: list of physical eraseblocks which should not be used by UBI (e.g.,
 *         those belonging to "preserve"-compatible internal volumes)
 * @waiting: list of physical eraseblocks which mabybe fix by BACKUP_LSB
 * @corr_peb_count: count of PEBs in the @corr list
 * @empty_peb_count: count of PEBs which are presumably empty (contain only
 *                   0xFF bytes)
 * @alien_peb_count: count of PEBs in the @alien list
 * @bad_peb_count: count of bad physical eraseblocks
 * @maybe_bad_peb_count: count of bad physical eraseblocks which are not marked
 *                       as bad yet, but which look like bad
 * @vols_found: number of volumes found
 * @highest_vol_id: highest volume ID
 * @is_empty: flag indicating whether the MTD device is empty or not
 * @min_ec: lowest erase counter value
 * @max_ec: highest erase counter value
 * @max_sqnum: highest sequence number value
 * @mean_ec: mean erase counter value
 * @ec_sum: a temporary variable used when calculating @mean_ec
 * @ec_count: a temporary variable used when calculating @mean_ec
 * @aeb_slab_cache: slab cache for &struct ubi_ainf_peb objects
 *
 * This data structure contains the result of attaching an MTD device and may
 * be used by other UBI sub-systems to build final UBI data structures, further
 * error-recovery and so on.
 */
struct ubi_attach_info {
	struct rb_root volumes;
	struct list_head corr;
	struct list_head free;
	struct list_head erase;
	struct list_head alien;
#ifdef CONFIG_MTD_UBI_LOWPAGE_BACKUP
	struct list_head waiting;
#endif
	int corr_peb_count;
	int empty_peb_count;
	int alien_peb_count;
	int bad_peb_count;
	int maybe_bad_peb_count;
	int vols_found;
	int highest_vol_id;
	int is_empty;
	int min_ec;
	int max_ec;
	unsigned long long max_sqnum;
	int mean_ec;
	uint64_t ec_sum;
	int ec_count;
#ifdef CONFIG_MTK_SLC_BUFFER_SUPPORT
	int tlc_min_ec;
	int tlc_max_ec;
	int tlc_mean_ec;
	uint64_t tlc_ec_sum;
	int tlc_ec_count;
#endif
	struct kmem_cache *aeb_slab_cache;
};

/**
 * struct ubi_work - UBI work description data structure.
 * @list: a link in the list of pending works
 * @func: worker function
 * @e: physical eraseblock to erase
 * @vol_id: the volume ID on which this erasure is being performed
 * @lnum: the logical eraseblock number
 * @torture: if the physical eraseblock has to be tortured
 * @anchor: produce a anchor PEB to by used by fastmap
 *
 * The @func pointer points to the worker function. If the @shutdown argument is
 * not zero, the worker has to free the resources and exit immediately as the
 * WL sub-system is shutting down.
 * The worker has to return zero in case of success and a negative error code in
 * case of failure.
 */
struct ubi_work {
	struct list_head list;
	int (*func)(struct ubi_device *ubi, struct ubi_work *wrk, int shutdown);
	/* The below fields are only relevant to erasure works */
	struct ubi_wl_entry *e;
	int vol_id;
	int lnum;
	int torture;
	int anchor;
};

#include "debug.h"

extern struct kmem_cache *ubi_wl_entry_slab;
extern const struct file_operations ubi_ctrl_cdev_operations;
extern const struct file_operations ubi_cdev_operations;
extern const struct file_operations ubi_vol_cdev_operations;
extern struct class *ubi_class;
extern struct mutex ubi_devices_mutex;
extern struct blocking_notifier_head ubi_notifiers;
#ifdef CONFIG_MTD_UBI_LOWPAGE_BACKUP
extern u32 mtk_nand_paired_page_transfer(u32, bool);
#endif

#ifdef CONFIG_UBI_SHARE_BUFFER
extern struct mutex ubi_buf_mutex;
#endif

#ifdef CONFIG_MTK_SLC_BUFFER_SUPPORT
extern u64 mtd_partition_start_address(struct mtd_info *mtd);
extern bool mtk_block_istlc(u64 addr);
#endif

/* attach.c */
int ubi_add_to_av(struct ubi_device *ubi, struct ubi_attach_info *ai, int pnum,
		  int ec, const struct ubi_vid_hdr *vid_hdr, int bitflips);
struct ubi_ainf_volume *ubi_find_av(const struct ubi_attach_info *ai,
				    int vol_id);
void ubi_remove_av(struct ubi_attach_info *ai, struct ubi_ainf_volume *av);
struct ubi_ainf_peb *ubi_early_get_peb(struct ubi_device *ubi,
				       struct ubi_attach_info *ai);
int ubi_attach(struct ubi_device *ubi, int force_scan);
void ubi_destroy_ai(struct ubi_attach_info *ai);
#ifdef CONFIG_MTK_SLC_BUFFER_SUPPORT
int ubi_peb_istlc(struct ubi_device *ubi, int pnum);
int ubi_trigger_archive_by_slc(struct ubi_device *ubi);
#endif

/* vtbl.c */
int ubi_change_vtbl_record(struct ubi_device *ubi, int idx,
			   struct ubi_vtbl_record *vtbl_rec);
int ubi_vtbl_rename_volumes(struct ubi_device *ubi,
			    struct list_head *rename_list);
int ubi_read_volume_table(struct ubi_device *ubi, struct ubi_attach_info *ai);
#ifdef CONFIG_MTK_SLC_BUFFER_SUPPORT
int ubi_read_mtbl_record(struct ubi_device *ubi, struct ubi_attach_info *ai, int peb_count);
int ubi_change_mtbl_record(struct ubi_device *ubi, int idx, int ec, int vol_id, int map);
int ubi_change_empty_ec(struct ubi_device *ubi, int pnum, int ec, int vol_id, int map);
int ubi_wipe_mtbl_record(struct ubi_device *ubi, int vol_id);
#endif

/* vmt.c */
int ubi_create_volume(struct ubi_device *ubi, struct ubi_mkvol_req *req);
int ubi_remove_volume(struct ubi_volume_desc *desc, int no_vtbl);
int ubi_resize_volume(struct ubi_volume_desc *desc, int reserved_pebs);
int ubi_rename_volumes(struct ubi_device *ubi, struct list_head *rename_list);
int ubi_add_volume(struct ubi_device *ubi, struct ubi_volume *vol);
void ubi_free_volume(struct ubi_device *ubi, struct ubi_volume *vol);

/* upd.c */
int ubi_start_update(struct ubi_device *ubi, struct ubi_volume *vol,
		     long long bytes);
int ubi_more_update_data(struct ubi_device *ubi, struct ubi_volume *vol,
			 const void __user *buf, int count);
int ubi_start_leb_change(struct ubi_device *ubi, struct ubi_volume *vol,
			 const struct ubi_leb_change_req *req);
int ubi_more_leb_change_data(struct ubi_device *ubi, struct ubi_volume *vol,
			     const void __user *buf, int count);

/* misc.c */
int ubi_calc_data_len(const struct ubi_device *ubi, const void *buf,
		      int length);
int ubi_check_volume(struct ubi_device *ubi, int vol_id);
void ubi_update_reserved(struct ubi_device *ubi);
void ubi_calculate_reserved(struct ubi_device *ubi);
int ubi_check_pattern(const void *buf, uint8_t patt, int size);

/* eba.c */
#ifdef CONFIG_MTD_UBI_LOWPAGE_BACKUP
int blb_record_page1(struct ubi_device *ubi, int pnum,
			 struct ubi_vid_hdr *vidh, int work);
int blb_get_startpage(void);
int ubi_get_compat(const struct ubi_device *ubi, int vol_id);
#endif
int ubi_eba_unmap_leb(struct ubi_device *ubi, struct ubi_volume *vol,
		      int lnum);
int ubi_eba_read_leb(struct ubi_device *ubi, struct ubi_volume *vol, int lnum,
		     void *buf, int offset, int len, int check);
int ubi_eba_write_leb(struct ubi_device *ubi, struct ubi_volume *vol, int lnum,
		      const void *buf, int offset, int len);
int ubi_eba_write_leb_st(struct ubi_device *ubi, struct ubi_volume *vol,
			 int lnum, const void *buf, int len, int used_ebs);
int ubi_eba_atomic_leb_change(struct ubi_device *ubi, struct ubi_volume *vol,
			      int lnum, const void *buf, int len);
int ubi_eba_copy_leb(struct ubi_device *ubi, int from, int to,
		     struct ubi_vid_hdr *vid_hdr, int do_wl);
int ubi_eba_init(struct ubi_device *ubi, struct ubi_attach_info *ai);
unsigned long long ubi_next_sqnum(struct ubi_device *ubi);
int self_check_eba(struct ubi_device *ubi, struct ubi_attach_info *ai_fastmap,
		   struct ubi_attach_info *ai_scan);
#ifdef CONFIG_MTK_SLC_BUFFER_SUPPORT
int ubi_eba_copy_tlc_leb(struct ubi_device *ubi, int from, int to,
		struct ubi_vid_hdr *vid_hdr, int do_wl);
int ubi_eba_write_tlc_leb(struct ubi_device *ubi, struct ubi_volume *vol, int lnum,
	  const void *buf, int offset, int len);
int ensure_slc_archive(struct ubi_device *ubi, int pnum);
#endif

/* wl.c */
int ubi_wl_get_peb(struct ubi_device *ubi);
int ubi_wl_put_peb(struct ubi_device *ubi, int vol_id, int lnum,
		   int pnum, int torture);
#ifdef CONFIG_MTK_SLC_BUFFER_SUPPORT
struct ubi_wl_entry *get_peb_for_tlc_wl(struct ubi_device *ubi);
struct ubi_wl_entry *ubi_wl_get_tlc_peb(struct ubi_device *ubi);
int ubi_wl_archive_leb(struct ubi_device *ubi, struct ubi_volume *vol, int lnum);
int __ubi_wl_archive_leb(struct ubi_device *ubi, int pnum);
#endif
int ubi_wl_flush(struct ubi_device *ubi, int vol_id, int lnum);
int ubi_wl_scrub_peb(struct ubi_device *ubi, int pnum);
int ubi_wl_init(struct ubi_device *ubi, struct ubi_attach_info *ai);
void ubi_wl_close(struct ubi_device *ubi);
int ubi_thread(void *u);
struct ubi_wl_entry *ubi_wl_get_fm_peb(struct ubi_device *ubi, int anchor);
int ubi_wl_put_fm_peb(struct ubi_device *ubi, struct ubi_wl_entry *used_e,
		      int lnum, int torture);
int ubi_is_erase_work(struct ubi_work *wrk);
void ubi_refill_pools(struct ubi_device *ubi);
int ubi_ensure_anchor_pebs(struct ubi_device *ubi);
int sync_erase(struct ubi_device *ubi, struct ubi_wl_entry *e, int torture);
void ubi_wl_move_pg_to_used(struct ubi_device *ubi, int pnum);

/* io.c */
int ubi_io_read(const struct ubi_device *ubi, void *buf, int pnum, int offset,
		int len);
int ubi_io_write(struct ubi_device *ubi, const void *buf, int pnum, int offset,
		 int len);
int ubi_io_sync_erase(struct ubi_device *ubi, int pnum, int torture);
int ubi_io_is_bad(const struct ubi_device *ubi, int pnum);
int ubi_io_mark_bad(const struct ubi_device *ubi, int pnum);
int ubi_io_read_ec_hdr(struct ubi_device *ubi, int pnum,
		       struct ubi_ec_hdr *ec_hdr, int verbose);
int ubi_io_write_ec_hdr(struct ubi_device *ubi, int pnum,
			struct ubi_ec_hdr *ec_hdr);
int ubi_io_read_vid_hdr(struct ubi_device *ubi, int pnum,
			struct ubi_vid_hdr *vid_hdr, int verbose);
int ubi_io_write_vid_hdr(struct ubi_device *ubi, int pnum,
			 struct ubi_vid_hdr *vid_hdr);
#ifdef CONFIG_MTD_UBI_LOWPAGE_BACKUP
int ubi_io_write_vid_hdr_blb(struct ubi_device *ubi, int pnum,
			 struct ubi_vid_hdr *vid_hdr);
int ubi_backup_init_scan(struct ubi_device *ubi, struct ubi_attach_info *ai);
int ubi_io_read_oob(const struct ubi_device *ubi, void *databuf, void *oobbuf,
		    int pnum, int offset);
int ubi_io_write_oob(const struct ubi_device *ubi, void *databuf, void *oobbuf,
		    int pnum, int offset);
#endif
#ifdef CONFIG_MTK_SLC_BUFFER_SUPPORT
int ubi_io_fill_ec_hdr(struct ubi_device *ubi, int pnum, struct ubi_ec_hdr *ec_hdr, int ec);
int ubi_io_fill_vid_hdr(struct ubi_device *ubi, int pnum, struct ubi_vid_hdr *vid_hdr);
#endif

/* build.c */
int ubi_attach_mtd_dev(struct mtd_info *mtd, int ubi_num,
		       int vid_hdr_offset, int max_beb_per1024);
int ubi_detach_mtd_dev(int ubi_num, int anyway);
struct ubi_device *ubi_get_device(int ubi_num);
void ubi_put_device(struct ubi_device *ubi);
struct ubi_device *ubi_get_by_major(int major);
int ubi_major2num(int major);
int ubi_volume_notify(struct ubi_device *ubi, struct ubi_volume *vol,
		      int ntype);
int ubi_notify_all(struct ubi_device *ubi, int ntype,
		   struct notifier_block *nb);
int ubi_enumerate_volumes(struct notifier_block *nb);
void ubi_free_internal_volumes(struct ubi_device *ubi);

/* kapi.c */
void ubi_do_get_device_info(struct ubi_device *ubi, struct ubi_device_info *di);
void ubi_do_get_volume_info(struct ubi_device *ubi, struct ubi_volume *vol,
			    struct ubi_volume_info *vi);
/* scan.c */
int ubi_compare_lebs(struct ubi_device *ubi, const struct ubi_ainf_peb *aeb,
		      int pnum, const struct ubi_vid_hdr *vid_hdr);

/* fastmap.c */
size_t ubi_calc_fm_size(struct ubi_device *ubi);
int ubi_update_fastmap(struct ubi_device *ubi);
int ubi_scan_fastmap(struct ubi_device *ubi, struct ubi_attach_info *ai,
		     int fm_anchor);

/* block.c */
#ifdef CONFIG_MTD_UBI_BLOCK
int ubiblock_init(void);
void ubiblock_exit(void);
int ubiblock_create(struct ubi_volume_info *vi);
int ubiblock_remove(struct ubi_volume_info *vi);
#else
static inline int ubiblock_init(void) { return 0; }
static inline void ubiblock_exit(void) {}
static inline int ubiblock_create(struct ubi_volume_info *vi)
{
	return -ENOSYS;
}
static inline int ubiblock_remove(struct ubi_volume_info *vi)
{
	return -ENOSYS;
}
#endif


/*
 * ubi_rb_for_each_entry - walk an RB-tree.
 * @rb: a pointer to type 'struct rb_node' to use as a loop counter
 * @pos: a pointer to RB-tree entry type to use as a loop counter
 * @root: RB-tree's root
 * @member: the name of the 'struct rb_node' within the RB-tree entry
 */
#define ubi_rb_for_each_entry(rb, pos, root, member)                         \
	for (rb = rb_first(root),                                            \
	     pos = (rb ? container_of(rb, typeof(*pos), member) : NULL);     \
	     rb;                                                             \
	     rb = rb_next(rb),                                               \
	     pos = (rb ? container_of(rb, typeof(*pos), member) : NULL))

/*
 * ubi_move_aeb_to_list - move a PEB from the volume tree to a list.
 *
 * @av: volume attaching information
 * @aeb: attaching eraseblock information
 * @list: the list to move to
 */
static inline void ubi_move_aeb_to_list(struct ubi_ainf_volume *av,
					 struct ubi_ainf_peb *aeb,
					 struct list_head *list)
{
		rb_erase(&aeb->u.rb, &av->root);
		list_add_tail(&aeb->u.list, list);
}

/**
 * ubi_zalloc_vid_hdr - allocate a volume identifier header object.
 * @ubi: UBI device description object
 * @gfp_flags: GFP flags to allocate with
 *
 * This function returns a pointer to the newly allocated and zero-filled
 * volume identifier header object in case of success and %NULL in case of
 * failure.
 */
static inline struct ubi_vid_hdr *
ubi_zalloc_vid_hdr(const struct ubi_device *ubi, gfp_t gfp_flags)
{
	void *vid_hdr;

	vid_hdr = kzalloc(ubi->vid_hdr_alsize, gfp_flags);
	if (!vid_hdr)
		return NULL;

	/*
	 * VID headers may be stored at un-aligned flash offsets, so we shift
	 * the pointer.
	 */
	return vid_hdr + ubi->vid_hdr_shift;
}

/**
 * ubi_free_vid_hdr - free a volume identifier header object.
 * @ubi: UBI device description object
 * @vid_hdr: the object to free
 */
static inline void ubi_free_vid_hdr(const struct ubi_device *ubi,
				    struct ubi_vid_hdr *vid_hdr)
{
	void *p = vid_hdr;

	if (!p)
		return;

	kfree(p - ubi->vid_hdr_shift);
}

/*
 * This function is equivalent to 'ubi_io_read()', but @offset is relative to
 * the beginning of the logical eraseblock, not to the beginning of the
 * physical eraseblock.
 */
static inline int ubi_io_read_data(const struct ubi_device *ubi, void *buf,
				   int pnum, int offset, int len)
{
	ubi_assert(offset >= 0);
	return ubi_io_read(ubi, buf, pnum, offset + ubi->leb_start, len);
}

/*
 * This function is equivalent to 'ubi_io_write()', but @offset is relative to
 * the beginning of the logical eraseblock, not to the beginning of the
 * physical eraseblock.
 */
#ifdef CONFIG_MTK_SLC_BUFFER_SUPPORT
static inline int ubi_io_write_tlc_data(struct ubi_device *ubi, const void *buf,
				    int pnum, int offset, int len)
{
	ubi_assert(offset == 0);
	ubi_assert(len == ubi->peb_size);
	return ubi_io_write(ubi, buf, pnum, offset, len);
}
#endif
static inline int ubi_io_write_data(struct ubi_device *ubi, const void *buf,
				    int pnum, int offset, int len)
{
	ubi_assert(offset >= 0);
	return ubi_io_write(ubi, buf, pnum, offset + ubi->leb_start, len);
}

/**
 * ubi_ro_mode - switch to read-only mode.
 * @ubi: UBI device description object
 */
static inline void ubi_ro_mode(struct ubi_device *ubi)
{
	if (!ubi->ro_mode) {
		ubi->ro_mode = 1;
		ubi_warn("switch to read-only mode");
		dump_stack();
	}
}

/**
 * vol_id2idx - get table index by volume ID.
 * @ubi: UBI device description object
 * @vol_id: volume ID
 */
static inline int vol_id2idx(const struct ubi_device *ubi, int vol_id)
{
	if (vol_id >= UBI_INTERNAL_VOL_START)
		return vol_id - UBI_INTERNAL_VOL_START + ubi->vtbl_slots;
	else
		return vol_id;
}

/**
 * idx2vol_id - get volume ID by table index.
 * @ubi: UBI device description object
 * @idx: table index
 */
static inline int idx2vol_id(const struct ubi_device *ubi, int idx)
{
	if (idx >= ubi->vtbl_slots)
		return idx - ubi->vtbl_slots + UBI_INTERNAL_VOL_START;
	else
		return idx;
}

#endif /* !__UBI_UBI_H__ */
