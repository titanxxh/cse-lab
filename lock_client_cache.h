// lock client interface.

#ifndef lock_client_cache_h

#define lock_client_cache_h

#include <string>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_client.h"
#include "lang/verify.h"


// Classes that inherit lock_release_user can override dorelease so that 
// that they will be called when lock_client releases a lock.
// You will not need to do anything with this class until Lab 5.
class lock_release_user {
 public:
  virtual void dorelease(lock_protocol::lockid_t) = 0;
  virtual ~lock_release_user() {};
};

class lock_client_cache : public lock_client {
 private:
  class lock_release_user *lu;
  int rlock_port;
  std::string hostname;
  std::string id;
	enum lock_status {
      none=0,
      free,
      locked,
      acquiring,
      releasing,
      retry_later
  };
	struct lockstatus {
		lock_status status;
		bool revoke;
	};
	std::map<lock_protocol::lockid_t, struct lockstatus> locks;
	std::list<lock_protocol::lockid_t> revoke_queue;
	std::list<lock_protocol::lockid_t> retry_queue;
	pthread_mutex_t lid_m, revoke_m, retry_m;
	pthread_cond_t lid_cond, revoke_cond, retry_cond;
	pthread_t tRevoke, tRetry;
 public:
  static int last_port;
  lock_client_cache(std::string xdst, class lock_release_user *l = 0);
  virtual ~lock_client_cache() {};
	int stat(lock_protocol::lockid_t);
  lock_protocol::status acquire(lock_protocol::lockid_t);
  lock_protocol::status release(lock_protocol::lockid_t);
	static void *threadWrapper_revoke(void *arg)
	{
		return ((lock_client_cache *)arg)->DoRevokeQueue();
	}
	static void *threadWrapper_retry(void *arg)
	{
		return ((lock_client_cache *)arg)->DoRetryQueue();
	}
	void *DoRevokeQueue(void);
	void *DoRetryQueue(void);
  rlock_protocol::status revoke_handler(lock_protocol::lockid_t, 
                                        int &);
  rlock_protocol::status retry_handler(lock_protocol::lockid_t, 
                                       int &);
};


#endif
