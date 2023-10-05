#include "mtx.h"
#include "macro.h"
#include "coscheduler.h"

namespace yhchaos {

Sem::Sem(uint32_t count) {
    if(sem_init(&m_semaphore, 0, count)) {
        throw std::logic_error("sem_init error");
    }
}

Sem::~Sem() {
    sem_destroy(&m_semaphore);
}

void Sem::wait() {
    if(sem_wait(&m_semaphore)) {
        throw std::logic_error("sem_wait error");
    }
}

void Sem::notify() {
    if(sem_post(&m_semaphore)) {
        throw std::logic_error("sem_post error");
    }
}

CoroutineSem::CoroutineSem(size_t initial_concurrency)
    :m_concurrency(initial_concurrency) {
}

CoroutineSem::~CoroutineSem() {
    YHCHAOS_ASSERT(m_waiters.empty());
}

bool CoroutineSem::tryWait() {
    YHCHAOS_ASSERT(CoScheduler::GetThis());
    {
        MtxType::Lock lock(m_mutex);
        if(m_concurrency > 0u) {
            --m_concurrency;
            return true;
        }
        return false;
    }
}

void CoroutineSem::wait() {
    YHCHAOS_ASSERT(CoScheduler::GetThis());
    {
        MtxType::Lock lock(m_mutex);
        if(m_concurrency > 0u) {
            --m_concurrency;
            return;
        }
        m_waiters.push_back(std::make_pair(CoScheduler::GetThis(), Coroutine::GetThis()));
    }
    Coroutine::YieldToHold();
}

void CoroutineSem::notify() {
    MtxType::Lock lock(m_mutex);
    if(!m_waiters.empty()) {
        auto next = m_waiters.front();
        m_waiters.pop_front();
        next.first->coschedule(next.second);
    } else {
        ++m_concurrency;
    }
}

}
