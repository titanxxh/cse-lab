#include "extent_client_cache.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

extent_client_cache::extent_client_cache(std::string dst)
	: extent_client(dst)
{
	pthread_mutex_init(&ec_m, NULL);
}

extent_protocol::status extent_client_cache::create(uint32_t type, extent_protocol::extentid_t &eid)
{
	return extent_client::create(type, eid);
}

extent_protocol::status extent_client_cache::get(extent_protocol::extentid_t eid, std::string &buf)
{
  extent_protocol::status ret = extent_protocol::OK;
	bool valuegot = false;
	{
		ScopedLock ecm(&ec_m);
		if (cachemap.find(eid) != cachemap.end())
		{
			if (cachemap[eid].cached)
			{
				printf("xxh5: get eid %llu from cache!\n", eid);
				buf = cachemap[eid].value;
				valuegot = true;
			}
		}
	}
	if (valuegot)
	{
		return ret;
	}
	ret = cl->call(extent_protocol::get, eid, buf);
	printf("xxh5: get eid %llu calling RPC returned\n", eid);
	if (ret == extent_protocol::OK)
	{
		ScopedLock ecm(&ec_m);
		if (cachemap.find(eid) == cachemap.end())
		{
			cachemap[eid].acached = false;
			cachemap[eid].modified = false;
			printf("xxh5: get new eid %llu calling RPC\n", eid);
		}
		cachemap[eid].value = buf;
		printf("xxh5: get eid %llu calling RPC\n", eid);
		cachemap[eid].cached = true;
	}
	return ret;
}

extent_protocol::status extent_client_cache::getattr(extent_protocol::extentid_t eid, extent_protocol::attr &a)
{
  extent_protocol::status ret = extent_protocol::OK;
  bool valuegot = false;
  {
    ScopedLock ecm(&ec_m);
    if (cachemap.find(eid) != cachemap.end())
    {
			if (cachemap[eid].acached)
			{
  		  a = cachemap[eid].a;
	      printf("xxh5: getattr eid %llu from cache! type %d\n", eid, a.type);
      	valuegot = true;
			}
    }
  }
  if (valuegot)
  {
    return ret;
  }
  ret = cl->call(extent_protocol::getattr, eid, a);
  printf("xxh5: getattr eid %llu calling RPC returned\n", eid);
  if (ret == extent_protocol::OK)
  {
    ScopedLock ecm(&ec_m);
		if (cachemap.find(eid) == cachemap.end())
		{
    	cachemap[eid].cached = false;
    	cachemap[eid].modified = false;
			cachemap[eid].value.clear();
  		printf("xxh5: getattr new eid %llu calling RPC\n", eid);
		}
    cachemap[eid].a = a;
		cachemap[eid].acached = true;
  	printf("xxh5: getattr new eid %llu calling RPC type %d\n", eid, a.type);
  }
  return ret;
}

extent_protocol::status extent_client_cache::put(extent_protocol::extentid_t eid, std::string buf)
{
	ScopedLock ecm(&ec_m);
  extent_protocol::status ret = extent_protocol::OK;
	cachemap[eid].a.atime = cachemap[eid].a.mtime = time(NULL);
	cachemap[eid].a.size = buf.size();
	cachemap[eid].value = buf;
	cachemap[eid].cached = cachemap[eid].modified = true;
  printf("xxh5: put eid %llu in cache type %d\n", eid, cachemap[eid].a.type);
	return ret;
}

extent_protocol::status extent_client_cache::remove(extent_protocol::extentid_t eid)
{
	ScopedLock ecm(&ec_m);
  extent_protocol::status ret = extent_protocol::OK;
	cachemap.erase(eid);
  printf("xxh5: remove eid %llu in cache\n", eid);
	return ret;

}

void extent_client_cache::flush(extent_protocol::extentid_t eid)
{
  extent_protocol::status ret = extent_protocol::OK;
	std::string buf;
	bool toremove = false;
	bool toupdate = false;
	{
		ScopedLock ecm(&ec_m);
  	printf("xxh5: flush eid %llu\n", eid);
		if (cachemap.find(eid) == cachemap.end())
		{
			toremove = true;
		}
		else
		{
			if (cachemap[eid].modified)
			{
				toupdate = true;
				buf = cachemap[eid].value;
			}
			cachemap.erase(eid);
		}	
	}
	int r;
	if (toremove)
	{
		ret = cl->call(extent_protocol::remove, eid, r);
  	printf("xxh5: remove eid %llu calling RPC returned\n", eid);
	}
	else if (toupdate)
	{
		ret = cl->call(extent_protocol::put, eid, buf, r);
  	printf("xxh5: put eid %llu calling RPC returned\n", eid);
	}
}

