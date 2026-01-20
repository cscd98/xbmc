/*
 *  Copyright (C) 2017-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "Registry.h"

#include "WinEventsWayland.h"
#include "utils/log.h"

#include <wayland-client-protocol.h>

#ifdef TARGET_WEBOS
#include "platform/linux/WebOSTVPlatformConfig.h"
extern "C" {
  extern const wl_interface wl_webos_shell_interface;
  extern const wl_interface wl_webos_foreign_interface;
}
#endif

using namespace KODI::WINDOWING::WAYLAND;

namespace
{

void TryBind(wayland::registry_t& registry,
             wayland::proxy_t& target,
             std::uint32_t name,
             const std::string& interface,
             std::uint32_t minVersion,
             std::uint32_t maxVersion,
             std::uint32_t offeredVersion)
{
  if (interface.empty())
  {
    CLog::Log(LOGERROR, "TryBind: Empty interface name for global {}", name);
    return;
  }

  if (minVersion > maxVersion)
  {
    CLog::Log(LOGERROR, "TryBind: Invalid version range for {} (min={}, max={})",
              interface, minVersion, maxVersion);
    return;
  }

  if (offeredVersion < minVersion)
  {
    CLog::Log(LOGWARNING,
              "Not binding Wayland protocol {} because server has only version {} "
              "(need at least {})",
              interface, offeredVersion, minVersion);
    return;
  }

  auto bindVersion = std::min(maxVersion, offeredVersion);
  CLog::Log(LOGDEBUG, "Binding Wayland protocol {} version {} (server has version {})",
            interface, bindVersion, offeredVersion);

  try
  {
    registry.bind(name, target, bindVersion);
    if (!target)
    {
      CLog::Log(LOGWARNING, "TryBind: Bind returned invalid proxy for {}", interface);
    }
    else
    {
      CLog::Log(LOGDEBUG, "TryBind: Successfully bound {}", interface);
    }
  }
  catch (const std::exception& e)
  {
    CLog::Log(LOGERROR, "TryBind: Exception binding {}: {}", interface, e.what());
  }
}
}

CRegistry::CRegistry(CConnection& connection)
: m_connection{connection}
{
}

void CRegistry::RequestSingletonInternal(wayland::proxy_t& target, std::string const& interfaceName, std::uint32_t minVersion, std::uint32_t maxVersion, bool required)
{
  if (m_registry)
  {
    throw std::logic_error("Cannot request more binds from registry after binding has started");
  }
  m_singletonBinds.emplace(std::piecewise_construct, std::forward_as_tuple(interfaceName), std::forward_as_tuple(target, minVersion, maxVersion, required));
}

void CRegistry::RequestInternal(std::function<wayland::proxy_t()> constructor, const std::string& interfaceName, std::uint32_t minVersion, std::uint32_t maxVersion, AddHandler addHandler, RemoveHandler removeHandler)
{
  if (m_registry)
  {
    throw std::logic_error("Cannot request more binds from registry after binding has started");
  }
  m_binds.emplace(std::piecewise_construct, std::forward_as_tuple(interfaceName), std::forward_as_tuple(constructor, minVersion, maxVersion, addHandler, removeHandler));
}

void CRegistry::Bind()
{
  if (m_registry)
  {
    throw std::logic_error("Cannot start binding on registry twice");
  }

  // We want to block in this function until we have received the global interfaces
  // from the compositor - no matter whether the global event pump is running
  // or not.
  // If it is running, we have to take special precautions not to drop events between
  // the creation of the registry and attaching event handlers, so we create
  // an extra queue and use that to dispatch the singleton globals. Then
  // we switch back to the global queue for further dispatch of interfaces
  // added/removed dynamically.
#ifdef TARGET_WEBOS
  unsigned int webOSVersion = WebOSTVPlatformConfig::GetWebOSVersion();

  switch(webOSVersion)
  {
    case 1:
    case 2:
    {
      CLog::Log(LOGDEBUG, "Wayland connection: Using webOS compatibility mode (no event queues)");

      wl_display* display_c = m_connection.GetDisplay();

      // wl_display_get_registry is a macro on newer libwayland, use the actual function
      // On old libwayland: wl_proxy_marshal_constructor(display, WL_DISPLAY_GET_REGISTRY, &wl_registry_interface, NULL)
      wl_proxy* registry_proxy = wl_proxy_marshal_constructor(
          reinterpret_cast<wl_proxy*>(display_c),
          WL_DISPLAY_GET_REGISTRY,
          &wl_registry_interface,
          nullptr);

      wl_registry* registry_c = reinterpret_cast<wl_registry*>(registry_proxy);

      if (!registry_c)
        throw std::runtime_error("Failed to get wl_registry");

      // Set up C-style listener
      static const wl_registry_listener registry_listener = {
        // global callback
        [](void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
          CRegistry* self = static_cast<CRegistry*>(data);
          std::string iface_str(interface);

          CLog::LogF(LOGDEBUG, "webOS: Registry global: name={} interface='{}' version={}",
                    name, iface_str, version);

          {
            auto it = self->m_singletonBinds.find(iface_str);
            if (it != self->m_singletonBinds.end())
            {
              auto& bind = it->second;

              // Get the wl_interface
              const wl_interface* iface = nullptr;

              if (iface_str == "wl_compositor")
                iface = &wl_compositor_interface;
              else if (iface_str == "wl_shm")
                iface = &wl_shm_interface;
              else if (iface_str == "wl_seat")
                iface = &wl_seat_interface;
              else if (iface_str == "wl_output")
                iface = &wl_output_interface;
              else if (iface_str == "wl_data_device_manager")
                iface = &wl_data_device_manager_interface;
              else if (iface_str == "wl_shell")
                iface = &wl_shell_interface;
              else if (iface_str == "wl_subcompositor")
                iface = &wl_subcompositor_interface;
              else
              {
                // For optional/unknown interfaces, mark as not required and skip
                if (!bind.required)
                {
                  CLog::LogF(LOGDEBUG, "webOS: Skipping optional interface '{}'", iface_str);
                }
                else
                {
                  CLog::LogF(LOGWARNING, "webOS: Required interface '{}' not handled in webOS mode", iface_str);
                }
                return;
              }

              if (!iface)
                return;

              uint32_t bind_version = std::min(version, bind.maxVersion);
              if (version < bind.minVersion)
              {
                CLog::LogF(LOGWARNING, "webOS: Interface '{}' version {} < minimum {}",
                          iface_str, version, bind.minVersion);
                return;
              }

              // Use old libwayland API: wl_proxy_marshal_constructor for bind
              // WL_REGISTRY_BIND opcode is 0, args: name, interface_name, version, new_id
              wl_proxy* proxy_c = wl_proxy_marshal_constructor(
                  reinterpret_cast<wl_proxy*>(registry),
                  WL_REGISTRY_BIND,
                  iface,
                  name,
                  iface_str.c_str(),
                  bind_version,
                  nullptr);

              if (proxy_c)
              {
                // Wrap in waylandpp object as foreign
                bind.target = wayland::proxy_t(proxy_c, wayland::proxy_t::wrapper_type::foreign);
                CLog::LogF(LOGDEBUG, "webOS: Bound singleton '{}' version {}", iface_str, bind_version);
              }
              return;
            }
          }

          {
            auto it = self->m_binds.find(iface_str);
            if (it != self->m_binds.end())
            {
              auto& bind = it->second;

              const wl_interface* iface = nullptr;

              if (iface_str == "wl_output")
                iface = &wl_output_interface;
              else if (iface_str == "wl_seat")
                iface = &wl_seat_interface;
              else
              {
                CLog::LogF(LOGDEBUG, "webOS: Skipping unknown dynamic interface '{}'", iface_str);
                return;
              }

              if (!iface)
                return;

              uint32_t bind_version = std::min(version, bind.maxVersion);
              if (version < bind.minVersion)
                return;

              // Use old libwayland API
              wl_proxy* proxy_c = wl_proxy_marshal_constructor(
                  reinterpret_cast<wl_proxy*>(registry),
                  WL_REGISTRY_BIND,
                  iface,
                  name,
                  iface_str.c_str(),
                  bind_version,
                  nullptr);

              if (proxy_c)
              {
                wayland::proxy_t target(proxy_c, wayland::proxy_t::wrapper_type::foreign);
                self->m_boundNames.emplace(name, bind);
                bind.addHandler(name, std::move(target));
                CLog::LogF(LOGDEBUG, "webOS: Bound dynamic '{}' version {}", iface_str, bind_version);
              }
              return;
            }
          }
        },
        // global_remove callback
        [](void* data, wl_registry* registry, uint32_t name) {
          CRegistry* self = static_cast<CRegistry*>(data);
          auto it = self->m_boundNames.find(name);
          if (it != self->m_boundNames.end())
          {
            it->second.get().removeHandler(name);
            self->m_boundNames.erase(it);
          }
        }
      };

      wl_registry_add_listener(registry_c, &registry_listener, this);

      // Wrap for storage
      m_registry = wayland::registry_t(registry_c, wayland::proxy_t::wrapper_type::foreign);

      CLog::Log(LOGDEBUG, "Wayland connection: Waiting for global interfaces");

      wl_display_flush(display_c);
      wl_display_roundtrip(display_c);
      wl_display_roundtrip(display_c);

      CheckRequired();

      CLog::Log(LOGDEBUG, "Wayland connection: Waiting for global interfaces - registry completed");

      return;
    }
    case 3:
    {
      CLog::Log(LOGDEBUG, "Wayland connection: Using webOS v3 compatibility mode (limited event queues)");
      m_registry = m_connection.GetDisplay().get_registry();
      CLog::Log(LOGDEBUG, "webOS 3: Got registry pointer: {}", static_cast<void*>(m_registry));
      break;
    }
    default:
        break;
  }

  if (webOSVersion <= 3)
  {
    m_registry.on_global() = [this](std::uint32_t name,
                                    const std::string& interface,
                                    std::uint32_t version)
    {
      CLog::LogF(LOGDEBUG,
                "webOS: Registry global: name={} interface='{}' version={}",
                name, interface, version);

      // Handle singletons
      {
        auto it = m_singletonBinds.find(interface);
        if (it != m_singletonBinds.end())
        {
          auto& bind = it->second;
          CLog::LogF(LOGDEBUG, "webOS: Found singleton bind for '{}'", interface);

          TryBind(m_registry, bind.target, name, interface,
                  bind.minVersion, bind.maxVersion, version);

          if (bind.target)
          {
            CLog::LogF(LOGDEBUG,
                      "webOS: Singleton bind complete for '{}', target valid",
                      interface);
          }
          else
          {
            CLog::LogF(LOGWARNING,
                      "webOS: Singleton bind failed for '{}', target invalid",
                      interface);
          }
          return;
        }
      }

      // Handle multi binds
      {
        auto it = m_binds.find(interface);
        if (it != m_binds.end())
        {
          auto& bind = it->second;
          CLog::LogF(LOGDEBUG, "webOS: Found multi bind for '{}'", interface);

          wayland::proxy_t target{bind.constructor()};
          if (!target)
          {
            CLog::LogF(LOGWARNING,
                      "webOS: Constructor returned invalid proxy for '{}'",
                      interface);
            return;
          }

          TryBind(m_registry, target, name, interface,
                  bind.minVersion, bind.maxVersion, version);

          if (target)
          {
            CLog::LogF(LOGDEBUG,
                      "webOS: Binding succeeded, calling addHandler for '{}'",
                      interface);
            m_boundNames.emplace(name, bind);
            bind.addHandler(name, std::move(target));
            CLog::LogF(LOGDEBUG,
                      "webOS: addHandler completed for '{}'", interface);
          }
          else
          {
            CLog::LogF(LOGWARNING,
                      "webOS: Binding failed for '{}', target invalid",
                      interface);
          }
          return;
        }
      }

      CLog::LogF(LOGDEBUG,
                "webOS: No bind handler for interface '{}'", interface);
    };

    m_registry.on_global_remove() = [this](std::uint32_t name)
    {
      auto it = m_boundNames.find(name);
      if (it != m_boundNames.end())
      {
        it->second.get().removeHandler(name);
        m_boundNames.erase(it);
      }
    };

    CLog::Log(LOGDEBUG, "Wayland connection: Waiting for global interfaces");

    m_connection.GetDisplay().flush();
    int pendingEvents = m_connection.GetDisplay().dispatch_pending();
    CLog::Log(LOGDEBUG, "webOS: Dispatched {} pending events", pendingEvents);

    int eventsDispatched = m_connection.GetDisplay().roundtrip();
    CLog::Log(LOGDEBUG,
              "Wayland connection: First roundtrip complete, {} events dispatched",
              eventsDispatched);

    eventsDispatched = m_connection.GetDisplay().roundtrip();
    CLog::Log(LOGDEBUG,
              "Wayland connection: Second roundtrip complete, {} events dispatched",
              eventsDispatched);

    CheckRequired();
    return;
  }

#endif
  auto registryRoundtripQueue = m_connection.GetDisplay().create_queue();

  auto displayProxy = m_connection.GetDisplay().proxy_create_wrapper();
  displayProxy.set_queue(registryRoundtripQueue);

  m_registry = displayProxy.get_registry();

  m_registry.on_global() = [this](std::uint32_t name, const std::string& interface,
                                  std::uint32_t version) {
    {
      auto it = m_singletonBinds.find(interface);
      if (it != m_singletonBinds.end())
      {
        auto& bind = it->second;
        auto registryProxy = m_registry.proxy_create_wrapper();
        // Events on the bound global should always go to the main queue
        registryProxy.set_queue(wayland::event_queue_t());
        TryBind(registryProxy, bind.target, name, interface, bind.minVersion, bind.maxVersion, version);
        return;
      }
    }

    {
      auto it = m_binds.find(interface);
      if (it != m_binds.end())
      {
        auto& bind = it->second;
        wayland::proxy_t target{bind.constructor()};
        auto registryProxy = m_registry.proxy_create_wrapper();
        // Events on the bound global should always go to the main queue
        registryProxy.set_queue(wayland::event_queue_t());
        TryBind(registryProxy, target, name, interface, bind.minVersion, bind.maxVersion, version);
        if (target)
        {
          m_boundNames.emplace(name, bind);
          bind.addHandler(name, std::move(target));
        }
        return;
      }
    }
  };

  m_registry.on_global_remove() = [this] (std::uint32_t name)
  {
    auto it = m_boundNames.find(name);
    if (it != m_boundNames.end())
    {
      it->second.get().removeHandler(name);
      m_boundNames.erase(it);
    }
  };

  CLog::Log(LOGDEBUG, "Wayland connection: Waiting for global interfaces");
  m_connection.GetDisplay().roundtrip_queue(registryRoundtripQueue);
  CLog::Log(LOGDEBUG, "Wayland connection: Roundtrip complete");

  CheckRequired();

  // Now switch it to the global queue for further runtime binds
  m_registry.set_queue(wayland::event_queue_t());
  // Roundtrip extra queue one last time in case something got queued up there.
  // Do it on the event thread so it does not race/run in parallel with the
  // dispatch of newly arrived registry messages in the default queue.
  CWinEventsWayland::RoundtripQueue(registryRoundtripQueue);
}

void CRegistry::UnbindSingletons()
{
  for (auto& bind : m_singletonBinds)
  {
    bind.second.target.proxy_release();
  }
}

void CRegistry::CheckRequired()
{
  for (auto const& bind : m_singletonBinds)
  {
    if (bind.second.required && !bind.second.target)
    {
      throw std::runtime_error(std::string("Missing required ") + bind.first + " protocol");
    }
  }
}