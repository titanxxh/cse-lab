// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client_cache.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
  ec = new extent_client_cache(extent_dst);
	lu = new lock_release_flush(ec);
  lc = new lock_client_cache(lock_dst, lu);
	lc->acquire(1);
	printf("!!xxh yfs: con pid %d\n", getpid());
  if (ec->put(1, "") != extent_protocol::OK)
      printf("error init root dir\n"); // XYB: init root dir
	lc->release(1);
}


yfs_client::inum
yfs_client::n2i(std::string n)
{
    std::istringstream ist(n);
    unsigned long long finum;
    ist >> finum;
    return finum;
}

std::string
yfs_client::filename(inum inum)
{
    std::ostringstream ost;
    ost << inum;
    return ost.str();
}

bool
yfs_client::isfile(inum inum)
{
	lc->acquire(inum);
	bool r = _isfile(inum);
	lc->release(inum);
	return r;
}

bool
yfs_client::_isfile(inum inum)
{
    extent_protocol::attr a;
		bool r;
		
		printf("!!xxh yfs: isfile\n");
    if (ec->getattr(inum, a) != extent_protocol::OK) {
        printf("error getting attr\n");
        r = false;
				goto out;
    }

    if (a.type == extent_protocol::T_FILE) {
        printf("!!xxh yfs: isfile: %lld is a file\n", inum);
        r = true;
				goto out;
    } 
    printf("!!xxh yfs: isfile: %lld is a dir\n", inum);
		r = false;
out:
    return r;
}

bool
yfs_client::isdir(inum inum)
{
	lc->acquire(inum);
	bool r = _isdir(inum);
	lc->release(inum);
	return r;
}

bool
yfs_client::_isdir(inum inum)
{
    return !_isfile(inum);
}

int
yfs_client::getfile(inum inum, fileinfo &fin)
{
	lc->acquire(inum);
	int r = _getfile(inum, fin);
	lc->release(inum);
	return r;
}

int
yfs_client::_getfile(inum inum, fileinfo &fin)
{
    int r = OK;

    extent_protocol::attr a;
    if (ec->getattr(inum, a) != extent_protocol::OK) {
        r = IOERR;
        goto out;
    }

    fin.atime = a.atime;
    fin.mtime = a.mtime;
    fin.ctime = a.ctime;
    fin.size = a.size;
    printf("!!xxh yfs: getfile %llu -> sz %llu\n", inum, fin.size);

out:
    return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
	lc->acquire(inum);
	int r = _getdir(inum, din);
	lc->release(inum);
	return r;
}

int
yfs_client::_getdir(inum inum, dirinfo &din)
{
    int r = OK;
    
    printf("!!xxh yfs: getdir %llu\n", inum);
    extent_protocol::attr a;
    if (ec->getattr(inum, a) != extent_protocol::OK) {
        r = IOERR;
        goto out;
    }
    din.atime = a.atime;
    din.mtime = a.mtime;
    din.ctime = a.ctime;

out:
    return r;
}


#define EXT_RPC(xx) do { \
    if ((xx) != extent_protocol::OK) { \
        printf("EXT_RPC Error: %s:%d \n", __FILE__, __LINE__); \
        r = IOERR; \
        goto out; \
    } \
} while (0)


int
yfs_client::setattr(inum ino, size_t size)
{
	lc->acquire(ino);
	int r = _setattr(ino, size);
	lc->release(ino);
	return r;
}

// Only support set size of attr
int
yfs_client::_setattr(inum ino, size_t size)
{
    int r = OK;

    /*
     * your lab2 code goes here.
     * note: get the content of inode ino, and modify its content
     * according to the size (<, =, or >) content length.
     */
		std::string data;
		fileinfo fin;
		if (_getfile(ino, fin))
		{
        r = IOERR;
        goto out;
    }
		if (_read(ino, fin.size, 0, data) != OK)
		{
        r = IOERR;
        goto out;
    }
		printf("!!xxh yfs: setattr\n");
		data = data.substr(0, size);
		if (ec->put(ino, data) != extent_protocol::OK)
		{
        r = IOERR;
        goto out;
    }
out:
    return r;
}

int
yfs_client::create(inum parent, const char *name, mode_t mode, inum &ino_out, bool isdir)
{
	lc->acquire(parent);
	int r = _create(parent, name, mode, ino_out, isdir);
	lc->release(parent);
	return r;
}

int
yfs_client::_create(inum parent, const char *name, mode_t mode, inum &ino_out, bool isdir)
{
    int r = OK;

    /*
     * your lab2 code goes here.
     * note: lookup is what you need to check if file exist;
     * after create file or dir, you must remember to modify the parent infomation.
     */
		printf("!!xxh yfs: create %s pid %d\n", name, getpid());
		bool found;
		std::string data;
		fileinfo fin;
		size_t bw;
		if (_lookup(parent, name, found, ino_out) != OK)
		{
        r = IOERR;
        goto out;
    }
		if (found)
		{
        r = EXIST;
        goto out;
    }
		if (isdir)
		{
				if (ec->create(extent_protocol::T_DIR, ino_out) != extent_protocol::OK)
				{
						r = IOERR;
        		goto out;
				}
		}
		else
		{
				if (ec->create(extent_protocol::T_FILE, ino_out) != extent_protocol::OK)
				{
						r = IOERR;
        		goto out;
				}
		}
		if (_getfile(parent, fin))
		{
        r = IOERR;
        goto out;
    }
		if (_read(parent, fin.size, 0, data) != OK)
		{
        r = IOERR;
        goto out;
    }
		data.append(" ");
		data.append(std::string(name));
		data.append(" ");
		data.append(filename(ino_out));
		//printf("!!xxh create data %s\n", data.c_str());
		if (_setattr(parent, 0) != OK)
		{
        r = IOERR;
        goto out;
    }
		if (_write(parent, data.size(), 0, data.c_str(), bw) != OK)
		{
        r = IOERR;
        goto out;
    }
	
out:
		printf("!!xxh yfs: create %llu done pid %d\n", ino_out, getpid());
    return r;
}

int
yfs_client::lookup(inum parent, const char *name, bool &found, inum &ino_out)
{
	lc->acquire(parent);
	int r = _lookup(parent, name, found, ino_out);
	lc->release(parent);
	return r;
}

int
yfs_client::_lookup(inum parent, const char *name, bool &found, inum &ino_out)
{
    int r = OK;

    /*
     * your lab2 code goes here.
     * note: lookup file from parent dir according to name;
     * you should design the format of directory content.
     */
		std::list<dirent> filelist;
		std::list<dirent>::iterator it;
		if (_readdir(parent, filelist) != OK)
		{
        r = IOERR;
        goto out;
    }
		found = false;
		printf("!!xxh lookup parent %llu name %s listsize %d\n", parent, name, filelist.size());
		for (it = filelist.begin();it != filelist.end(); it++)
		{
				printf("!!xxh lookup %s\n", it->name.c_str());
				if (it->name == std::string(name))
				{
						found = true;
						ino_out = it->inum;
						//printf("!!!xxh found!!! %s in %d\n", name, parent);
						break;
				}
		}
out:
		printf("!!xxh lookup parent %llu name %s found %d\n", parent, name, found);
    return r;
}

int
yfs_client::readdir(inum dir, std::list<dirent> &list)
{
	lc->acquire(dir);
	int r = _readdir(dir, list);
	lc->release(dir);
	return r;
}

int
yfs_client::_readdir(inum dir, std::list<dirent> &list)
{
    int r = OK;

    /*
     * your lab2 code goes here.
     * note: you should parse the dirctory content using your defined format,
     * and push the dirents to the list.
     */
		std::string data;
		fileinfo fin;
    std::istringstream iss;
		if (_getfile(dir, fin))
		{
        r = IOERR;
        goto out;
    }
		if (_read(dir, fin.size, 0, data) != OK)
		{
        r = IOERR;
        goto out;
    }

		iss.str(data);
		printf("!!xxh readdir data %s\n", data.c_str());
		while (true)
		{
				dirent d;
				iss >> d.name >> d.inum;
				if (!iss) break;
				printf("!!xxh readdir name %s inum %llu\n", d.name.c_str(), d.inum);
				list.push_back(d);
		}
out:
    return r;
}

int
yfs_client::read(inum ino, size_t size, off_t off, std::string &data)
{
	lc->acquire(ino);
	int r = _read(ino, size, off, data);
	lc->release(ino);
	return r;
}

int
yfs_client::_read(inum ino, size_t size, off_t off, std::string &data)
{
    int r = OK;

    /*
     * your lab2 code goes here.
     * note: read using ec->get().
     */
		if (ec->get(ino, data) != extent_protocol::OK)
		{
        r = IOERR;
        goto out;
    }
		data = data.substr(off, size);
out:
    return r;
}

int
yfs_client::write(inum ino, size_t size, off_t off, const char *data,
        size_t &bytes_written)
{
	lc->acquire(ino);
	int r = _write(ino, size, off, data, bytes_written);
	lc->release(ino);
	return r;
}

int
yfs_client::_write(inum ino, size_t size, off_t off, const char *data,
        size_t &bytes_written)
{
    int r = OK;

    /*
     * your lab2 code goes here.
     * note: write using ec->put().
     * when off > length of original file, fill the holes with '\0'.
     */
		std::string oridata;
		std::string newdata(data, size);
		if (ec->get(ino, oridata) != extent_protocol::OK)
		{
        r = IOERR;
        goto out;
    }
		if (off > oridata.size())
		{
				int start = oridata.size();
				oridata = std::string(oridata.c_str(), off + size);
				for (int i = start; i < off; i++)
						oridata[i] = '\0';
		}
		oridata = oridata.replace(off, newdata.size(), newdata);
		if (ec->put(ino, oridata) != extent_protocol::OK)
		{
        r = IOERR;
        goto out;
    }
		bytes_written = size;

out:
    return r;
}

int yfs_client::unlink(inum parent,const char *name)
{
	lc->acquire(parent);
	int r = _unlink(parent, name);
	lc->release(parent);
	return r;
}

int yfs_client::_unlink(inum parent,const char *name)
{
    int r = OK;

    /*
     * your lab2 code goes here.
     * note: you should remove the file using ec->remove,
     * and update the parent directory content.
     */
		std::string data = "";
		std::string temp = "";
		std::list<dirent> filelist;
		std::list<dirent>::iterator it;
		bool found = false;
		printf("!!xxh unlink1 data %s\n", data.c_str());
		if (_readdir(parent, filelist) != OK)
		{
        r = IOERR;
        goto out;
    }
		printf("!!xxh unlink2 data %s\n", data.c_str());
		for (it = filelist.begin();it != filelist.end(); it++)
		{
				if (it->name == std::string(name))
				{
						found = true;
						lc->acquire(it->inum);
						if (ec->remove(it->inum) != extent_protocol::OK)
						{
								lc->release(it->inum);
    				    r = IOERR;
        				goto out;
				    }
						lc->release(it->inum);
				}
				else
				{
						temp = "";
						temp.append(" ");
						temp.append(it->name);
						temp.append(" ");
						temp.append(filename(it->inum));
						data.append(temp);
				}
		}
		if (!found)
		{
        r = IOERR;
        goto out;
    }
		printf("!!xxh unlink data %s\n", data.c_str());
		size_t bw;
		if (_setattr(parent, 0) != OK)
		{
        r = IOERR;
        goto out;
    }
		if (_write(parent, data.size(), 0, data.c_str(), bw) != OK)
		{
        r = IOERR;
        goto out;
    }
out:
    return r;
}

