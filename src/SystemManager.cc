/*
 * Copyright (C) 2022 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/

#include <ignition/common/StringUtils.hh>

#include "ignition/gazebo/components/SystemPluginInfo.hh"
#include "ignition/gazebo/Conversions.hh"
#include "SystemManager.hh"

using namespace ignition;
using namespace gazebo;

//////////////////////////////////////////////////
SystemManager::SystemManager(const SystemLoaderPtr &_systemLoader,
                             EntityComponentManager *_entityCompMgr,
                             EventManager *_eventMgr,
                             const std::string &_namespace)
  : systemLoader(_systemLoader),
    entityCompMgr(_entityCompMgr),
    eventMgr(_eventMgr)
{
  transport::NodeOptions opts;
  opts.SetNameSpace(_namespace);
  this->node = std::make_unique<transport::Node>(opts);
  std::string entitySystemAddService{"entity/system/add"};
  this->node->Advertise(entitySystemAddService,
      &SystemManager::EntitySystemAddService, this);
  ignmsg << "Serving entity system service on ["
         << "/" << entitySystemAddService << "]" << std::endl;

  std::string entitySystemInfoService{"entity/system/info"};
  this->node->Advertise(entitySystemInfoService,
      &SystemManager::EntitySystemInfoService, this);
}

//////////////////////////////////////////////////
void SystemManager::LoadPlugin(const Entity _entity,
                               const sdf::Plugin &_plugin)
{
  std::optional<SystemPluginPtr> system;
  {
    std::lock_guard<std::mutex> lock(this->systemLoaderMutex);
    system = this->systemLoader->LoadPlugin(_plugin);
  }

  // System correctly loaded from library
  if (system)
  {
    this->AddSystem(system.value(), _entity, _plugin.ToElement());
    igndbg << "Loaded system [" << _plugin.Name()
           << "] for entity [" << _entity << "]" << std::endl;
  }
}

//////////////////////////////////////////////////
size_t SystemManager::TotalCount() const
{
  return this->ActiveCount() + this->PendingCount();
}

//////////////////////////////////////////////////
size_t SystemManager::ActiveCount() const
{
  return this->systems.size();
}

//////////////////////////////////////////////////
size_t SystemManager::PendingCount() const
{
  std::lock_guard<std::mutex> lock(this->pendingSystemsMutex);
  return this->pendingSystems.size();
}

//////////////////////////////////////////////////
size_t SystemManager::ActivatePendingSystems()
{
  std::lock_guard<std::mutex> lock(this->pendingSystemsMutex);

  auto count = this->pendingSystems.size();

  for (const auto& system : this->pendingSystems)
  {
    this->systems.push_back(system);

    if (system.configure)
      this->systemsConfigure.push_back(system.configure);

    if (system.preupdate)
      this->systemsPreupdate.push_back(system.preupdate);

    if (system.update)
      this->systemsUpdate.push_back(system.update);

    if (system.postupdate)
      this->systemsPostupdate.push_back(system.postupdate);
  }

  this->pendingSystems.clear();
  return count;
}

//////////////////////////////////////////////////
void SystemManager::AddSystem(const SystemPluginPtr &_system,
      Entity _entity,
      std::shared_ptr<const sdf::Element> _sdf)
{
  this->AddSystemImpl(SystemInternal(_system, _entity), _sdf);
}

//////////////////////////////////////////////////
void SystemManager::AddSystem(
      const std::shared_ptr<System> &_system,
      Entity _entity,
      std::shared_ptr<const sdf::Element> _sdf)
{
  this->AddSystemImpl(SystemInternal(_system, _entity), _sdf);
}

//////////////////////////////////////////////////
void SystemManager::AddSystemImpl(
      SystemInternal _system,
      std::shared_ptr<const sdf::Element> _sdf)
{
  // Add component
  if (this->entityCompMgr && kNullEntity != _system.parentEntity)
  {
    msgs::Plugin_V systemInfoMsg;
    auto systemInfoComp =
        this->entityCompMgr->Component<components::SystemPluginInfo>(
        _system.parentEntity);
    if (systemInfoComp)
    {
      systemInfoMsg = systemInfoComp->Data();
    }
    if (_sdf)
    {
      auto pluginMsg = systemInfoMsg.add_plugins();
      pluginMsg->CopyFrom(convert<msgs::Plugin>(*_sdf.get()));
    }

    this->entityCompMgr->SetComponentData<components::SystemPluginInfo>(
        _system.parentEntity, systemInfoMsg);
    this->entityCompMgr->SetChanged(_system.parentEntity,
        components::SystemPluginInfo::typeId);
  }

  // Configure the system, if necessary
  if (_system.configure && this->entityCompMgr && this->eventMgr)
  {
    _system.configure->Configure(_system.parentEntity, _sdf,
                                 *this->entityCompMgr,
                                 *this->eventMgr);
  }

  // Update callbacks will be handled later, add to queue
  std::lock_guard<std::mutex> lock(this->pendingSystemsMutex);
  this->pendingSystems.push_back(_system);
}

//////////////////////////////////////////////////
const std::vector<ISystemConfigure *>& SystemManager::SystemsConfigure()
{
  return this->systemsConfigure;
}

//////////////////////////////////////////////////
const std::vector<ISystemPreUpdate *>& SystemManager::SystemsPreUpdate()
{
  return this->systemsPreupdate;
}

//////////////////////////////////////////////////
const std::vector<ISystemUpdate *>& SystemManager::SystemsUpdate()
{
  return this->systemsUpdate;
}

//////////////////////////////////////////////////
const std::vector<ISystemPostUpdate *>& SystemManager::SystemsPostUpdate()
{
  return this->systemsPostupdate;
}

//////////////////////////////////////////////////
std::vector<SystemInternal> SystemManager::TotalByEntity(Entity _entity)
{
  auto checkEntity = [&](const SystemInternal &_system)
      {
        return _system.parentEntity == _entity;
      };

  std::vector<SystemInternal> result;
  std::copy_if(this->systems.begin(), this->systems.end(),
      std::back_inserter(result), checkEntity);
  std::copy_if(this->pendingSystems.begin(), this->pendingSystems.end(),
      std::back_inserter(result), checkEntity);
  return result;
}

//////////////////////////////////////////////////
bool SystemManager::EntitySystemAddService(const msgs::EntityPlugin_V &_req,
                                           msgs::Boolean &_res)
{
  std::lock_guard<std::mutex> lock(this->systemsMsgMutex);
  this->systemsToAdd.push_back(_req);
  _res.set_data(true);
  return true;
}

//////////////////////////////////////////////////
bool SystemManager::EntitySystemInfoService(const msgs::Empty &,
                                            msgs::EntityPlugin_V &_res)
{
  // loop through all files in paths and populate the list of
  // plugin libraries.
  std::list<std::string> paths = this->systemLoader->PluginPaths();
  std::set<std::string> filenames;
  for (const auto &p : paths)
  {
    if (common::exists(p))
    {
      for (common::DirIter file(p);
          file != common::DirIter(); ++file)
      {
        std::string current(*file);
        std::string filename = common::basename(current);
        if (common::isFile(current) &&
            (common::EndsWith(filename, ".so") ||
             common::EndsWith(filename, ".dll") ||
             common::EndsWith(filename, ".dylib")))
        {
          // remove extension and lib prefix
          size_t extensionIndex = filename.rfind(".");
          std::string nameWithoutExtension =
              filename.substr(0, extensionIndex);
          if (common::StartsWith(nameWithoutExtension, "lib"))
          {
            nameWithoutExtension = nameWithoutExtension.substr(3);
          }
          filenames.insert(nameWithoutExtension);
        }
      }
    }
  }

  for (auto fn : filenames)
  {
    auto plugin = _res.add_plugins();
    plugin->set_filename(fn);
  }

  return true;
}

//////////////////////////////////////////////////
void SystemManager::ProcessPendingEntitySystems()
{
  std::lock_guard<std::mutex> lock(this->systemsMsgMutex);
  for (auto &req : this->systemsToAdd)
  {
    Entity entity = req.entity().id();

    if (req.plugins().empty())
    {
      ignwarn << "Unable to add plugins to Entity: '" << entity
              << "'. No plugins specified." << std::endl;
       continue;
    }

    for (auto &pluginMsg : req.plugins())
    {
      std::string fname = pluginMsg.filename();
      std::string name = pluginMsg.name();
      std::string innerxml = pluginMsg.innerxml();
      sdf::Plugin pluginSDF(fname, name, innerxml);
      this->LoadPlugin(entity, pluginSDF);
    }
  }
  this->systemsToAdd.clear();
}
