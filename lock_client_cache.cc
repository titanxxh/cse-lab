// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache.h"
#include "extent_client_cache.h"
#include "rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include "tprintf.h"

lock_release_flush::lock_release_flush(extent_client *ecc)
	:ec(ecc)
{
}

void
lock_release_flush::dorelease(lock_protocol::lockid_t lid)
{
	tprintf("xxh5: flush lid %llu to disk\n", lid);
	ec->flush((extent_protocol::extentid_t)lid);
}

int lock_client_cache::last_port = 0;

lock_client_cache::lock_client_cache(std::string xdst, 
				     class lock_release_user *_lu)
  : lock_client(xdst), lu(_lu)
{
  srand(time(NULL)^last_port);
  rlock_port = ((rand()%32000) | (0x1 << 10));
  const char *hname;
  // VERIFY(gethostname(hname, 100) == 0);
  hname = "127.0.0.1";
  std::ostringstream host;
  host << hname << ":" << rlock_port;
  id = host.str();
  last_port = rlock_port;
  rpcs *rlsrpc = new rpcs(rlock_port);
  rlsrpc->reg(rlock_protocol::revoke, this, &lock_client_cache::revoke_handler);
  rlsrpc->reg(rlock_protocol::retry, this, &lock_client_cache::retry_handler);

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

int
lock_client_cache::stat(lock_protocol::lockid_t lid)
{
	int r;
	lock_protocol::status ret = cl->call(lock_protocol::stat, lid, id, r);
	return r;
}

lock_protocol::status
lock_client_cache::acquire(lock_protocol::lockid_t lid)
{
	bool is_acquired = false;
	int r;
	while (!is_acquired)
	{
		{
			ScopedLock lm(&lid_m);
			if (locks.find(lid) == locks.end())
			{
				tprintf("lock_client: client %s acquire new lock %llu (%lu)\n", id.c_str(), lid, (unsigned long)pthread_self());
				struct lockstatus s;
				s.status = none;
				s.revoke = false;
				locks[lid] = s;
				//locks.insert(std::pair<lock_protocol::lockid_t, struct lockstatus> (lid, s));
			}
			else if (locks[lid].status != free && locks[lid].status != none)
			{
				while (locks[lid].status != free && locks[lid].status != none)
				{
					pthread_cond_wait(&lid_cond, &lid_m);
				}
			}
			if (locks[lid].status == none)
			{
				locks[lid].status = acquiring;
			}
			else if (locks[lid].status == free)
			{
				locks[lid].status = locked;
				is_acquired = true;
				tprintf("lock_client: client has free lock %llu\n", lid);
			}
		}
		if (!is_acquired)
		{
			tprintf("lock_client: client %s start RPC acquire lock %llu (%lu)\n", id.c_str(), lid, (unsigned long)pthread_self());
			lock_protocol::status ret = cl->call(lock_protocol::acquire, lid, id, r);
			if (ret == lock_protocol::OK)
			{
				ScopedLock lm(&lid_m);
				tprintf("lock_client: client %s acquire from server for lock %llu (%lu)\n", id.c_str(), lid, (unsigned long)pthread_self());
				locks[lid].status = locked;
				is_acquired = true;
			}
			else if (ret == lock_protocol::RETRY)
			{
				ScopedLock lm(&lid_m);
				tprintf("lock_client: client %s acquire from server for lock %llu retry later (%lu)\n", id.c_str(), lid, (unsigned long)pthread_self());
				locks[lid].status = retry_later;
				pthread_cond_broadcast(&lid_cond);
			}
		}
	}
  return r;
}

lock_protocol::status
lock_client_cache::release(lock_protocol::lockid_t lid)
{
	
  int ret = rlock_protocol::OK;
	int r;
	bool to_release = false;
	tprintf("lock_client: client %s release lock %llu (%lu)\n", id.c_str(), lid, (unsigned long)pthread_self());
	{
		ScopedLock lm(&lid_m);
		if (locks[lid].revoke)
		{
			locks[lid].revoke = false;
			locks[lid].status = releasing;
			to_release = true;
			tprintf("lock_client: client %s will call release lock %llu (%lu)\n", id.c_str(), lid, (unsigned long)pthread_self());
		}
		else
		{
			tprintf("lock_client: client %s just free lock %llu (%lu)\n", id.c_str(), lid, (unsigned long)pthread_self());
			locks[lid].status = free;
		}
	}
	if (to_release)
	{
		if (lu)
		{
			lu->dorelease(lid);
		}
		lock_protocol::status ret = cl->call(lock_protocol::release, lid, id, r);
		tprintf("lock_client: client %s call release lock %llu return (%lu)\n", id.c_str(), lid, (unsigned long)pthread_self());
		ScopedLock lm(&lid_m);
		locks[lid].status = none;
	}
	ScopedLock lm(&lid_m);
	pthread_cond_broadcast(&lid_cond);
  return ret;
}

rlock_protocol::status
lock_client_cache::revoke_handler(lock_protocol::lockid_t lid, 
                                  int &)
{
  int ret = rlock_protocol::OK;
	ScopedLock revokem(&revoke_m);
	tprintf("lock_client: revoke RPC for lid %llu clt %s (%lu)\n", lid, id.c_str(), (unsigned long)pthread_self());
	revoke_queue.push_back(lid);
	pthread_cond_signal(&revoke_cond);
  return ret;
}

void *lock_client_cache::DoRevokeQueue(void)
{
	while (true)
	{
		lock_protocol::lockid_t lid;// = revoke_queue.front();
		{
			ScopedLock revokem(&revoke_m);
			while (revoke_queue.empty())
			{
				pthread_cond_wait(&revoke_cond, &revoke_m);
			}
			lid = revoke_queue.front();
			revoke_queue.pop_front();
		}
		{
			ScopedLock lidm(&lid_m);
			while (locks[lid].status != free)
			{
				tprintf("lock_client: do revoke wait lid %llu clt %s status %d\n", lid, id.c_str(), (int)locks[lid].status);
				pthread_cond_wait(&lid_cond, &lid_m);
			}
			tprintf("lock_client: do revoke for lid %llu call release RPC clt %s (%lu)\n", lid, id.c_str(), (unsigned long)pthread_self());
			locks[lid].revoke = true;
		}
		lock_protocol::status ret = release(lid);
		VERIFY(ret == lock_protocol::OK);
	}
	return NULL;
}

rlock_protocol::status
lock_client_cache::retry_handler(lock_protocol::lockid_t lid, 
                                 int &)
{
  int ret = rlock_protocol::OK;
	ScopedLock retrym(&retry_m);
	retry_queue.push_back(lid);
	tprintf("lock_client: retry RPC for lid %llu clt %s (%lu)\n", lid, id.c_str(), (unsigned long)pthread_self());
	pthread_cond_signal(&retry_cond);
  return ret;
}

void *lock_client_cache::DoRetryQueue(void)
{
	while (true)
	{
		lock_protocol::lockid_t lid;
		{
			ScopedLock retrym(&retry_m);
			while (retry_queue.empty())
			{
				pthread_cond_wait(&retry_cond, &retry_m);
			}
			lid = retry_queue.front();
			retry_queue.pop_front();
		}
		ScopedLock lidm(&lid_m);
		while (locks[lid].status == acquiring)
		{
			tprintf("lock_client: do retry clt %s lid %llu wait rt for acq\n", id.c_str(), lid);
			pthread_cond_wait(&lid_cond, &lid_m);
		}
		if (locks[lid].status != locked)
		{
			tprintf("lock_client: do retry clt %s lid %llu can retry acq\n", id.c_str(), lid);
			locks[lid].status = none;
			pthread_cond_broadcast(&lid_cond);
		}
	}
	return NULL;
}
