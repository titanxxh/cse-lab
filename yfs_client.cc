// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
  ec = new extent_client(extent_dst);
  lc = new lock_client(lock_dst);
  if (ec->put(1, "") != extent_protocol::OK)
      printf("error init root dir\n"); // XYB: init root dir
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
    extent_protocol::attr a;

    if (ec->getattr(inum, a) != extent_protocol::OK) {
        printf("error getting attr\n");
        return false;
    }

    if (a.type == extent_protocol::T_FILE) {
        printf("isfile: %lld is a file\n", inum);
        return true;
    } 
    printf("isfile: %lld is a dir\n", inum);
    return false;
}

bool
yfs_client::isdir(inum inum)
{
    return ! isfile(inum);
}

int
yfs_client::getfile(inum inum, fileinfo &fin)
{
    int r = OK;

    printf("getfile %016llx\n", inum);
    extent_protocol::attr a;
    if (ec->getattr(inum, a) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }

    fin.atime = a.atime;
    fin.mtime = a.mtime;
    fin.ctime = a.ctime;
    fin.size = a.size;
    printf("getfile %016llx -> sz %llu\n", inum, fin.size);

release:
    return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
    int r = OK;

    printf("getdir %016llx\n", inum);
    extent_protocol::attr a;
    if (ec->getattr(inum, a) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }
    din.atime = a.atime;
    din.mtime = a.mtime;
    din.ctime = a.ctime;

release:
    return r;
}


#define EXT_RPC(xx) do { \
    if ((xx) != extent_protocol::OK) { \
        printf("EXT_RPC Error: %s:%d \n", __FILE__, __LINE__); \
        r = IOERR; \
        goto release; \
    } \
} while (0)

// Only support set size of attr
int
yfs_client::setattr(inum ino, size_t size)
{
    int r = OK;

    /*
     * your lab2 code goes here.
     * note: get the content of inode ino, and modify its content
     * according to the size (<, =, or >) content length.
     */
		std::string data;
		fileinfo fin;
		if (getfile(ino, fin))
				return IOERR;
		if (read(ino, fin.size, 0, data) != OK)
				return IOERR;
		data = data.substr(0, size);
		if (ec->put(ino, data) != extent_protocol::OK)
				return IOERR;
    return r;
}

int
yfs_client::create(inum parent, const char *name, mode_t mode, inum &ino_out, bool isdir)
{
    int r = OK;

    /*
     * your lab2 code goes here.
     * note: lookup is what you need to check if file exist;
     * after create file or dir, you must remember to modify the parent infomation.
     */
		bool found;
		if (lookup(parent, name, found, ino_out) != OK)
				return IOERR;
		if (found)
			return EXIST;
		if (isdir)
		{
				if (ec->create(extent_protocol::T_DIR, ino_out) != extent_protocol::OK)
						return IOERR;
		}
		else
		{
				if (ec->create(extent_protocol::T_FILE, ino_out) != extent_protocol::OK)
						return IOERR;
		}
		std::string data;
		fileinfo fin;
		if (getfile(parent, fin))
				return IOERR;
		if (read(parent, fin.size, 0, data) != OK)
				return IOERR;
		data.append(" ");
		data.append(std::string(name));
		data.append(" ");
		data.append(filename(ino_out));
		//printf("!!xxh create data %s\n", data.c_str());
		size_t bw;
		if (setattr(parent, 0) != OK)
				return IOERR;
		if (write(parent, data.size(), 0, data.c_str(), bw) != OK)
				return IOERR;
	
    return r;
}

int
yfs_client::lookup(inum parent, const char *name, bool &found, inum &ino_out)
{
    int r = OK;

    /*
     * your lab2 code goes here.
     * note: lookup file from parent dir according to name;
     * you should design the format of directory content.
     */
		std::list<dirent> filelist;
		if (readdir(parent, filelist) != OK)
				return IOERR;
		std::list<dirent>::iterator it;
		found = false;
		//printf("!!xxh lookup name %s listsize %d\n", name, filelist.size());
		for (it = filelist.begin();it != filelist.end(); it++)
		{
				if (it->name == std::string(name))
				{
						found = true;
						ino_out = it->inum;
						//printf("!!!xxh found!!! %s in %d\n", name, parent);
						break;
				}
		}
    return r;
}

int
yfs_client::readdir(inum dir, std::list<dirent> &list)
{
    int r = OK;

    /*
     * your lab2 code goes here.
     * note: you should parse the dirctory content using your defined format,
     * and push the dirents to the list.
     */
		std::string data;
		fileinfo fin;
		if (getfile(dir, fin))
				return IOERR;
		if (read(dir, fin.size, 0, data) != OK)
				return IOERR;

    std::istringstream iss(data);
		//printf("!!xxh readdir data %s\n", data.c_str());
		while (true)
		{
				dirent d;
				iss >> d.name >> d.inum;
				if (!iss) break;
				//printf("!!xxh readdir name %s inum %d\n", d.name.c_str(), d.inum);
				list.push_back(d);
		}
    return r;
}

int
yfs_client::read(inum ino, size_t size, off_t off, std::string &data)
{
    int r = OK;

    /*
     * your lab2 code goes here.
     * note: read using ec->get().
     */
		if (ec->get(ino, data) != extent_protocol::OK)
				return IOERR;
		data = data.substr(off, size);
    return r;
}

int
yfs_client::write(inum ino, size_t size, off_t off, const char *data,
        size_t &bytes_written)
{
    int r = OK;

    /*
     * your lab2 code goes here.
     * note: write using ec->put().
     * when off > length of original file, fill the holes with '\0'.
     */
		std::string oridata;
		if (ec->get(ino, oridata) != extent_protocol::OK)
				return IOERR;
		std::string newdata(data);
		newdata = newdata.substr(0, size);
		if (off > oridata.size())
		{
				char *buf = new char[off + size];
				for (int i = 0; i < oridata.size(); i++)
						buf[i] = oridata[i];
				for (int i = oridata.size(); i < off; i++)
						buf[i] = '\0';
				for (int i = off; i < off + size; i++)
						buf[i] = data[i - off];
				extent_protocol::status s = ec->put2(ino, buf, off + size);
				delete buf;
				if (s != extent_protocol::OK)
						return IOERR;
		}
		else
		{
				oridata = oridata.replace(off, newdata.size(), newdata);
				if (ec->put(ino, oridata) != extent_protocol::OK)
						return IOERR;
		}
		bytes_written = size;

    return r;
}

int yfs_client::unlink(inum parent,const char *name)
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
		if (readdir(parent, filelist) != OK)
				return IOERR;
		std::list<dirent>::iterator it;
		bool found = false;
		for (it = filelist.begin();it != filelist.end(); it++)
		{
				if (it->name == std::string(name))
				{
						found = true;
						if (ec->remove(it->inum) != extent_protocol::OK)
								return IOERR;
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
				return IOERR;
		//printf("!!xxh unlink data %s\n", data.c_str());
		size_t bw;
		if (setattr(parent, 0) != OK)
				return IOERR;
		if (write(parent, data.size(), 0, data.c_str(), bw) != OK)
				return IOERR;
    return r;
}

