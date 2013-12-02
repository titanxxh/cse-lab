#ifndef lock_server_cache_h
#define lock_server_cache_h

#include <string>


#include <map>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_server.h"


class lock_server_cache {
 private:
  int nacquire;
	struct lockstatus
	{
		std::string clt;
		std::list<std::string> waiting_clt;
		int status;
		bool revoke;
	};
	std::map<lock_protocol::lockid_t, struct lockstatus> locks;
	struct revoke
	{
		std::string clt;
		lock_protocol::lockid_t lid;
	};
	std::list<struct revoke> revoke_queue;
	std::list<lock_protocol::lockid_t> released_lid_queue;
	rlock_protocol::status clientRPC(int, lock_protocol::lockid_t, std::string);
	pthread_t tRevoke, tRetry;
	pthread_mutex_t lid_m, revoke_m, retry_m;
	pthread_cond_t lid_cond, revoke_cond, retry_cond;

 public:
  lock_server_cache();
	~lock_server_cache();
  lock_protocol::status stat(lock_protocol::lockid_t, std::string id, int &);
  int acquire(lock_protocol::lockid_t, std::string id, int &);
  int release(lock_protocol::lockid_t, std::string id, int &);
	void *DoRetryQueue(void);
	void *DoRevokeQueue(void);
	static void *threadWrapper_revoke(void *arg)
	{
		return ((lock_server_cache *)arg)->DoRevokeQueue();
	}
	static void *threadWrapper_retry(void *arg)
	{
		return ((lock_server_cache *)arg)->DoRetryQueue();
	}
};

#endif
