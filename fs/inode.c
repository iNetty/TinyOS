#include "inode.h"
#include "debug.h"
#include "file.h"
#include "fs.h"
#include "global.h"
#include "interrupt.h"
#include "list.h"
#include "memory.h"
#include "stdio-kernel.h"
#include "string.h"
#include "super_block.h"

// inode position
typedef struct _INODE_POSITION {
    bool two_sec;       // whether the inode occupy two sectors
    uint32_t sec_lba;   // sector number of inode
    uint32_t off_size;  // inode BYTE offset in the sector
} inode_position, INODE_POSITION, *PINODE_POSITION;

// get the sector and offset of inode
static void inode_locate( PPARTITION part, uint32_t inode_no, PINODE_POSITION inode_pos ) {
    // inode_table is continuous
    ASSERT( inode_no < 4096 );
    uint32_t inode_table_lba = part->sb->inode_table_lba;

    uint32_t inode_size = sizeof( INODE );
    uint32_t off_size = inode_no * inode_size;  // inode_table_lba BYTE offset of the inode_no node
    uint32_t off_sec = off_size / 512;          // inode_table_lba SECTOR offset of the inode_no node
    uint32_t off_size_in_sec = off_size % 512;  // offset in sector

    // judge whether the inode is across two_sec
    uint32_t left_in_sec = 512 - off_size_in_sec;
    if ( left_in_sec < inode_size ) {  // if no enogh left space, the inode across two_sec
        inode_pos->two_sec = true;
    } else {
        inode_pos->two_sec = false;
    }
    inode_pos->sec_lba = inode_table_lba + off_sec;
    inode_pos->off_size = off_size_in_sec;
}

// write inode to partition
// io_buf is the buffer to ide write
void inode_sync( PPARTITION part, PINODE inode, void* io_buf ) {
    uint8_t inode_no = inode->i_number;
    INODE_POSITION inode_pos;
    inode_locate( part, inode_no, &inode_pos );  // store inode position info to inode_pos
    ASSERT( inode_pos.sec_lba <= ( part->start_lba + part->sec_cnt ) );

    // we don't need to use inode_tag or i_open_cnts
    // they are only used in memory to record inode_list and open counts
    INODE pure_inode;
    memcpy( &pure_inode, inode, sizeof( INODE ) );

    // reset
    pure_inode.i_open_cnts = 0;
    pure_inode.write_deny = false;  // ensure writeable in the next time
    pure_inode.inode_tag.prev = pure_inode.inode_tag.next = NULL;

    char* inode_buf = ( char* )io_buf;
    if ( inode_pos.two_sec ) {
        // cope with two sectors
        ide_read( part->my_disk, inode_pos.sec_lba, inode_buf, 2 );

        // write to buffer
        memcpy( ( inode_buf + inode_pos.off_size ), &pure_inode, sizeof( INODE ) );

        // write to disk
        ide_write( part->my_disk, inode_pos.sec_lba, inode_buf, 2 );
    } else {
        // cope with one sector
        ide_read( part->my_disk, inode_pos.sec_lba, inode_buf, 1 );
        memcpy( ( inode_buf + inode_pos.off_size ), &pure_inode, sizeof( INODE ) );
        ide_write( part->my_disk, inode_pos.sec_lba, inode_buf, 1 );
    }
}

// return the inode according to the inode number
PINODE inode_open( PPARTITION part, uint32_t inode_no ) {
    // firstly find inode in the open_inodes list
    // store in memory in order to increase speed
    PLIST_NODE elem = part->open_inodes.head.next;
    PINODE inode_found;
    while ( elem != &part->open_inodes.tail ) {
        inode_found = elem2entry( INODE, inode_tag, elem );
        if ( inode_found->i_number == inode_no ) {
            inode_found->i_open_cnts++;
            return inode_found;
        }
        elem = elem->next;
    }

    // since cannot find inode in open_inodes list, we load it from disk and add to open_inodes list
    INODE_POSITION inode_pos;

    // get inode position
    inode_locate( part, inode_no, &inode_pos );

    // temporarily claer pgdir to malloc kernel memory for inode to share with all app
    // when we free this inode, we should clear pgdir as well.
    PTASK_STRUCT cur = running_thread();
    uint32_t* cur_pagedir_bak = cur->pgdir;
    cur->pgdir = NULL;
    inode_found = ( PINODE )sys_malloc( sizeof( INODE ) );
    // recover pgdir
    cur->pgdir = cur_pagedir_bak;

    char* inode_buf;
    if ( inode_pos.two_sec ) {
        // cope with two sectors
        inode_buf = ( char* )sys_malloc( 1024 );

        ide_read( part->my_disk, inode_pos.sec_lba, inode_buf, 2 );
    } else {
        // cope with one sector

        inode_buf = ( char* )sys_malloc( 512 );
        ide_read( part->my_disk, inode_pos.sec_lba, inode_buf, 1 );
    }
    memcpy( inode_found, inode_buf + inode_pos.off_size, sizeof( INODE ) );

    // according to the Locality_of_reference, we add this inode to the front of the open_cnts lists
    list_push( &part->open_inodes, &inode_found->inode_tag );
    inode_found->i_open_cnts = 1;

    sys_free( inode_buf );
    return inode_found;
}

// close the inode or decrease the inode_open_cnts
void inode_close( PINODE inode ) {
    INTR_STATUS old_status = intr_disable();
    if ( --inode->i_open_cnts == 0 ) {
        // if inode_open_cnts==0, i.e. no application use this inode, then remove this inode
        list_remove( &inode->inode_tag );
        // clear pgdir to free kernel memory
        // inode memory is shared with all applications
        PTASK_STRUCT cur = running_thread();
        uint32_t* cur_pagedir_bak = cur->pgdir;
        cur->pgdir = NULL;
        sys_free( inode );
        cur->pgdir = cur_pagedir_bak;
    }
    intr_set_status( old_status );
}

// init inode
void inode_init( uint32_t inode_no, PINODE new_inode ) {
    new_inode->i_number = inode_no;
    new_inode->i_size = 0;
    new_inode->i_open_cnts = 0;
    new_inode->write_deny = false;

    // init i_sectors block tables
    uint8_t sec_idx = 0;
    while ( sec_idx < 13 ) {
        // i_sectors[12] is primary indirect block pointer
        new_inode->i_sectors[ sec_idx ] = 0;
        sec_idx++;
    }
}