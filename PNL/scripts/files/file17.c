// SPDX-License-Identifier: GPL-2.0
/*
 * ouiche_fs - a simple educational filesystem for Linux
 *
 * Copyright (C) 2018 Redha Gouicem <redha.gouicem@lip6.fr>
 */

#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/mpage.h>

#include "ouichefs.h"
#include "bitmap.h"

#include "ioctl.h"

/*
 * Map the buffer_head passed in argument with the iblock-th block of the file
 * represented by inode. If the requested block is not allocated and create is
 * true, allocate a new block on disk and map it.
 */
static int ouichefs_file_get_block(struct inode *inode, sector_t iblock,
				   struct buffer_head *bh_result, int create)
{
	struct super_block *sb = inode->i_sb;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
	struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
	struct ouichefs_file_index_block *index;
	struct buffer_head *bh_index;
	int ret = 0, bno;

	/* If block number exceeds filesize, fail */
	if (iblock >= OUICHEFS_BLOCK_SIZE >> 2)
		return -EFBIG;

	/* Read index block from disk */
	bh_index = sb_bread(sb, ci->index_block);
	if (!bh_index)
		return -EIO;
	index = (struct ouichefs_file_index_block *)bh_index->b_data;

	/*
	 * Check if iblock is already allocated. If not and create is true,
	 * allocate it. Else, get the physical block number.
	 */
	if (index->blocks[iblock] == 0) {
		if (!create) {
			ret = 0;
			goto brelse_index;
		}
		bno = get_free_block(sbi);
		if (!bno) {
			ret = -ENOSPC;
			goto brelse_index;
		}
		index->blocks[iblock] = bno;
	} else {
		bno = index->blocks[iblock];
	}

	/* Map the physical block to the given buffer_head */
	map_bh(bh_result, sb, bno);

brelse_index:
	brelse(bh_index);

	return ret;
}

/*
 * Called by the page cache to read a page from the physical disk and map it in
 * memory.
 */
static void ouichefs_readahead(struct readahead_control *rac)
{
	mpage_readahead(rac, ouichefs_file_get_block);
}

/*
 * Called by the page cache to write a dirty page to the physical disk (when
 * sync is called or when memory is needed).
 */
static int ouichefs_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, ouichefs_file_get_block, wbc);
}

/*
 * Called by the VFS when a write() syscall occurs on file before writing the
 * data in the page cache. This functions checks if the write will be able to
 * complete and allocates the necessary blocks through block_write_begin().
 */
static int ouichefs_write_begin(struct file *file,
				struct address_space *mapping, loff_t pos,
				unsigned int len, struct page **pagep,
				void **fsdata)
{
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(file->f_inode->i_sb);
	int err;
	uint32_t nr_allocs = 0;

	/* Check if the write can be completed (enough space?) */
	if (pos + len > OUICHEFS_MAX_FILESIZE)
		return -ENOSPC;
	nr_allocs = max(pos + len, file->f_inode->i_size) / OUICHEFS_BLOCK_SIZE;
	if (nr_allocs > file->f_inode->i_blocks - 1)
		nr_allocs -= file->f_inode->i_blocks - 1;
	else
		nr_allocs = 0;
	if (nr_allocs > sbi->nr_free_blocks)
		return -ENOSPC;

	/* prepare the write */
	err = block_write_begin(mapping, pos, len, pagep,
				ouichefs_file_get_block);
	/* if this failed, reclaim newly allocated blocks */
	if (err < 0) {
		pr_err("%s:%d: newly allocated blocks reclaim not implemented yet\n",
		       __func__, __LINE__);
	}
	return err;
}

/*
 * Called by the VFS after writing data from a write() syscall to the page
 * cache. This functions updates inode metadata and truncates the file if
 * necessary.
 */
static int ouichefs_write_end(struct file *file, struct address_space *mapping,
			      loff_t pos, unsigned int len, unsigned int copied,
			      struct page *page, void *fsdata)
{
	int ret;
	struct inode *inode = file->f_inode;
	struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
	struct super_block *sb = inode->i_sb;

	/* Complete the write() */
	ret = generic_write_end(file, mapping, pos, len, copied, page, fsdata);
	if (ret < len) {
		pr_err("%s:%d: wrote less than asked... what do I do? nothing for now...\n",
		       __func__, __LINE__);
	} else {
		uint32_t nr_blocks_old = inode->i_blocks;

		/* Update inode metadata */
		inode->i_blocks = inode->i_size / OUICHEFS_BLOCK_SIZE + 2;
		inode->i_mtime = inode->i_ctime = current_time(inode);
		mark_inode_dirty(inode);

		/* If file is smaller than before, free unused blocks */
		if (nr_blocks_old > inode->i_blocks) {
			int i;
			struct buffer_head *bh_index;
			struct ouichefs_file_index_block *index;

			/* Free unused blocks from page cache */
			truncate_pagecache(inode, inode->i_size);

			/* Read index block to remove unused blocks */
			bh_index = sb_bread(sb, ci->index_block);
			if (!bh_index) {
				pr_err("failed truncating '%s'. we just lost %llu blocks\n",
				       file->f_path.dentry->d_name.name,
				       nr_blocks_old - inode->i_blocks);
				goto end;
			}
			index = (struct ouichefs_file_index_block *)
					bh_index->b_data;

			for (i = inode->i_blocks - 1; i < nr_blocks_old - 1;
			     i++) {
				put_block(OUICHEFS_SB(sb), index->blocks[i]);
				index->blocks[i] = 0;
			}
			mark_buffer_dirty(bh_index);
			brelse(bh_index);
		}
	}
end:
	return ret;
}

const struct address_space_operations ouichefs_aops = {
	.readahead = ouichefs_readahead,
	.writepage = ouichefs_writepage,
	.write_begin = ouichefs_write_begin,
	.write_end = ouichefs_write_end
};

static int ouichefs_open(struct inode *inode, struct file *file)
{
	bool wronly = (file->f_flags & O_WRONLY) != 0;
	bool rdwr = (file->f_flags & O_RDWR) != 0;
	bool trunc = (file->f_flags & O_TRUNC) != 0;

	if ((wronly || rdwr) && trunc && (inode->i_size != 0)) {
		struct super_block *sb = inode->i_sb;
		struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
		struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
		struct ouichefs_file_index_block *index;
		struct buffer_head *bh_index;
		sector_t iblock;

		/* Read index block from disk */
		bh_index = sb_bread(sb, ci->index_block);
		if (!bh_index)
			return -EIO;
		index = (struct ouichefs_file_index_block *)bh_index->b_data;

		for (iblock = 0; index->blocks[iblock] != 0; iblock++) {
			put_block(sbi, index->blocks[iblock]);
			index->blocks[iblock] = 0;
		}
		inode->i_size = 0;
		inode->i_blocks = 0;

		brelse(bh_index);
	}
	return 0;
}

static ssize_t ouichefs_read(struct file *file,
			char __user *buf, size_t count, loff_t *pos)
{
	if (*pos >= file->f_inode->i_size)
		return 0;

	unsigned long to_be_copied = 0;
	unsigned long copied_to_user = 0;

	struct super_block *sb = file->f_inode->i_sb;
	sector_t iblock = *pos / OUICHEFS_BLOCK_SIZE;
	size_t offset, bsize12, bnum20;
	int bno;

	struct ouichefs_file_index_block *index;
	struct buffer_head *bh_index;
	struct ouichefs_inode_info *ci = OUICHEFS_INODE(file->f_inode);

	/* If block number exceeds filesize, fail */
	if (iblock >= OUICHEFS_BLOCK_SIZE >> 2)
		return -EFBIG;

	/* Read index block from disk */
	bh_index = sb_bread(sb, ci->index_block);
	if (!bh_index)
		return -EIO;
	index = (struct ouichefs_file_index_block *)bh_index->b_data;

	/* get iblock with offset associated to pos */
	offset = *pos;
	for (iblock = 0; iblock < (OUICHEFS_BLOCK_SIZE>>2);
		iblock++) {
		bno = index->blocks[iblock];
		bsize12 = (bno & BLOCK_SIZE_MASK) >> 20;
		if (bno == 0) {
			brelse(bh_index);
			return -EIO;
		}
		if (bsize12 > offset
			|| index->blocks[iblock+1] == 0) {
			break;
		}
		offset -= (size_t) bsize12;
	}

	/* Get the block number for the current iblock */
	bno = index->blocks[iblock];
	bsize12 = (bno & BLOCK_SIZE_MASK) >> 20;
	bnum20 = bno & BLOCK_NUMBER_MASK;

	/* block must exist */
	if (bno == 0) {
		brelse(bh_index);
		return -EIO;
	}

	/* get the block */
	struct buffer_head *bh = sb_bread(sb, bnum20);

	if (!bh) {
		brelse(bh_index);
		return -EIO;
	}

	char *buffer = bh->b_data;

	/* shift buffer pointer */
	buffer += offset % OUICHEFS_BLOCK_SIZE;

	/* copy the minimum bytes */
	if (bsize12 < count)
		to_be_copied = bsize12;
	else
		to_be_copied = count;


	/* copy to user */
	copied_to_user = to_be_copied
			- copy_to_user(buf, buffer, to_be_copied);

	/* update variables */
	*pos += copied_to_user;
	file->f_pos = *pos;

	brelse(bh);
	brelse(bh_index);

	return copied_to_user;
}

static ssize_t ouichefs_write(struct file *filep,
				const char __user *buf,
				size_t len, loff_t *ppos)
{
	struct inode *inode = filep->f_inode;
	struct super_block *sb = inode->i_sb;
	struct ouichefs_inode_info *ii = OUICHEFS_INODE(inode);
	struct ouichefs_file_index_block *index;
	struct buffer_head *bh_index, *bh;
	char *buffer;
	size_t to_write, written = 0;
	sector_t iblock;
	int bno;
	uint32_t bnum20, bsize12;
	size_t offset, remaining;

	/* adding at the end of the file*/
	if (filep->f_flags & O_APPEND)
		*ppos = inode->i_size;

	/* adding more than max filesize  */
	if (*ppos >= OUICHEFS_MAX_FILESIZE)
		return -EFBIG;

	/* Read index block from disk */
	bh_index = sb_bread(sb, ii->index_block);
	if (!bh_index)
		return -EIO;

	index = (struct ouichefs_file_index_block *)bh_index->b_data;

	/* searching iblock associated to ppos and calculate offset */
	offset = *ppos;
	for (iblock = 0;
		iblock < (OUICHEFS_BLOCK_SIZE>>2) || offset == 0;
		iblock++) {

		/* block does not exist */
		if (index->blocks[iblock] == 0) {
			bnum20 = get_free_block(OUICHEFS_SB(sb));
			if (!bnum20) {
				brelse(bh_index);
				return -ENOSPC;
			}
			/* fill until the position or fill the block */
			bsize12 = min(offset, (size_t) (OUICHEFS_BLOCK_SIZE-1));
			bno = bnum20 | (bsize12 << 20);
			index->blocks[iblock] = bno;
			inode->i_blocks++;
			mark_buffer_dirty(bh_index);
			sync_dirty_buffer(bh_index);
		} else {
			/* block exist */
			bno = index->blocks[iblock];
			bsize12 = (bno & BLOCK_SIZE_MASK) >> 20;
			bnum20 = bno & BLOCK_NUMBER_MASK;
		}
		/* found a block that can contain our data */
		/*  we are the last block*/
		if (bsize12 >= offset
			|| index->blocks[iblock+1] == 0)
			break;
		/* update our remaining offset
			before to reach pos */
		offset -= bsize12;
	}
	/* reached the max block size */
	if (iblock == (OUICHEFS_BLOCK_SIZE>>2)) {
		brelse(bh_index);
		return -ENOSPC;
	}

    /* variables for splitting our block */
	int bno1, bno2, currentBlock, precBlock = 0;
	uint32_t b1num20, b1size12, b2num20, b2size12;
	struct buffer_head *bh_bno1, *bh_bno2;

	/* block that will be divised transfered */
	bno1 = index->blocks[iblock];
	b1num20 = bno1 & BLOCK_NUMBER_MASK;
	b1size12 = (bno1 & BLOCK_SIZE_MASK) >> 20;

    /* shift blocks if next blocks are not empty */
	precBlock = index->blocks[iblock+1];
	for (int i = iblock+2;
		i < inode->i_blocks+1;
		i++) {
		if (precBlock == 0)
			break;
		currentBlock = index->blocks[i];
		index->blocks[i] = precBlock;
		precBlock = currentBlock;
		mark_buffer_dirty(bh_index);
		sync_dirty_buffer(bh_index);
	}

	/* separate the block into two blocks */
	if (offset != 0) {
		/* allocate new bloc */
		b2num20 = get_free_block(OUICHEFS_SB(sb));
		b2size12 = 0;
		bno2 = (b2size12 << 20) | b2num20;
		if (!b2num20) {
			brelse(bh_index);
			return -ENOSPC;
		}
		inode->i_blocks++;
		index->blocks[iblock+1] = bno2;
		mark_buffer_dirty(bh_index);
		sync_dirty_buffer(bh_index);

		/* get the two blocks */
		bh_bno1 = sb_bread(sb, b1num20);
		if (!bh_bno1) {
			brelse(bh_index);
			return -EIO;
		}
		bh_bno2 = sb_bread(sb, b2num20);
		if (!bh_bno2) {
			brelse(bh_bno1);
			brelse(bh_index);
			return -EIO;
		}

		/* calculate bytes to transfer into the new block */
		if ((size_t) b1size12 > offset)
			remaining = (size_t) b1size12 - offset;
		else
			/* offset is out of the block size */
			remaining = 0;

		/* transfer data into the 2nd blocks */
		memcpy(bh_bno2->b_data, bh_bno1->b_data + offset, remaining);
		mark_buffer_dirty(bh_bno2);
		sync_dirty_buffer(bh_bno2);

		/* update block size */
		b1size12 = offset; /* start of block until offset */
		b2size12 = remaining; /* offset to end of the old block */
		bno1 = b1size12 << 20 | b1num20;
		bno2 = b2size12 << 20 | b2num20;
		index->blocks[iblock] = bno1;
		index->blocks[iblock+1] = bno2;
		mark_buffer_dirty(bh_index);
		sync_dirty_buffer(bh_index);
		brelse(bh_bno1);
		brelse(bh_bno2);
		iblock += 1;
		offset = 0;
	}

	/* write until no space or all written */
	while (len > 0 && iblock != (OUICHEFS_BLOCK_SIZE>>2)) {
		bh_index = sb_bread(sb, ii->index_block);
		if (!bh_index) {
			pr_err("Failed to read index block\n");
			return -EIO;
		}
		index = (struct ouichefs_file_index_block *)bh_index->b_data;
		bno = index->blocks[iblock];

		/* the block not exists */
		if (!bno) {
			/* allocate new blocK */
			bnum20 = get_free_block(OUICHEFS_SB(sb));
			if (!bnum20) {
				brelse(bh_index);
				return -ENOSPC;
			}
			/* prepare to_write len or max block size */
			remaining = (size_t) (OUICHEFS_BLOCK_SIZE - 1)
					- offset;
			to_write = min_t(size_t, remaining,
					len);
			bsize12 = to_write;
			/* allocated size for the writing */
			bno = (bsize12 << 20) | bnum20;
			index->blocks[iblock] = bno;
			inode->i_blocks++;
			mark_buffer_dirty(bh_index);
			sync_dirty_buffer(bh_index);
		} else {
			/* block exists */
			/* write len or block size */
			bsize12 = (bno & BLOCK_SIZE_MASK) >> 20;
			to_write = min_t(size_t, bsize12, len);
		    	bnum20 = bno & BLOCK_NUMBER_MASK;
		}

		bh = sb_bread(sb, bnum20);
		if (!bh) {
			brelse(bh_index);
			return -EIO;
		}
		buffer = bh->b_data;

		/* copy to the buffer */
		if (copy_from_user(buffer + offset, buf, to_write)) {
			brelse(bh);
			brelse(bh_index);
			return -EFAULT;
		}

		mark_buffer_dirty(bh);
		sync_dirty_buffer(bh);
		brelse(bh);

		/* update block numbers in index block */
		bno = (bsize12 << 20) | bnum20;
		index->blocks[iblock] = bno;

		mark_buffer_dirty(bh_index);
		sync_dirty_buffer(bh_index);

		brelse(bh_index);

		/* update variables */
		*ppos += to_write;
		buf += to_write;
		len -= to_write;
		written += to_write;

		/* update size of the file if needed */
		if (*ppos > inode->i_size) {
			inode->i_size = *ppos;
			mark_inode_dirty(inode);
		}

		/* reset offset and get next block */
		offset = 0;
		iblock++;
	}

	return written;
}

static long ouichefs_ioctl(struct file *file,
							unsigned int cmd,
							unsigned long arg)
{
	if (_IOC_TYPE(cmd) != OUICHEFS_IOC_MAGIC) {
		pr_info("Invalid type\n");
		return -ENOTTY;
	}

	struct super_block *sb = file->f_inode->i_sb;
	struct inode *inode = file->f_inode;
	struct ouichefs_inode_info *ii = OUICHEFS_INODE(inode);
	struct buffer_head *bh_index;
	int used_blocks = 0;
	int partial_blocks = 0;
	unsigned long internal_frag = 0;
	char ret[128];

	/* index bloc */
	bh_index = sb_bread(sb, ii->index_block);
	struct ouichefs_file_index_block *index =
		(struct ouichefs_file_index_block *)bh_index->b_data;

	if (index == NULL) {
		pr_err("Failed to read index block\n");
		return -EIO;
	}

	/* calculate blocks information */
	used_blocks = inode->i_blocks;
	for (int i = 0; i < inode->i_blocks; i++) {
		uint32_t sizeb = ((index->blocks[i] & BLOCK_SIZE_MASK) >> 20);

		/* bloc is not full */
		if (sizeb < OUICHEFS_BLOCK_SIZE - 1) {
			partial_blocks++;
			internal_frag += (OUICHEFS_BLOCK_SIZE - sizeb);
		}
	}
	brelse(bh_index);

	switch (cmd) {
	case USED_BLOCKS:
		snprintf(ret, sizeof(used_blocks), "%d", used_blocks);
		if (copy_to_user((char *)arg, ret, sizeof(ret))) {
			pr_info("ouiche_ioctl: copy_to_user failed\n");
			return -EFAULT;
		}
		return 0;
	case PARTIAL_BLOCKS:
		snprintf(ret, sizeof(partial_blocks), "%d", partial_blocks);
		if (copy_to_user((char *)arg, ret, sizeof(ret))) {
			pr_info("ouiche_ioctl: copy_to_user failed\n");
			return -EFAULT;
		}
			return 0;
	case INTERNAL_FRAG:
		snprintf(ret, sizeof(internal_frag), "%lu", internal_frag);
		if (copy_to_user((char *)arg, ret, sizeof(ret))) {
			pr_info("ouiche_ioctl: copy_to_user failed\n");
			return -EFAULT;
		}
		return 0;
	case USED_BLOCKS_INFO:
		bh_index = sb_bread(sb, ii->index_block);
		if (!bh_index)
			return -EIO;
		index = (struct ouichefs_file_index_block *)bh_index->b_data;
		for (int i = 0; i < inode->i_blocks; i++) {
			/* get block number and size */
			uint32_t bn = (index->blocks[i] & BLOCK_NUMBER_MASK);
			uint32_t size = (index->blocks[i] & BLOCK_SIZE_MASK)
				>> 20;

			if (bn == 0)
				continue;

			struct buffer_head *bh = sb_bread(sb, bn);

			pr_info("block number : %d  size : %d\n", bn, size);
			brelse(bh);
		}
		brelse(bh_index);
		return 0;
	case DEFRAG:
		int bno_prec, bno_next;
		struct buffer_head *bh_prec, *bh_next;
		char *b_prec, *b_next;
		size_t to_write, remaining, defrag;
		uint32_t bnum20_prec, bsize12_prec, bnum20_next, bsize12_next;

		/* index bloc */
		bh_index = sb_bread(sb, ii->index_block);
		if (!bh_index)
			return -EIO;
		index = (struct ouichefs_file_index_block *)bh_index->b_data;

		defrag = 0; /* counter of the current contiguous data */
		int bmax = inode->i_blocks;

		for (int i = 0; i < bmax; i++) {
			/* reached the end of file */
			if (defrag == inode->i_size) {
				/* liberate the block that is not used */
				inode->i_blocks--;
				put_block(OUICHEFS_SB(sb),
				index->blocks[i] & BLOCK_NUMBER_MASK);
				index->blocks[i] = 0;
				mark_buffer_dirty(bh_index);
				sync_dirty_buffer(bh_index);
			}

			/* block is full */
			bno_prec = index->blocks[i];
			if ((bno_prec & BLOCK_SIZE_MASK) == BLOCK_SIZE_MASK) {
				defrag += OUICHEFS_BLOCK_SIZE - 1;
				continue;
			} else {
				/* block is not full */
				bsize12_prec = (bno_prec & BLOCK_SIZE_MASK)
									>> 20;
				defrag += bsize12_prec;
			}
			/* transfer next blocks data into the block */
			for (int j = i + 1; j < bmax; j++) {
				/* reach the size of the file */
				if (defrag == inode->i_size)
					break;

				/* prepare blocks information */
				bno_next = index->blocks[j];
				bnum20_prec = bno_prec & BLOCK_NUMBER_MASK;
				bsize12_prec = (bno_prec & BLOCK_SIZE_MASK)
									>> 20;
				bnum20_next = bno_next & BLOCK_NUMBER_MASK;
				bsize12_next = (bno_next & BLOCK_SIZE_MASK)
									>> 20;
				/* block is empty */
				if (bsize12_next == 0)
					continue;

				/* get blocks data */
				bh_prec = sb_bread(sb, bnum20_prec);
				if (!bh_prec) {
					brelse(bh_index);
					return -EIO;
				}
				bh_next = sb_bread(sb, bnum20_next);
				if (!bh_next) {
					brelse(bh_prec);
					brelse(bh_index);
					return -EIO;
				}
				b_prec = bh_prec->b_data;
				b_next = bh_next->b_data;

				/* determine how much to write
					in the precedent block*/
				remaining = (OUICHEFS_BLOCK_SIZE - 1)
					- bsize12_prec;
				to_write = min_t(size_t, remaining,
					bsize12_next);

				/* move data for the two blocks */
				memcpy(b_prec + bsize12_prec, b_next,
					to_write);
				mark_buffer_dirty(bh_prec);
				memcpy(b_next, b_next + to_write,
					bsize12_next - to_write);
				mark_buffer_dirty(bh_next);
				sync_dirty_buffer(bh_prec);
				sync_dirty_buffer(bh_next);

				/* calculate new size and update blocks */
				bsize12_prec += to_write;
				bsize12_next -= to_write;
				defrag += to_write;
				bno_prec = bsize12_prec << 20 | bnum20_prec;
				bno_next = bsize12_next << 20 | bnum20_next;
				index->blocks[i] = bno_prec;
				index->blocks[j] = bno_next;
				mark_buffer_dirty(bh_index);
				sync_dirty_buffer(bh_index);
				brelse(bh_prec);
				brelse(bh_next);
			}
		}
		brelse(bh_index);
		return 0;
	default:
		return -ENOTTY;
	}

	return 0;
}



const struct file_operations ouichefs_file_ops = {
	.owner = THIS_MODULE,
	.open = ouichefs_open,
	.llseek = generic_file_llseek,
	.read = ouichefs_read,
	.read_iter = generic_file_read_iter,
	.write = ouichefs_write,
	.write_iter = generic_file_write_iter,
	.unlocked_ioctl = ouichefs_ioctl
};
