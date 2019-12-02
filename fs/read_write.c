#include "testfs.h"
#include "list.h"
#include "super.h"
#include "block.h"
#include "inode.h"
#include "stdbool.h"
long max_blocks = 4194304;
unsigned long max_size = 34376597504;

bool readBlockExists(struct inode * in, long blockNum, char * block);
/* given logical block number, read the corresponding physical block into block.
 * return physical block number.
 * returns 0 if physical block does not exist.
 * returns negative value on other errors. */
static int
testfs_read_block(struct inode *in, int log_block_nr, char *block)
{
	int phy_block_nr = 0;
	
	assert(log_block_nr >= 0);
	if (log_block_nr < NR_DIRECT_BLOCKS) {
		phy_block_nr = (int)in->in.i_block_nr[log_block_nr];
	} else {
		log_block_nr -= NR_DIRECT_BLOCKS;
		if (log_block_nr >= NR_INDIRECT_BLOCKS){ ////////////////
			log_block_nr -= NR_INDIRECT_BLOCKS;
			if (log_block_nr >= max_blocks){
				return -EFBIG;
			}
			if (in->in.i_dindirect > 0){
				read_blocks(in->sb, block, in->in.i_dindirect, 1);
				//each address in the dindirect array points to an indirect block.
				long tempBlock = 0;
				tempBlock = ((int *)block)[log_block_nr / NR_INDIRECT_BLOCKS];
				if(tempBlock == 0){
					return 0;
				}
				read_blocks(in->sb, block, tempBlock, 1);
				phy_block_nr = ((int *)block)[log_block_nr % NR_INDIRECT_BLOCKS];
			}
		}	/////////////////
		else if (in->in.i_indirect > 0) {
			read_blocks(in->sb, block, in->in.i_indirect, 1);
			phy_block_nr = ((int *)block)[log_block_nr];
		}
	}
	if (phy_block_nr > 0) {
		read_blocks(in->sb, block, phy_block_nr, 1);
	} else {
		/* we support sparse files by zeroing out a block that is not
		 * allocated on disk. */
		bzero(block, BLOCK_SIZE);
	}
	return phy_block_nr;
}

bool readBlockExists(struct inode * in, long blockNum, char * block){
	int check = testfs_read_block(in, blockNum, block);
	if (check < 0){
		return false;
	}
	return true;
}

int
testfs_read_data(struct inode *in, char *buf, off_t start, size_t size)
{
	char block[BLOCK_SIZE];
	long block_nr = start / BLOCK_SIZE; // logical block number in the file
	long block_ix = start % BLOCK_SIZE; //  index or offset in the block
	int ret;

	assert(buf);
	if (start + (off_t) size > in->in.i_size) {
		size = in->in.i_size - start;
	}
	if (block_ix + size > BLOCK_SIZE) {///////////////
		if (size > max_size){	//too big, return error
			return -EFBIG;
		}
		long remainingSize = size;
        if (readBlockExists(in, block_nr, block) == false) {
			return 0;
		}
        memcpy(buf, block + block_ix, BLOCK_SIZE - block_ix);	//copy the top
        remainingSize -= BLOCK_SIZE - block_ix;
        int i = 1;
        while(remainingSize > BLOCK_SIZE){	//keep copying until all full blocks have been copied
			if (readBlockExists(in, block_nr + i, block) == true) {
				memcpy(buf + BLOCK_SIZE * i - block_ix, block, BLOCK_SIZE); 	//copy everything in the middle
				remainingSize -= BLOCK_SIZE;
				i++;
			}
			else {
				return 0;
			}
            
        }
        if (readBlockExists(in, block_nr + i, block) == false) {
			return 0;
		}
        memcpy(buf + BLOCK_SIZE * i - block_ix, block, remainingSize);		//copy the bottome
        return size;
	}//////////////////////////
	ret = testfs_read_block(in, block_nr, block);
	if (ret < 0){
		return ret;
	}
	memcpy(buf, block + block_ix, size);
	/* return the number of bytes read or any error */
	return size;
}

/* given logical block number, allocate a new physical block, if it does not
 * exist already, and return the physical block number that is allocated.
 * returns negative value on error. */
static int
testfs_allocate_block(struct inode *in, int log_block_nr, char *block)
{
	int phy_block_nr;
	char indirect[BLOCK_SIZE];
	int indirect_allocated = 0;

	assert(log_block_nr >= 0);
	phy_block_nr = testfs_read_block(in, log_block_nr, block);

	/* phy_block_nr > 0: block exists, so we don't need to allocate it, 
	   phy_block_nr < 0: some error */
	if (phy_block_nr != 0)
		return phy_block_nr;

	/* allocate a direct block */
	if (log_block_nr < NR_DIRECT_BLOCKS) {
		assert(in->in.i_block_nr[log_block_nr] == 0);
		phy_block_nr = testfs_alloc_block_for_inode(in);
		if (phy_block_nr >= 0) {
			in->in.i_block_nr[log_block_nr] = phy_block_nr;
		}
		return phy_block_nr;
	}

	log_block_nr -= NR_DIRECT_BLOCKS;
	if (log_block_nr >= NR_INDIRECT_BLOCKS){/////////////
		log_block_nr -= NR_INDIRECT_BLOCKS;
        if (log_block_nr >= max_blocks) {
            return -EFBIG;
        }
		char doubleIndirect[BLOCK_SIZE];
        bool doubleIndirectAllocated = false;
        //double indirect blocks
        if(in->in.i_dindirect == 0){
            bzero(doubleIndirect, BLOCK_SIZE);
            phy_block_nr = testfs_alloc_block_for_inode(in);
            if(phy_block_nr >= 0){
				doubleIndirectAllocated = true;
            	in->in.i_dindirect = phy_block_nr;
			}
			else {
				return phy_block_nr;
			}
        }
		else{
            read_blocks(in->sb, doubleIndirect, in->in.i_dindirect, 1);
        }
        //indirect block
        if(((int*) doubleIndirect)[log_block_nr / NR_INDIRECT_BLOCKS] == 0) {
            bzero(indirect, BLOCK_SIZE);
            phy_block_nr = testfs_alloc_block_for_inode(in);
			if (phy_block_nr >= 0){
				indirect_allocated = 1;
                ((int *) doubleIndirect)[log_block_nr / NR_INDIRECT_BLOCKS] = phy_block_nr;
                write_blocks(in->sb, doubleIndirect, in->in.i_dindirect, 1);
			}
            else if (doubleIndirectAllocated == true) {
				testfs_free_block_from_inode(in, in->in.i_dindirect);
				return phy_block_nr;
            }
        }
		else {
            read_blocks(in->sb, indirect, ((int*) doubleIndirect)[log_block_nr / NR_INDIRECT_BLOCKS], 1);
        }
        //direct block
        assert(((int *) indirect)[log_block_nr % NR_INDIRECT_BLOCKS] == 0);
        phy_block_nr = testfs_alloc_block_for_inode(in);
		if (phy_block_nr >= 0) {
            ((int *) indirect)[log_block_nr % NR_INDIRECT_BLOCKS] = phy_block_nr;
            write_blocks(in->sb, indirect, ((int *)doubleIndirect)[log_block_nr / NR_INDIRECT_BLOCKS], 1);
        } 
        else{
			testfs_free_block_from_inode(in, ((int *)doubleIndirect)[log_block_nr / NR_INDIRECT_BLOCKS]);
            if(doubleIndirectAllocated == true){
                testfs_free_block_from_inode(in, in->in.i_dindirect);
            }
            return phy_block_nr;
        }
        return phy_block_nr;
	}//////////////////
	
	if (in->in.i_indirect == 0) {	/* allocate an indirect block */
		bzero(indirect, BLOCK_SIZE);
		phy_block_nr = testfs_alloc_block_for_inode(in);
		if (phy_block_nr < 0)
			return phy_block_nr;
		indirect_allocated = 1;
		in->in.i_indirect = phy_block_nr;
	} else {	/* read indirect block */
		read_blocks(in->sb, indirect, in->in.i_indirect, 1);
	}

	/* allocate direct block */
	assert(((int *)indirect)[log_block_nr] == 0);	
	phy_block_nr = testfs_alloc_block_for_inode(in);

	if (phy_block_nr >= 0) {
		/* update indirect block */
		((int *)indirect)[log_block_nr] = phy_block_nr;
		write_blocks(in->sb, indirect, in->in.i_indirect, 1);
	} else if (indirect_allocated) {
		/* there was an error while allocating the direct block, 
		 * free the indirect block that was previously allocated */
		testfs_free_block_from_inode(in, in->in.i_indirect);
		in->in.i_indirect = 0;
	}
	return phy_block_nr;
}

int
testfs_write_data(struct inode *in, const char *buf, off_t start, size_t size)
{
	char block[BLOCK_SIZE];
	long block_nr = start / BLOCK_SIZE; // logical block number in the file
	long block_ix = start % BLOCK_SIZE; //  index or offset in the block
	int ret;

	if (block_ix + size > BLOCK_SIZE) {/////////////////
		if (size > max_size){
			return -EFBIG;
		}
		long remainingSize = size;
		ret = testfs_allocate_block(in, block_nr, block);
		if (ret < 0) {
            return ret;
		}
        memcpy(block + block_ix, buf, BLOCK_SIZE - block_ix);
        write_blocks(in->sb, block, ret, 1);
        remainingSize -= BLOCK_SIZE - block_ix;
        int i = 1;
        while(remainingSize > BLOCK_SIZE){
			ret = testfs_allocate_block(in, block_nr + i, block);
            if (ret >= 0) {
                memcpy(block, buf + BLOCK_SIZE * i - block_ix, BLOCK_SIZE);
				write_blocks(in->sb, block, ret, 1);
				remainingSize -= BLOCK_SIZE;
				i++;
			}
			else {
				return ret;
			}
        }
		ret = testfs_allocate_block(in, block_nr + i, block);
        if (ret < 0) {
            in->in.i_size = max_size;
			// in->i_flags |= I_FLAGS_DIRTY;
            return ret;
        }
        memcpy(block, buf + BLOCK_SIZE * i - block_ix, remainingSize);
        write_blocks(in->sb, block, ret, 1);
        if (size > 0){
            in->in.i_size = MAX(in->in.i_size, start + (off_t) size);
		}
        in->i_flags |= I_FLAGS_DIRTY;
        return size;
	}//////////////
	/* ret is the newly allocated physical block number */
	ret = testfs_allocate_block(in, block_nr, block);
	if (ret < 0)
		return ret;
	memcpy(block + block_ix, buf, size);
	write_blocks(in->sb, block, ret, 1);
	/* increment i_size by the number of bytes written. */
	if (size > 0)
		in->in.i_size = MAX(in->in.i_size, start + (off_t) size);
	in->i_flags |= I_FLAGS_DIRTY;
	/* return the number of bytes written or any error */
	return size;
}

int
testfs_free_blocks(struct inode *in)
{
	int i;
	int e_block_nr;

	/* last logical block number */
	e_block_nr = DIVROUNDUP(in->in.i_size, BLOCK_SIZE);

	/* remove direct blocks */
	for (i = 0; i < e_block_nr && i < NR_DIRECT_BLOCKS; i++) {
		if (in->in.i_block_nr[i] == 0)
			continue;
		testfs_free_block_from_inode(in, in->in.i_block_nr[i]);
		in->in.i_block_nr[i] = 0;
	}
	e_block_nr -= NR_DIRECT_BLOCKS;

	/* remove indirect blocks */
	if (in->in.i_indirect > 0) {
		char block[BLOCK_SIZE];
		assert(e_block_nr > 0);
		read_blocks(in->sb, block, in->in.i_indirect, 1);
		for (i = 0; i < e_block_nr && i < NR_INDIRECT_BLOCKS; i++) {
			if (((int *)block)[i] == 0)
				continue;
			testfs_free_block_from_inode(in, ((int *)block)[i]);
			((int *)block)[i] = 0;
		}
		testfs_free_block_from_inode(in, in->in.i_indirect);
		in->in.i_indirect = 0;
	}

	e_block_nr -= NR_INDIRECT_BLOCKS;
	/* handle double indirect blocks */
	 if (e_block_nr >= 0) {
        int deleteIterator = 0;
        char doubleBlock[BLOCK_SIZE];
		char singleBlock[BLOCK_SIZE];
        read_blocks(in->sb, doubleBlock, in->in.i_dindirect, 1);
        for(i = 0; deleteIterator < e_block_nr && i < NR_INDIRECT_BLOCKS; i++){
			if (((int *)doubleBlock)[i] == 0){
				deleteIterator += NR_INDIRECT_BLOCKS;
				continue;
			}	
			read_blocks(in->sb, singleBlock, ((int *)doubleBlock)[i], 1);
			for(int j = 0; deleteIterator < e_block_nr && j < NR_INDIRECT_BLOCKS; j++){
				deleteIterator++;
				if (((int *)singleBlock)[j] == 0){
					continue;
				}
				testfs_free_block_from_inode(in, ((int *) singleBlock)[j]);
				((int *) singleBlock)[j] = 0;
			}
			testfs_free_block_from_inode(in, ((int *) doubleBlock)[i]);
			((int *) doubleBlock)[i] = 0;
        }
        testfs_free_block_from_inode(in, in->in.i_dindirect);
        in->in.i_dindirect = 0;
    }

	in->in.i_size = 0;
	in->i_flags |= I_FLAGS_DIRTY;
	return 0;
}
