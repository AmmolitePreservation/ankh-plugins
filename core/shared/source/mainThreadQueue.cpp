#include "mainThreadQueue.h"

#include "fatalError.h"

#include <algorithm>
#include <deque>

namespace {

enum class TaskState { waiting, running, done };

struct Task {
  std::function<std::string()> work;
  std::string result;
  TaskState state;
  HANDLE finished;
};

DWORD gameMainThreadID = 0;
CRITICAL_SECTION queueLock;
std::deque<Task*> waitingTasks;
// Work may pump messages itself; a nested pump must not start the next task inside this one.
bool isDraining = false;
void (*removePumpCallback)(void (*callback)()) = nullptr;

void drainQueue() {
  if (GetCurrentThreadId() != gameMainThreadID || isDraining) {
    return;
  }
  isDraining = true;
  for (;;) {
    EnterCriticalSection(&queueLock);
    if (waitingTasks.empty()) {
      LeaveCriticalSection(&queueLock);
      break;
    }
    Task* task = waitingTasks.front();
    waitingTasks.pop_front();
    task->state = TaskState::running;
    LeaveCriticalSection(&queueLock);
    task->result = task->work();
    EnterCriticalSection(&queueLock);
    task->state = TaskState::done;
    LeaveCriticalSection(&queueLock);
    SetEvent(task->finished);
  }
  isDraining = false;
}

}

void installMainThreadQueue(const PluginHost& host) {
  gameMainThreadID = host.mainThreadID;
  removePumpCallback = host.removePumpCallback;
  InitializeCriticalSection(&queueLock);
  host.addPumpCallback(&drainQueue);
}

void removeMainThreadQueue() {
  removePumpCallback(&drainQueue);
  EnterCriticalSection(&queueLock);
  const bool isIdle = waitingTasks.empty();
  LeaveCriticalSection(&queueLock);
  if (!isIdle) {
    fatalError("removeMainThreadQueue called with main-thread tasks still waiting");
  }
  DeleteCriticalSection(&queueLock);
}

bool runOnMainThread(const std::function<std::string()>& work, unsigned long waitMilliseconds, std::string* result) {
  if (GetCurrentThreadId() == gameMainThreadID) {
    fatalError("runOnMainThread was called on the main thread, which would wait on itself");
  }
  Task task = { work, std::string(), TaskState::waiting, CreateEventA(nullptr, TRUE, FALSE, nullptr) };
  if (task.finished == nullptr) {
    fatalError("Could not create an event for a main-thread task (error %lu)", GetLastError());
  }
  EnterCriticalSection(&queueLock);
  waitingTasks.push_back(&task);
  LeaveCriticalSection(&queueLock);
  // A loop blocked in GetMessage only wakes for a message. Posting fails when the main thread has
  // no message queue yet, and then it is not blocked in GetMessage either.
  PostThreadMessageA(gameMainThreadID, WM_NULL, 0, 0);

  const DWORD waited = WaitForSingleObject(task.finished, waitMilliseconds);
  if (waited == WAIT_TIMEOUT) {
    EnterCriticalSection(&queueLock);
    const bool isWithdrawn = task.state == TaskState::waiting;
    if (isWithdrawn) {
      waitingTasks.erase(std::find(waitingTasks.begin(), waitingTasks.end(), &task));
    }
    LeaveCriticalSection(&queueLock);
    if (isWithdrawn) {
      CloseHandle(task.finished);
      return false;
    }
    // Game code that has started cannot be abandoned midway.
    WaitForSingleObject(task.finished, INFINITE);
  } else if (waited != WAIT_OBJECT_0) {
    fatalError("Waiting for a main-thread task failed (error %lu)", GetLastError());
  }
  CloseHandle(task.finished);
  *result = task.result;
  return true;
}
