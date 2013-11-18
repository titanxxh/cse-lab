#include "inode_manager.h"
#include "gettime.h"

// disk layer -----------------------------------------

disk::disk()
{
  bzero(blocks, sizeof(blocks));
}

void
disk::read_block(blockid_t id, char *buf)
{
  if (id < 0 || id >= BLOCK_NUM || buf == NULL)
    return;

  memcpy(buf, blocks[id], BLOCK_SIZE);
}

void
disk::write_block(blockid_t id, const char *buf)
{
  if (id < 0 || id >= BLOCK_NUM || buf == NULL)
    return;

  memcpy(blocks[id], buf, BLOCK_SIZE);
}

// block layer -----------------------------------------

bool
block_manager::check_blk_bit(uint32_t pos)
{
	return (blk_bitmap[pos / (sizeof(unsigned long) * 8)] >> (pos % (sizeof(unsigned long) * 8))) & 1;
}

// Allocate a free disk block.
blockid_t
block_manager::alloc_block()
{
	for (int i = BLOCK_NUM - 1; i >= 0; i--)
	{
		if (!check_blk_bit(i))
		{
			set_blk_bitmap(i, 1);
			return i;
		}
	}
  return 0;
}

void
block_manager::free_block(uint32_t id)
{
	set_blk_bitmap(id, 0); 
  return;
}

void
block_manager::set_blk_bitmap(uint32_t pos, bool b)
{
  if (b)
	{
		blk_bitmap[pos / (sizeof(unsigned long) * 8)] |=
			1 << (pos % (sizeof(unsigned long) * 8));
		rest_blocks --;
	}
	else
	{
		blk_bitmap[pos / (sizeof(unsigned long) * 8)] &=
			~(1 << (pos % (sizeof(unsigned long) * 8)));
		rest_blocks ++;
	}
}

// The layout of disk should be like this:
// |<-sb->|<-free block bitmap->|<-inode table->|<-data->|
block_manager::block_manager()
{
  d = new disk();

  // format the disk
  sb.size = BLOCK_SIZE * BLOCK_NUM;
  sb.nblocks = BLOCK_NUM;
  sb.ninodes = INODE_NUM;
	int used = 3 + sb.nblocks / BPB + INODE_NUM /  IPB;
	rest_blocks = BLOCK_NUM - used;
  bzero(blk_bitmap, sizeof(blk_bitmap));
	for (int i = 0; i < used; i++)
		set_blk_bitmap(i, 1);
}

void
block_manager::read_block(uint32_t id, char *buf)
{
  d->read_block(id, buf);
}

void
block_manager::write_block(uint32_t id, const char *buf)
{
  d->write_block(id, buf);
}

// inode layer -----------------------------------------

inode_manager::inode_manager()
{
  inode_csr = 0;
	inode_used = 0;
  bm = new block_manager();
  bzero(inode_bitmap, sizeof(inode_bitmap));
  uint32_t root_dir = alloc_inode(extent_protocol::T_DIR);
  if (root_dir != 1) {
    printf("\tim: error! alloc first inode %d, should be 1\n", root_dir);
    exit(0);
  }
}

bool
inode_manager::check_inode_bit(uint32_t pos)
{
	return (inode_bitmap[pos / (sizeof(long) * 8)] >> (pos % (sizeof(long) * 8))) & 1;
}

void
inode_manager::set_inode_bitmap(uint32_t pos, bool b)
{
  if (b)
	{
		inode_bitmap[pos / (sizeof(long) * 8)] |=
			1 << (pos % (sizeof(long) * 8));
		inode_used ++;
	}
	else
	{
		inode_bitmap[pos / (sizeof(long) * 8)] &=
			~(1 << (pos % (sizeof(long) * 8)));
		inode_used --;
	}
}


/* Create a new file.
 * Return its inum. */
uint32_t
inode_manager::alloc_inode(uint32_t type)
{
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts))
    return -1;
  struct inode *new_node = new inode();
  new_node->type = type;
  new_node->atime = new_node->ctime = new_node->mtime = ts.tv_nsec;
  new_node->size = 0;
	while (true)
	{
  	inode_csr ++;
		if (inode_csr % INODE_NUM == 0)
			inode_csr = 2;
		if (!check_inode_bit(inode_csr))
		{
			set_inode_bitmap(inode_csr, true);
			break;
		}
	}
  put_inode(inode_csr, new_node);
	//printf("inm: alloc inode %d\n", inode_csr);
  return inode_csr;
}

void
inode_manager::free_inode(uint32_t inum)
{
	struct inode *in = get_inode(inum);
  if (in == NULL) return;
	int it = in->isize <= NDIRECT ? in->isize : NDIRECT;
	for (int i = 0; i < it; i++)
	{
		bm->free_block(in->blocks[i]);
	}
	if (in->isize >= NDIRECT)
		free_inode(in->blocks[NDIRECT]);
	//bm->rest_blocks += it;
	memset(in, 0, sizeof(struct inode));
	put_inode(inum, in);
	delete in;
	set_inode_bitmap(inum, false);
	//printf("inm: free inode %d\n", inum);
  return;
}


/* Return an inode structure by inum, NULL otherwise.
 * Caller should release the memory. */
struct inode* 
inode_manager::get_inode(uint32_t inum)
{
  struct inode *ino, *ino_disk;
  char buf[BLOCK_SIZE];

  //printf("\tim: get_inode %d\n", inum);

  if (inum < 0 || inum >= INODE_NUM) {
    //printf("\tim: inum out of range\n");
    return NULL;
  }

  bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf); 
  // printf("%s:%d\n", __FILE__, __LINE__);

  ino_disk = (struct inode*)buf + inum%IPB;
  if (ino_disk->type == 0) {
    //printf("\tim: inode not exist\n");
    return NULL;
  }

  ino = (struct inode*)malloc(sizeof(struct inode));
  *ino = *ino_disk;

  return ino;
}

void
inode_manager::put_inode(uint32_t inum, struct inode *ino)
{
  char buf[BLOCK_SIZE];
  struct inode *ino_disk;

  //printf("\tim: put_inode %d\n", inum);
  if (ino == NULL)
    return;

  bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
  ino_disk = (struct inode*)buf + inum%IPB;
  *ino_disk = *ino;
  bm->write_block(IBLOCK(inum, bm->sb.nblocks), buf);
}

#define MIN(a,b) ((a)<(b) ? (a) : (b))

/* Get all the data of a file by inum. 
 * Return alloced data, should be freed by caller. */
void
inode_manager::read_file(uint32_t inum, char **buf_out, int *size)
{
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts))
    return;
	struct inode *in = get_inode(inum);
  in->atime = ts.tv_nsec;
	*size = in->size;
	*buf_out = new char[(*size / BLOCK_SIZE + 1) * BLOCK_SIZE];
	char *buf = *buf_out;
	while (true)
	{
		int it = in->isize <= NDIRECT ? in->isize : NDIRECT;
		for (int i = 0; i < it; i++)
		{
			bm->read_block(in->blocks[i], buf + i * BLOCK_SIZE);
		}
		if (in->isize <= NDIRECT)
		{
			put_inode(inum, in);
			delete in;
			break;
		}
		put_inode(inum, in);
		inum = in->blocks[NDIRECT];
		delete in;
		in = get_inode(inum);
		buf += NDIRECT * BLOCK_SIZE;
	}
	//printf("im size read %d\n", *size);
  return;
}

/* alloc/free blocks if needed */
void
inode_manager::write_file(uint32_t inum, const char *buf, int size)
{
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts))
    return;
	struct inode *in = get_inode(inum);
	//printf("!!xxh write_file inum %d type %d\n", inum, in->type);
	in->mtime = ts.tv_nsec;
	if (in->type == extent_protocol::T_DIR)
	{
		//printf("!!xxh fuckj %d %d\n", in->ctime, ts.tv_nsec);
		in->ctime = ts.tv_nsec;
	}
  if (in)
  {  
		if (in->isize > NDIRECT)
			free_inode(in->blocks[NDIRECT]);
		int it = in->isize <= NDIRECT ? in->isize : NDIRECT;
		for (int i = 0; i < it; i++)
		{
			bm->free_block(in->blocks[i]);
		}
		int bn2 = (size % BLOCK_SIZE == 0) ?
					size / BLOCK_SIZE : size / BLOCK_SIZE + 1;
		if (bm->rest_blocks < bn2) return;
		in->size = size;
		int bn = bn2 > NDIRECT ? NDIRECT : bn2;
		for (int i = 0; i < bn; i++)
		{
			int b = bm->alloc_block();
			in->blocks[i] = b;
			bm->write_block(b, buf + i * BLOCK_SIZE);
		}
		in->isize = bn;
		//bm->rest_blocks -= bn;
		if (bn2 - bn > 0)
		{
			in->isize = NDIRECT + 1;
			in->blocks[NDIRECT] = alloc_inode(in->type);
			write_file(in->blocks[NDIRECT],
								 buf + NDIRECT * BLOCK_SIZE,
								 size - NDIRECT * BLOCK_SIZE);
		}
		//printf("!!xxh im size write %d\n", size);
  } 
	put_inode(inum, in);
	delete in;
	printf("!!xxh write_file done inum %d used %ld restblk %d\n", inum, inode_used, bm->rest_blocks);
  return;
}

void
inode_manager::getattr(uint32_t inum, extent_protocol::attr &a)
{
  struct inode *i = get_inode(inum);
	if (i == NULL) return;
  a.type = i->type;
  a.size = i->size;
  a.atime = i->atime;
  a.mtime = i->mtime;
	a.ctime = i->ctime;
	//printf("!!xxh getattr a%d %d\n", a.mtime, a.ctime);
  delete i;
  return;
}

void
inode_manager::remove_file(uint32_t inum)
{
	//printf("!!xxh remove inum %d\n", inum);
	free_inode(inum); 
  return;
}
