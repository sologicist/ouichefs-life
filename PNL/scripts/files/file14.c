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

	/* Get the block number for the current iblock */

	int bno = index->blocks[iblock];
	int bnum = (bno & BLOCK_NUMBER_MASK);

	if (bno == 0) {
		brelse(bh_index);
		return -EIO;
	}

	struct buffer_head *bh = sb_bread(sb, bnum);

	if (!bh) {
		brelse(bh_index);
		return -EIO;
	}

	char *buffer = bh->b_data;

	/* get data from the buffer from the current position */
	buffer += *pos % OUICHEFS_BLOCK_SIZE;

	if (bh->b_size < count)
		to_be_copied = bh->b_size;
	else
		to_be_copied = count;

	copied_to_user = to_be_copied - copy_to_user(buf, buffer, to_be_copied);

	*pos += copied_to_user;
	file->f_pos = *pos;

	brelse(bh);
	brelse(bh_index);

	return copied_to_user;
}

static ssize_t ouichefs_write(struct file *filep,
		const char __user *buf, size_t len, loff_t *ppos)
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
	size_t offset;
	size_t remaining;

	if (filep->f_flags & O_APPEND)
		*ppos = inode->i_size;

	if (*ppos >= OUICHEFS_MAX_FILESIZE)
		return -EFBIG;

	bh_index = sb_bread(sb, ii->index_block);
	index = (struct ouichefs_file_index_block *)bh_index->b_data;
	iblock = *ppos / OUICHEFS_BLOCK_SIZE;

	if (!bh_index)
		return -EIO;

	/* Vérifier entre dernier bloc alloué et iblock
	si des blocs sont alloués, sinon les allouer */
	for (int i = 0; i < iblock; i++) {
		if (index->blocks[i] == 0) {
			/* Allouer un nouveau bloc */
			bno = get_free_block(OUICHEFS_SB(sb));
			if (!bno) {
				brelse(bh_index);
				return -ENOSPC;
			}
			inode->i_blocks++;
			index->blocks[i] = bno;
			mark_buffer_dirty(bh_index);
			sync_dirty_buffer(bh_index);
		} else {
			bno = index->blocks[iblock];
		}
	}

	while (len > 0) {
		bh_index = sb_bread(sb, ii->index_block);
		index = (struct ouichefs_file_index_block *)bh_index->b_data;
		iblock = *ppos / OUICHEFS_BLOCK_SIZE;
		/* Vérifier si le bloc est déjà alloué */
		if (index->blocks[iblock] == 0) {
			/* Allouer un nouveau bloc */
			bno = get_free_block(OUICHEFS_SB(sb));
			bno = (0 << 20) | (bno & BLOCK_NUMBER_MASK);
			if (!bno) {
				brelse(bh_index);
				return -ENOSPC;
			}
			inode->i_blocks++;
			index->blocks[iblock] = bno;
			mark_buffer_dirty(bh_index);
			sync_dirty_buffer(bh_index);
		} else {
			bno = index->blocks[iblock];
		}

		/* Lire ou initialiser le bloc de données */
		bh = sb_bread(sb, bno);
		if (!bh) {
			brelse(bh_index);
			return -EIO;
		}
		buffer = bh->b_data;

		/* Calculer la quantité de données
		 * à écrire dans ce bloc */
		offset = *ppos % OUICHEFS_BLOCK_SIZE;
		remaining = OUICHEFS_BLOCK_SIZE - offset;
		to_write = min(len, remaining);

		/* Copier les données de l'utilisateur
		 * dans le bloc de données */
		if (copy_from_user(buffer + offset, buf, to_write)) {
			brelse(bh);
			brelse(bh_index);
			return -EFAULT;
		}

		mark_buffer_dirty(bh);
		sync_dirty_buffer(bh);
		brelse(bh);

		uint32_t bnum20 = (bno & BLOCK_NUMBER_MASK);
		uint32_t bsize12 = (bno & BLOCK_SIZE_MASK) >> 20;

		bsize12 += written;
		index->blocks[iblock] = (bsize12 << 20)
			| (bnum20 & BLOCK_NUMBER_MASK);

		mark_buffer_dirty(bh_index);
		sync_dirty_buffer(bh_index);
		brelse(bh_index);

		brelse(bh_index);

		*ppos += to_write;
		buf += to_write;
		len -= to_write;
		written += to_write;

		/* Mettre à jour la taille du fichier si nécessaire */
		if (*ppos > inode->i_size) {
			inode->i_size = *ppos;
			mark_inode_dirty(inode);
		}
	}

	return written;
}

static long ouichefs_ioctl(struct file *file,
	unsigned int cmd, unsigned long arg)
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
	int internal_frag = 0;

	bh_index = sb_bread(sb, ii->index_block);
	struct ouichefs_file_index_block *index =
		(struct ouichefs_file_index_block *)bh_index->b_data;

	if (index == NULL) {
		pr_err("Failed to read index block\n");
		return -EIO;
	}

	used_blocks = inode->i_blocks;

	for (int i = 0; i < inode->i_blocks; i++) {
		uint32_t sizeb = (index->blocks[i] >> 20);

		if (sizeb < OUICHEFS_BLOCK_SIZE) {
			partial_blocks++;
			internal_frag += (OUICHEFS_BLOCK_SIZE - sizeb);
		}
	}
	brelse(bh_index);
	char ret[64];

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
		snprintf(ret, sizeof(internal_frag), "%d", internal_frag);
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
			uint32_t bn = (index->blocks[i] & 0x000FFFFF);

			if (bn == 0)
				continue;

			struct buffer_head *bh = sb_bread(sb, bn);
			uint32_t size = (index->blocks[i] & BLOCK_SIZE_MASK)
				>> 20;

			pr_info("block number : %d  size : %d\n", bn, size);
			brelse(bh);
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
