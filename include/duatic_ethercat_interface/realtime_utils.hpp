#pragma once

#include <pthread.h>
#include <sched.h>

#include <duatic_message_logger/log.hpp>

namespace duatic::ethercat_interface
{
/**
 * @brief set_realtime_priority - try to configure the passed thread to have higher priorities and behave more rt
 * capable therefore
 * @param thread - the thread to configure
 * @param priority - the thread prioty between [1-99] with 99 beeing the highest priority
 * @param cpu_coure - the cpu core to tie this thread to. Pass -1 to keep the default
 * @param scheduler - the scheduler type to choose for this thread. Reasonable options are [SCHED_FIFO, SCHED_RR]
 *
 * This function tries to make the passed thread more realtimy by doing three things:
 * 1. Configure the scheduler to a more realtime like scheduler. E.g. SCHED_FIFO
 * 2. Increase the priority of the thread
 * 3. Bind the thread to a specific core. This avoids overhead by moving the thread between cores (cache misses)
 *
 * @return true in case of success, false in case of error
 *
 * You will need to allow your current user + your container to set these settings
 */
bool set_realtime_priority(pthread_t thread, int priority = 60, int cpu_core = -1, int scheduler = SCHED_FIFO)
{
  if (priority < 1 || priority > 99) {
    logging::error() << "Failed to configure scheduler - pass a valid priority between [1,99]";
    return false;
  }

  // Configure the scheduler to the desired type and priority
  sched_param param;
  param.sched_priority = priority;
  if (auto ec = pthread_setschedparam(thread, scheduler, &param); ec != 0) {
    logging::error() << "Failed to configure scheduler (could not set priority and scheduler type) for thread: "
                     << thread << ". Check limits.conf or execute as root."
                     << " ec: " << ec;
    return false;
  }

  // Now try to attach the thread to a specific cpu core
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  const auto number_of_cpus = sysconf(_SC_NPROCESSORS_ONLN);

  // User did not want to attach thread to a cpu core
  if (cpu_core < 0) {
    return true;
  }

  if (cpu_core >= number_of_cpus) {
    logging::error() << "Failed to bind thread: " << thread << " to cpu core: " << cpu_core
                     << ". Please pass a valid cpu core (max: " << number_of_cpus << ")" << std::endl;
    return false;
  }

  CPU_SET(cpu_core, &cpuset);
  if (auto ec = pthread_setaffinity_np(thread, sizeof(cpuset), &cpuset); ec != 0) {
    logging::error() << "Failed to bind thread: " << thread << " to cpu core: " << cpu_core << ". ec: " << ec;
    return false;
  }
  return true;
}

/**
 * @brief set_realtime_priority - variant which configures the current thread
 * See set_realtime_priority with pthread parameter for the full explaination
 */
bool set_realtime_priority(int priority = 60, int cpu_core = -1, int scheduler = SCHED_FIFO)
{
  return set_realtime_priority(pthread_self(), priority, cpu_core, scheduler);
}
}  // namespace duatic::ethercat_interface
