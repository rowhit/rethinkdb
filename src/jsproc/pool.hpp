#ifndef JSPROC_POOL_HPP_
#define JSPROC_POOL_HPP_

#include <sys/types.h>          // pid_t

#include "concurrency/one_per_thread.hpp"
#include "concurrency/semaphore.hpp"
#include "containers/archive/socket_stream.hpp"
#include "containers/intrusive_list.hpp"
#include "jsproc/job.hpp"
#include "jsproc/spawner.hpp"

namespace jsproc {

class job_handle_t;
class pool_t;

// Use this to create one process pool per thread, and access the appropriate
// one (via `get()`).
class pool_group_t
{
    friend class pool_t;

  public:
    static const unsigned int DEFAULT_MIN_WORKERS = 2;
    static const unsigned int DEFAULT_MAX_WORKERS = 2;

    struct config_t {
        config_t()
            : min_workers(DEFAULT_MIN_WORKERS), max_workers(DEFAULT_MAX_WORKERS) {}
        unsigned int min_workers;        // >= 0
        unsigned int max_workers;        // >= min_workers, > 0
    };

    static const config_t DEFAULTS;

    pool_group_t(const spawner_t::info_t &info, const config_t &config);

    pool_t *get() { return pool_maker_.get(); }

  private:
    spawner_t spawner_;
    config_t config_;
    one_per_thread_t<pool_t> pool_maker_;
};

// A per-thread worker pool.
class pool_t :
        public home_thread_mixin_t
{
    friend class job_handle_t;

  public:
    pool_t(pool_group_t *group);
    ~pool_t();

    // Spawns `job` off and initializes `handle` with a connection to it.
    // Returns 0 on success, -1 on error.
    int spawn_job(job_t *job, job_handle_t *handle);

  private:
    pool_group_t::config_t *config() { return &group_->config_; }
    spawner_t *spawner() { return &group_->spawner_; }

    // A worker process.
    class worker_t :
        public intrusive_list_node_t<worker_t>,
        public unix_socket_stream_t
    {
      public:
        worker_t(pool_t *pool, pid_t pid, fd_t fd);
        ~worker_t();

        // Called when we get an error on a worker process socket, which usually
        // indicates the worker process' death.
        void on_error();

        // Inherited from unix_socket_stream_t. Called when epoll finds an error
        // condition on our socket. Calls on_error().
        virtual void on_event(int events);

        pool_t *pool_;
        pid_t pid_;

      private:
        DISABLE_COPYING(worker_t);
    };

    typedef intrusive_list_t<worker_t> workers_t;

  private:
    // Connects us to a worker. Private; used only by spawn_job.
    int connect(job_handle_t *handle);

    // Called by job_handle_t to indicate the job has finished or errored.
    void disconnect(job_handle_t *handle);

    void spawn_workers(unsigned int n);
    void create_worker(pid_t pid, fd_t fd);
    void end_worker(workers_t *list, worker_t *worker);

    unsigned int num_workers() {
        return idle_workers_.size() + busy_workers_.size() + num_spawning_workers_;
    }

  private:
    pool_group_t *group_;

    // Worker processes.
    workers_t idle_workers_, busy_workers_;

    // Count of the number of workers in the process of being spawned. Necessary
    // in order to maintain (min_workers, max_workers) bounds without race
    // conditions.
    unsigned int num_spawning_workers_;

    // Used to signify that you're using a worker. In particular, lets us block
    // for a worker to become available when we already have
    // config_->max_workers workers running.
    semaphore_t worker_semaphore_;
};

// A handle to a running job.
class job_handle_t :
    public read_stream_t, public write_stream_t
{
    friend class pool_t;

  public:
    // When constructed, job handles are in an "uninitialized" state, not
    // safe to use. They get initialized by pool_t::connect(), which is
    // called by pool_t::spawn_job.
    job_handle_t();
    ~job_handle_t();

    // On either read or write error, the job handle becomes "uninitialized"
    // again and must not be used.
    virtual MUST_USE int64_t read(void *p, int64_t n) {
        rassert(worker_);
        int res = worker_->read(p, n);
        if (-1 == res || 0 == res) disconnect();
        return res;
    }

    virtual int64_t write(const void *p, int64_t n) {
        rassert(worker_);
        int res = worker_->write(p, n);
        if (-1 == res) disconnect();
        return res;
    }

  private:
    void disconnect();

  private:
    pool_t::worker_t *worker_;
};

} // namespace jsproc

#endif // JSPROC_POOL_HPP_
