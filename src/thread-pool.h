// -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
// (c) 2021 Henner Zeller <h.zeller@acm.org>
//
// timg - a terminal image viewer.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation version 2.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://gnu.org/licenses/gpl-2.0.txt>
//
#ifndef TIMG_THREAD_POOL
#define TIMG_THREAD_POOL

#include <deque>
#include <future>
#include <thread>
#include <vector>

namespace timg {
// Simplistic thread-pool. Unfortunately, std::async() was a good idea but
// not with a relevant implementation
class ThreadPool {
public:
    explicit ThreadPool(int count) {
        while (count--) {
            threads_.push_back(new std::thread(&ThreadPool::Runner, this));
        }
    }

    ~ThreadPool() {
        CancelAllWork();
        for (std::thread *t : threads_) {
            t->join();
            delete t;
        }
    }

    template <class T>
    std::future<T> ExecAsync(std::function<T()> f) {
        std::promise<T> *p           = new std::promise<T>();
        std::future<T> future_result = p->get_future();
        auto promise_fulfiller       = [p, f]() {
            p->set_value(f());
            delete p;
        };
        lock_.lock();
        work_queue_.push_back(promise_fulfiller);
        lock_.unlock();
        cv_.notify_one();
        return future_result;
    }

    void CancelAllWork() {
        lock_.lock();
        exiting_ = true;
        lock_.unlock();
        cv_.notify_all();
    }

private:
    void Runner() {
        for (;;) {
            std::unique_lock<std::mutex> l(lock_);
            cv_.wait(l, [this]() { return !work_queue_.empty() || exiting_; });
            if (exiting_) return;
            auto process_work_item = work_queue_.front();
            work_queue_.pop_front();
            l.unlock();
            process_work_item();
        }
    }

    std::vector<std::thread *> threads_;
    std::mutex lock_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> work_queue_;
    bool exiting_ = false;
};

}  // namespace timg
#endif  // TIMG_THREAD_POOL
