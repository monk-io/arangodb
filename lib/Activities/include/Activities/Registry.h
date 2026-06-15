////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2024 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Business Source License 1.1 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     https://github.com/arangodb/arangodb/blob/devel/LICENSE
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "Activities/ActivityHandle.h"
#include "Activities/ActivityId.h"
#include "Containers/Concurrent/metrics.h"
#include "Activities/Activity.h"

#include "Basics/ErrorT.h"
#include "Basics/Guarded.h"
#include "Containers/Concurrent/Registry.h"

#include "Inspection/Status.h"
#include "Registry.h"

#include <velocypack/SharedSlice.h>

#include <deque>
#include <memory>

namespace arangodb::activities {

struct ActivityPtr {
  std::shared_ptr<Activity> item;
  using Snapshot = Activity::Snapshot;

  auto snapshot() -> Snapshot {
    // TODO error when item == nullptr
    return item->snapshot();
  }
  auto set_to_deleted() -> void { item = nullptr;}
};

using ThreadRegistry = containers::ThreadRegistry<ActivityPtr>;
 
struct Registry : containers::Registry<ActivityPtr> {
  struct [[nodiscard]] ScopedCurrentlyExecutingActivity;

  static auto currentlyExecutingActivity() noexcept -> ActivityHandle {
    return _currentlyExecutingActivity;
  }
  static auto setCurrentlyExecutingActivity(ActivityHandle activity) noexcept
      -> void {
    _currentlyExecutingActivity = std::move(activity);
  }

  template<typename T, typename... Args>
  auto makeActivityWithParent(ActivityHandle parent, Args&&... args)
      -> T::HandleType {
    auto id = _activityIdCounter.fetch_add(1);
    struct ThreadRegistryGuard {
      ThreadRegistryGuard(Registry& registry)
          : _registry{ThreadRegistry::make(registry.get_metrics())} {
        registry.add(_registry);
      }

      std::shared_ptr<ThreadRegistry> _registry;
    };
    static thread_local auto registry_guard = ThreadRegistryGuard{*this};
    ThreadRegistry& registry = *registry_guard._registry; // &ThreadRegistry
    auto activity = std::make_shared<T>(id, std::move(parent), std::forward<Args>(args)...);
    registry.add([&]() { return ActivityPtr{.item=activity};});

    return activity;
  }
  template<typename T, typename... Args>
  auto makeActivity(Args&&... args) -> T::HandleType {
    return makeActivityWithParent<T>(_currentlyExecutingActivity,
                                     std::forward<Args>(args)...);
  }

  auto snapshot()
      -> errors::ErrorT<inspection::Status, velocypack::SharedSlice>;
  // auto size() -> size_t;

 private:

  static thread_local ActivityHandle _currentlyExecutingActivity;
  std::atomic<ActivityId> _activityIdCounter{0};
};

struct [[nodiscard]] Registry::ScopedCurrentlyExecutingActivity {
  explicit ScopedCurrentlyExecutingActivity(ActivityHandle activity) noexcept;
  ~ScopedCurrentlyExecutingActivity();

  ScopedCurrentlyExecutingActivity(ScopedCurrentlyExecutingActivity const&) =
      delete;
  ScopedCurrentlyExecutingActivity(ScopedCurrentlyExecutingActivity&&) = delete;
  auto operator=(ScopedCurrentlyExecutingActivity const&) = delete;
  auto operator=(ScopedCurrentlyExecutingActivity&&) = delete;

 private:
  ActivityHandle _oldExecutingActivity;
};

template<typename Func>
auto withCurrentlyExecutingActivity(Func&& func) {
  return [
    func = std::forward<Func>(func),
    activity = Registry::currentlyExecutingActivity()
  ]<typename... Args,
    typename = std::enable_if_t<std::is_invocable_v<Func, Args...>>>(
      Args && ... args) mutable {
    Registry::ScopedCurrentlyExecutingActivity guard(activity);
    return std::forward<Func>(func)(std::forward<Args>(args)...);
  };
}

}  // namespace arangodb::activities
