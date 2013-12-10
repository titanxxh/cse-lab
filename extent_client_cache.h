#ifndef extent_client_cache_h
#define extent_client_cache_h

#include "extent_client.h"
#include <pthread.h>
#include <map>

class extent_client_cache : public extent_client
{
private:
	pthread_mutex_t ec_m;
	struct cached_value
	{
		std::string value;
		extent_protocol::attr a;
		bool cached, acached, modified;
	};
	std::map<extent_protocol::extentid_t, struct cached_value> cachemap;

public:
  extent_client_cache(std::string dst);

  extent_protocol::status create(uint32_t type, extent_protocol::extentid_t &eid);
  extent_protocol::status get(extent_protocol::extentid_t eid, std::string &buf);
  extent_protocol::status getattr(extent_protocol::extentid_t eid, extent_protocol::attr &a);
  extent_protocol::status put(extent_protocol::extentid_t eid, std::string buf);
  extent_protocol::status remove(extent_protocol::extentid_t eid);
	void flush(extent_protocol::extentid_t eid);
};

#endif
