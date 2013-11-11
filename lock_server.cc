// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

lock_server::lock_server():
  nacquire (0)
{
	VERIFY(pthread_mutex_init(&tmutex, NULL) == 0);
	VERIFY(pthread_cond_init(&avail, 0) == 0);
}

lock_protocol::status
lock_server::stat(int clt, lock_protocol::lockid_t lid, int &r)
{
	ScopedLock mtx(&tmutex);
  lock_protocol::status ret = lock_protocol::OK;
  r = locks[lid];
  printf("stat request from clt %d id %d status %d\n", clt, lid, r);
  return ret;
}

lock_protocol::status
lock_server::acquire(int clt, lock_protocol::lockid_t lid, int &r)
{
  printf("acquire request from clt %d\n", clt);
  lock_protocol::status ret = lock_protocol::OK;
	r = nacquire;
	if (clt < 0)
	{
		ret = lock_protocol::RPCERR;
	}
	else
	{
		ScopedLock mtx(&tmutex);
		while (true)
		{
			if (locks.find(lid) == locks.end())
			{
				locks[lid] = 1;
				locks_owner[lid] = clt;
				nacquire ++;
  			printf("acquire lock granted to clt %d lid %d\n", clt, lid);
				break;
			}
			else if (locks_owner[lid] < 0)
			{
				locks[lid] = 0;
				locks_owner[lid] = clt;
				nacquire ++;
  			printf("acquire lock granted to clt %d lid %d\n", clt, lid);
				break;
			}
			else
			{
				printf("acquire lock lid %d from clt %d is locked, waiting!!!\n", lid, clt);
				VERIFY(pthread_cond_wait(&avail, &tmutex) == 0);
			}
		}
	}
  return ret;
}

lock_protocol::status
lock_server::release(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  printf("release request from clt %d\n", clt);
	r = nacquire;
	if (clt < 0)
	{
		ret = lock_protocol::RPCERR;
	}
	else
	{
		ScopedLock mtx(&tmutex);
		if (locks.find(lid) == locks.end())
		{
  		printf("release request from clt %d for lid %d not exist\n", clt, lid);
			ret = lock_protocol::RPCERR;
		}
		else if (locks_owner[lid] != clt)
		{
  		printf("release request from clt %d for lid %d does not match its owner\n", clt, lid);
			ret = lock_protocol::RPCERR;
		}
		else
		{
			locks_owner[lid] = -1;
			locks[lid] = 0;
			nacquire --;
  		printf("release request from clt %d for lid %d\n", clt, lid);
			VERIFY(pthread_cond_broadcast(&avail) == 0);
		}
	}

  return ret;
}
