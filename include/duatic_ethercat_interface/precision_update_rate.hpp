#pragma once

#include <ctime>
#include <cstdint>
#include <chrono>

namespace duatic::ethercat_interface
{

namespace precision_timing
{
constexpr int64_t billion = 1000000000;
constexpr int64_t sleep_early_stop_ns = 50000;

inline bool timespec_smaller_than(const timespec& ts1, const timespec& ts2)
{
  return (ts1.tv_sec < ts2.tv_sec || (ts1.tv_sec == ts2.tv_sec && ts1.tv_nsec < ts2.tv_nsec));
}
inline int64_t timespec_diff_ns(const timespec& a, const timespec& b)
{
  return (int64_t)(a.tv_sec - b.tv_sec) * billion + (a.tv_nsec - b.tv_nsec);
}
inline struct timespec timespec_add_ns(timespec t, const int64_t ns)
{
  t.tv_sec += ns / billion;
  t.tv_nsec += ns % billion;

  if (t.tv_nsec >= billion) {
    t.tv_sec++;
    t.tv_nsec -= billion;
  } else if (t.tv_nsec < 0) {
    t.tv_sec--;
    t.tv_nsec += billion;
  }
  return t;
}

inline struct timespec now_monotonic()
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t;
}

inline void high_precision_sleep(timespec ts)
{
  timespec early = timespec_add_ns(ts, -sleep_early_stop_ns);
  clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &early, nullptr);

  // busy waiting for the remaining time
  timespec now;
  do {
    clock_gettime(CLOCK_MONOTONIC, &now);
  } while (timespec_smaller_than(now, ts));
}

}  // namespace precision_timing

class PrecisionUpdateRate
{
public:
  PrecisionUpdateRate(std::chrono::nanoseconds time_step) : time_step_ns_(time_step.count())
  {
    reset();
  }

  void reset()
  {
    sleep_end_ = precision_timing::now_monotonic();
  }
  bool step()
  {
    using namespace precision_timing;

    // advance fixed periodic deadline
    sleep_end_ = timespec_add_ns(sleep_end_, time_step_ns_);

    timespec now = now_monotonic();

    bool has_overrun = false;
    // we are late
    if (timespec_smaller_than(sleep_end_, now)) {
      has_overrun = true;
      overrun_count_ += 1;
      accumulated_delay_ns_ += timespec_diff_ns(now, sleep_end_);
      sleep_end_ = now;

      sleep_end_ = timespec_add_ns(sleep_end_, time_step_ns_);
    }

    high_precision_sleep(sleep_end_);

    return has_overrun;
  }

  int64_t accumulated_delay_ns() const
  {
    return accumulated_delay_ns_;
  }
  int64_t overrun_count() const
  {
    return overrun_count_;
  }

private:
  int64_t time_step_ns_;
  int64_t accumulated_delay_ns_{ 0 };
  uint64_t overrun_count_{ 0 };
  timespec sleep_end_;
};

}  // namespace duatic::ethercat_interface
