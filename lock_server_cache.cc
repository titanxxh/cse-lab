// the caching lock server implementation

#include "lock_server_cache.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "lang/verify.h"
#include "handle.h"
#include "tprintf.h"


lock_server_cache::lock_server_cache()
{
	nacquire = 0;
	pthread_mutex_init(&lid_m, NULL);
	pthread_cond_init(&lid_cond, 0);
	pthread_mutex_init(&revoke_m, NULL);
	pthread_cond_init(&revoke_cond, 0);
	pthread_mutex_init(&retry_m, NULL);
	pthread_cond_init(&retry_cond, 0);
	pthread_create(&tRevoke, NULL, &threadWrapper_revoke, this);
	pthread_create(&tRetry, NULL, &threadWrapper_retry, this);
	pthread_detach(tRevoke);
	pthread_detach(tRetry);
}

lock_server_cache::~lock_server_cache()
{
	ScopedLock lidm(&lid_m);
	locks.clear();
}

int lock_server_cache::acquire(lock_protocol::lockid_t lid, std::string id, 
                               int &r)
{
  lock_protocol::status ret;
	r = nacquire;

	ScopedLock revokem(&revoke_m);
	ScopedLock lidm(&lid_m);
	if (locks.find(lid) == locks.end())
	{
		nacquire ++;
		struct lockstatus ls;
		ls.clt = id;
		ls.status = 1;
		ls.revoke = false;
		locks[lid] = ls;
		//locks.insert(std::pair<lock_protocol::lockid_t,struct lockstatus>(lid,ls));
		tprintf("acquire grant to clt %s new lid %llu\n", id.c_str(), lid);
		pthread_cond_signal(&lid_cond);
		ret = lock_protocol::OK;
	}
	else if (locks[lid].clt == "")
	{
		nacquire ++;
		locks[lid].status = 1;
		locks[lid].clt = id;
		locks[lid].revoke = false;
		tprintf("acquire grant to clt %s free lid %llu\n", id.c_str(), lid);
		pthread_cond_signal(&lid_cond);
		ret = lock_protocol::OK;
	}
	else
	{
		//call revoke
		struct revoke r;
		r.clt = id;
		r.lid = lid;
		revoke_queue.push_back(r);
		locks[lid].waiting_clt.push_back(id);
		tprintf("acquire failed send RETRY clt %s lid %llu\n", id.c_str(), lid);
		pthread_cond_signal(&revoke_cond);
		ret = lock_protocol::RETRY;
	}
  return ret;
}

int 
lock_server_cache::release(lock_protocol::lockid_t lid, std::string id, 
         int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
	ScopedLock retrym(&retry_m);
	ScopedLock lidm(&lid_m);
	tprintf("release lid %llu released by clt: %s %s\n", lid, id.c_str(), locks[lid].clt.c_str());
	released_lid_queue.push_back(lid);
	nacquire --;
	locks[lid].clt = "";
	locks[lid].status = 0;
	pthread_cond_signal(&retry_cond);
  return ret;
}

lock_protocol::status
lock_server_cache::stat(lock_protocol::lockid_t lid, std::string clt, int &r)
{
	ScopedLock lidm(&lid_m);
	if (locks[lid].clt != clt)
		r = -1;
	else
		r = locks[lid].status;
  tprintf("stat lid %llu %d\n", lid, r);
  return lock_protocol::OK;
}

void *lock_server_cache::DoRetryQueue(void)
{
	while (true)
	{
		lock_protocol::lockid_t released_lid;
		{
			ScopedLock retrym(&retry_m);
			while (released_lid_queue.empty())
			{
				pthread_cond_wait(&retry_cond, &retry_m);
			}
			released_lid = released_lid_queue.front();
			released_lid_queue.pop_front();
		}
		std::string waiting_clt;
		rlock_protocol::status ret;
		{
			ScopedLock lidm(&lid_m);
			waiting_clt = locks[released_lid].waiting_clt.front();
			locks[released_lid].waiting_clt.pop_front();
		}
		ret = clientRPC(rlock_protocol::retry, released_lid, waiting_clt);
		if (ret != rlock_protocol::OK)
		{
			ScopedLock lidm(&lid_m);
			locks[released_lid].waiting_clt.push_back(waiting_clt);
			tprintf("DoRetryQueue failed: clt %s lid %llu\n", waiting_clt.c_str(), released_lid);
		}
		else
		{
			tprintf("DoRetryQueue success: clt %s lid %llu\n", waiting_clt.c_str(), released_lid);
		}
	}
	return NULL;
}

void *lock_server_cache::DoRevokeQueue(void)
{
	while (true)
	{
		struct revoke r;
		std::string lock_owner;
		rlock_protocol::status ret;
		bool flag_revoke;
		{
			ScopedLock revokem(&revoke_m);
			while (revoke_queue.empty())
			{
				pthread_cond_wait(&revoke_cond, &revoke_m);
			}
			r = revoke_queue.front();
		}
		{
			ScopedLock lidm(&lid_m);
			lock_owner = locks[r.lid].clt;
			if (locks[r.lid].revoke)
			{
				while (locks[r.lid].revoke)
				{
					pthread_cond_wait(&lid_cond, &lid_m);
				}
				flag_revoke = false;
			}
			else
			{
				flag_revoke = true;
				locks[r.lid].revoke = true;
			}
		}
		if (flag_revoke)
		{
			ScopedLock revokem(&revoke_m);
			revoke_queue.pop_front();
		}
		if (lock_owner != "" && flag_revoke)
		{
			ret = clientRPC(rlock_protocol::revoke, r.lid, lock_owner);
			if (ret != rlock_protocol::OK)
			{
				tprintf("DoRevokeQueue failed: clt %s lid %llu\n", lock_owner.c_str(), r.lid);
			}
			else
			{
				tprintf("DoRevokeQueue success: clt %s lid %llu\n", lock_owner.c_str(), r.lid);
			}
		}
	}
	return NULL;
}

rlock_protocol::status
lock_server_cache::clientRPC(int rpc, lock_protocol::lockid_t lid, std::string id)
{
	rlock_protocol::status ret;
	handle h(id);
	rpcc *cl = h.safebind();
	if (cl)
	{
		int r;
		ret = cl->call(rpc, lid, r);
		if (ret != lock_protocol::OK)
		{
			tprintf("clientRPC lid %llu failed for clt %s\n", lid, id.c_str());
		}
	}
	else
		tprintf("clientRPC lid: %llu bind handle failed for %s\n", lid, id.c_str());
	return ret;
}
