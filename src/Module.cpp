// Copyright (c) 2015 - 2016 Anyar, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//      http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ascent/Module.h"
#include "ascent/core/ModuleCore.h"

#include "ascent/Link.h"

using namespace asc;
using namespace std;

std::map<std::string, Module*> ModuleCore::external;
std::map<size_t, Module*> ModuleCore::accessor;
std::map<size_t, std::unique_ptr<Simulator>> ModuleCore::simulators;

size_t Module::next_module_id = 0;

struct null_deleter { void operator()(void const *) const {} };

// ModuleCore
void ModuleCore::error(const size_t sim, const std::string& description)
{
   getSimulator(sim).setError(description);
}

Module& ModuleCore::getExternal(const std::string& name)
{
   return *external[name];
}

Module& ModuleCore::getModule(const size_t id)
{
   return *accessor[id];
}

Simulator& ModuleCore::getSimulator(const size_t sim)
{
   if (!simulators.count(sim))
      simulators.emplace(sim, std::make_unique<Simulator>(sim));

   return *simulators[sim];
}

// Module
Module::Module(size_t sim) : simulator(getSimulator(sim)),
   t(simulator.t), dt(simulator.dt),
   sim(sim),
   module_id(next_module_id),
   chai(simulator.chai),
   myself(this, null_deleter()),
   vars(simulator),
   external(ModuleCore::external)
{
   ++next_module_id;
   ModuleCore::accessor[module_id] = this;

   simulator.modules[module_id] = this;
   
#define ascNS Module
   ascVar(frozen)

   // SHOULD THESE BE ADDED BASED ON THE SIMULATOR PHASE?
   // THIS QUESTION IS REFERRING TO MODULES THAT ARE INSTATIATED WHILE THE SIMULATION IS RUNNING.
   // WHERE ARE YOU CONNECTING MODULES? THIS IS IMPORTANT AS TO WHEN INIT() IS CALLED.
   // init() could just automatically be called if the simulation is running.
   simulator.inits[module_id] = this;
   simulator.updates[module_id] = this;
   simulator.postcalcs[module_id] = this;
   simulator.checks[module_id] = this;
   simulator.reports[module_id] = this;
   simulator.resets[module_id] = this;
}

Module::~Module()
{
   for (State* state : states)
      delete state;

   ModuleCore::accessor.erase(module_id);

   if (ModuleCore::external.count(module_name))
      ModuleCore::external.erase(module_name);

   // Pointers shouldn't be deleted because they are to this class:

   // directErase is used here because when a module is destroyed all pointers to the object must be removed now and can't be delayed.
   // Removing pointers is delayed while modules are still alive so that erasing can be handled while looping.

   // The module is garunteed to be erasable:
   simulator.modules.directErase(module_id);

   if (simulator.inits.count(module_id))
      simulator.inits.directErase(module_id);

   if (simulator.updates.count(module_id))
      simulator.updates.directErase(module_id);

   if (simulator.postcalcs.count(module_id))
      simulator.postcalcs.directErase(module_id);

   if (simulator.checks.count(module_id))
      simulator.checks.directErase(module_id);

   if (simulator.reports.count(module_id))
      simulator.reports.directErase(module_id);

   if (simulator.resets.count(module_id))
      simulator.resets.directErase(module_id);

   if (simulator.propagate.count(module_id))
      simulator.propagate.directErase(module_id);

   if (simulator.trackers.count(module_id))
      simulator.trackers.directErase(module_id);

   if (simulator.modules.size() == 0) // erase the simulator if there are no more modules
      ModuleCore::simulators.erase(sim);
}

std::string Module::name() const
{
   if ("" == module_name)
      module_name = "<" + to_string(module_id) + ", " + to_string(sim) + ">";

   return module_name;
}

void Module::addIntegrator(double &x, double &xd, const double tolerance)
{
   if (!simulator.propagate.count(module_id)) // if no integrators have been added (i.e. this module hasn't been added to be propagated)
      simulator.propagate[module_id] = this;

   states.push_back(simulator.integrator->factory(x, xd));
   states.back()->tolerance = tolerance;
}

void Module::propagateStates()
{
   for (State* state : states)
      state->propagate();
}

void Module::callInit()
{
   if (!init_run)
   {
      if (init_called)
      {
         error("Module: Circular dependency for init().");
         init_run = true;
         init_called = false;
         return;
      }

      init_called = true;
      if (!frozen)
         init();
      init_run = true;
      init_called = false;
      simulator.inits.erase(module_id); // Erasing module from inits because it should only be called once.
   }
}

void Module::callUpdate()
{
   if (!update_run)
   {
      bool update_now = true; // Whether or not this update() method should be run now, this will be set to false if the module needs to wait so that another module can update first.

      for (auto& p : run_first)
      {
         if (auto ptr = p.second.lock()) // auto& not supported by Xcode libc++ compiler when last tested
         {
            // If the run_first map contains an updating module, then we shouldn't update this module yet.
            if (ptr->update_called)
               update_now = false;
            else if (!ptr->update_run)
            {
               ptr->callUpdate();
               if (!ptr->update_run) // If the call to update didn't update the module, then this module cannot update yet.
                  update_now = false;
            }
         }
         else
            run_first.erase(p.first);
      }

      if (update_now)
      {
         if (update_called)
         {
            error("Circular dependency for update().");
            update_run = true;
            update_called = false;
            return;
         }

         update_called = true;

         if (!frozen)
            update();
         update_run = true;
         update_called = false;
      }
   }
}

void Module::callPostCalc()
{
   if (!postcalc_run)
   {
      bool postcalc_now = true;

      for (auto& p : run_first)
      {
         if (auto ptr = p.second.lock()) // auto& not supported by Xcode libc++ compiler when last tested
         {
            if (ptr->postcalc_called)
               postcalc_now = false;
            else if (!ptr->postcalc_run)
            {
               ptr->callPostCalc();
               if (!ptr->postcalc_run)
                  postcalc_now = false;
            }
         }
         else
            run_first.erase(p.first);
      }

      if (postcalc_now)
      {
         if (postcalc_called)
         {
            error("Circular dependency for postcalc().");
            postcalc_run = true;
            postcalc_called = false;
            return;
         }

         postcalc_called = true;

         if (!frozen)
            postcalc();
         postcalc_run = true;
         postcalc_called = false;
      }
   }
}

void Module::callCheck()
{
   if (!check_run)
   {
      if (check_called)
      {
         error("Circular dependency for check().");
         check_run = true;
         check_called = false;
         return;
      }

      check_called = true;
      if (!frozen)
         check();
      check_run = true;
      check_called = false;
   }
}

void Module::callReport()
{
   if (!report_run)
   {
      if (report_called)
      {
         error("Circular dependency for report().");
         report_run = true;
         report_called = false;
         return;
      }

      report_called = true;
      if (!frozen)
         report();
      report_run = true;
      report_called = false;
   }
}

void Module::callReset()
{
   if (!reset_run)
   {
      if (reset_called)
      {
         error("Circular dependency for reset().");
         reset_run = true;
         reset_called = false;
         return;
      }

      reset_called = true;
      if (!frozen)
         reset();
      reset_run = true;
      reset_called = false;
   }
}

// Tracking
void Module::track(const std::string& var_name)
{
   if ("t" == var_name)
      track_time = true;
   else
   {
      tracking.push_back(std::make_pair(module_id, var_name));
      steps(var_name);
   }
}

void Module::track(Module& module, const std::string& var_name)
{
   if ("t" == var_name)
      track_time = true;
   else
   {
      tracking.push_back(std::make_pair(module.module_id, var_name));
      ModuleCore::getModule(module.module_id).steps(var_name);
   }
}

void Module::outputTrack()
{
   ofstream file;
   string filename = module_directory + module_name + ".txt";
   file.open(filename);

   if (file)
   {
      size_t id = tracking.front().first;
      string var_name = tracking.front().second;
      
      size_t length;
      if (id == module_id)
         length = vars.length(var_name);
      else
         length = ModuleCore::getModule(id).vars.length(var_name);

      if (track_time)
         file << "t" << ", ";

      size_t n = tracking.size();
      for (size_t i = 0; i < n; ++i)
      {
         auto& p = tracking[i];
         if (i == n - 1) // last parameter
            file << ModuleCore::getModule(p.first).name() << " " << p.second;
         else
            file << ModuleCore::getModule(p.first).name() << " " << p.second << ", ";
      }

      file << '\n';

      for (size_t i = 0; i < length; ++i)
      {
         if (track_time)
            file << simulator.t_hist[i] << ", ";

         n = tracking.size();
         for (size_t j = 0; j < n; ++j)
         {
            auto& p = tracking[j];
            file << ModuleCore::getModule(p.first).vars.print(p.second, i);
            if (j < n - 1) // not the last parameter
               file << ", ";
         }

         file << '\n';
      }
   }
   else
      error("File <" + filename + "> could not be created.");
   
   file.close();
}

Simulator& Module::getSimulator(const size_t sim)
{
   return ModuleCore::getSimulator(sim);
}

void integrationTolerance(size_t sim, const double tolerance)
{
   ModuleCore::getSimulator(sim).integrationTolerance(tolerance);
}