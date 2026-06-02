#include "Activities/GenericActivity.h"
#include "Activities/RegistryGlobalVariable.h"
#include "Logger/LogMacros.h"

#include <gtest/gtest.h>
#include <chrono>
#include <stop_token>
#include <thread>
#include <latch>
#include <list>
#include <format>

using namespace arangodb;
using namespace arangodb::activities;
using namespace std::chrono_literals;

struct ActivityPerformanceTest : ::testing::Test {};

// TODO fix Taktfrequenz
// TODO check sleep or do a busy wait
// TODO more threads / less sleep to see numbers in between
// TODO gc when there are a lot of long-living activites
TEST_F(ActivityPerformanceTest, bla) {
  std::latch latch{9};
  std::stop_source stop;
  std::list<std::jthread> threads;

  for (int i = 0; i < 8; i++) {
    threads.emplace_back(
        [&latch](std::stop_token token) {
          latch.arrive_and_wait();
          uint64_t creation_counter = 0;
          std::chrono::steady_clock::duration creation_duration;
          auto last = std::chrono::steady_clock::now();
          while (not token.stop_requested()) {
            auto before_creation = std::chrono::steady_clock::now();
            activities::make<activities::GenericActivity>(
                "activity", activities::GenericActivityData{});
            creation_duration +=
                std::chrono::steady_clock::now() - before_creation;
            creation_counter++;

            std::this_thread::sleep_for(5us);

            auto now = std::chrono::steady_clock::now();
            if (now - last >= 1s) {
              LOG_DEVEL << std::format(
                  std::locale("en_US.UTF-8"),
                  "#creations: {:L} - time for creation: {:L} - time per "
                  "creation: {:L}us",
                  creation_counter,
                  std::chrono::duration_cast<std::chrono::microseconds>(
                      creation_duration),
                  creation_duration.count() / creation_counter);
              creation_counter = 0;
              creation_duration = std::chrono::steady_clock::duration{};
              last = now;
            }
          }
        },
        stop.get_token());
  }

  threads.emplace_back(
      [&latch](std::stop_token token) {
        latch.arrive_and_wait();
        while (not token.stop_requested()) {
          auto start = std::chrono::steady_clock::now();
          registry.garbageCollect();
          auto duration = std::chrono::steady_clock::now() - start;
          LOG_DEVEL << std::format(
              std::locale("en_US.UTF-8"), "time for gc: {:L}",
              std::chrono::duration_cast<std::chrono::microseconds>(duration));

          std::this_thread::sleep_for(1s - duration);
        }
      },
      stop.get_token());
}
