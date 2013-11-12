// this is the extent server

#ifndef extent_server_h
#define extent_server_h

#include <string>
#include <map>
#include "rpc/mystring.h"
#include "extent_protocol.h"
#include "inode_manager.h"

class extent_server {
 protected:
#if 0
  typedef struct extent {
    std::string data;
    struct extent_protocol::attr attr;
  } extent_t;
  std::map <extent_protocol::extentid_t, extent_t> extents;
#endif
  inode_manager *im;

 public:
  extent_server();

  int create(uint32_t type, extent_protocol::extentid_t &id);
	//int put2(extent_protocol::extentid_t id, const char *buf, uint32_t size, int &);
	int put2(extent_protocol::extentid_t id, struct mystring str, int &);
  int put(extent_protocol::extentid_t id, std::string, int &);
  int get(extent_protocol::extentid_t id, std::string &);
  int getattr(extent_protocol::extentid_t id, extent_protocol::attr &);
  int remove(extent_protocol::extentid_t id, int &);
};

#endif 







