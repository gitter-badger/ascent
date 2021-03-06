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

#include "ascent/core/Simulator.h"

#include "ascent/Module.h"
#include "ascent/integrators/RK4.h"

#include <assert.h>

using namespace asc;

std::map<std::string, std::shared_ptr<Module>> Simulator::tracking;

using namespace std;

Simulator::Simulator(size_t sim) : sim(sim), stepper(EPS, dtp, dt, t, t1, kpass, integrator_initialized)
{
   integrator = std::make_unique<RK4>(stepper);
}

bool Simulator::run(const double dt, const double tmax)
{
   tend = tmax;

   if (tend <= t)
      setError("The end time : " + to_string(tend) + " is less than or equal to the current time : " + to_string(t));

   if (modules.size() == 0)
      setError("There are no modules to run.");

   if (!error)
   {
      setup(dt);

      init();
   }

   while (!error)
   {
      event(tend);

      if (tickfirst)
      {
         if (tick0 && track_time) // If the very first tick of the simulation.
            t_hist.push_back(t);

         changeTimeStep();
         report();

         if (tick0) // tracker() must run after report(), but t_hist must be recorded before rpt(), thus tick0 checks are separated.
         {
            tracker();
            tick0 = false;
         }
      }

      update();

      tickfirst = false;

      if (sample())
      {
         if (integrator->adaptiveFSAL() && integrator_initialized)
            adaptiveCalc();
      }

      propagateStates();
      updateClock();

      if (sample())
      {
         if (track_time)
            t_hist.push_back(t);

         postcalc();

         check();

         runStoppers();

         if (stop_simulation || (t + EPS >= tend))
            ticklast = true;

         report();

         tracker();

         if (integrator->adaptive())
            adaptiveCalc();

         changeTimeStep();

         deleteModules();

         if (ticklast)
         {
            createFiles();
            break;
         }
      }

      reset();
   }
   
   directErase(true); // Specify that all DynamicMaps should use direct erasing since the simulation finished.

   phase = Phase::setup;

   if (error)
   {
      cerr << "Simulation was stopped due to an ERROR." << '\n';
      return false;
   }
   return true;
}

void Simulator::directErase(bool b)
{
   modules.direct_erase = b;

   inits.direct_erase = b;
   updates.direct_erase = b;
   postcalcs.direct_erase = b;
   checks.direct_erase = b;
   reports.direct_erase = b;
   resets.direct_erase = b;

   propagate.direct_erase = b;
}

void Simulator::setup(const double dt)
{
   phase = Phase::setup;

   if (trackers.size() > 0)
      track_time = true; // track time if parameters are tracked

   this->dt = dtp = dt; // sets base time step (dtp) and adjustable time step (dt)
   t1 = t + dt; // sets intended end time of next timestep
   kpass = 0;
   ticklast = false;
   tickfirst = true;
   directErase(false);
   stop_simulation = false;
}

void Simulator::init()
{
   phase = Phase::init;
   
   for (auto& p : inits)
   {
      p.second->callInit();

      if (error)
         break;
   }

   inits.erase(); // removes any modules that have been set for deletion
}

void Simulator::update()
{
   phase = Phase::update;

   for (auto& p : updates)
   {
      p.second->callUpdate();

      if (error)
         break;
   }

   updates.erase();

   for (auto& p : updates)
      p.second->update_run = false;
}

void Simulator::postcalc()
{
   phase = Phase::postcalc;

   for (auto& p : postcalcs)
   {
      p.second->callPostCalc();

      if (error)
         break;
   }

   postcalcs.erase();

   for (auto& p : postcalcs)
      p.second->postcalc_run = false;
}

void Simulator::check()
{
   phase = Phase::check;

   for (auto& p : checks)
   {
      p.second->callCheck();

      if (error)
         break;
   }

   checks.erase();

   for (auto& p : checks)
      p.second->check_run = false;
}

void Simulator::report()
{
   phase = Phase::report;

   for (auto& p : reports)
   {
      p.second->callReport();

      if (error)
         break;
   }

   reports.erase();

   for (auto& p : reports)
      p.second->report_run = false;
}

void Simulator::reset()
{
   phase = Phase::reset;

   for (auto& p : resets)
   {
      p.second->callReset();

      if (error)
         break;
   }

   resets.erase();

   for (auto& p : resets)
      p.second->reset_run = false;
}

void Simulator::tracker()
{
   phase = Phase::tracker;

   for (auto& p : trackers)
      p.second->tracker();
}

void Simulator::propagateStates()
{
   for (auto &p : propagate)
   {
      auto module = p.second;
      if (!module->frozen && !module->freeze_integration) // if neither is frozen then propagate
         module->propagateStates();
   }
}

void Simulator::updateClock()
{
   const double t_prev = t;

   integrator->updateClock();

   if (t >= (t_prev + EPS))
      time_advanced = true;
   else
      time_advanced = false;
}

void Simulator::adaptiveCalc()
{
   double dt_optimal = 1.0e9; // Start with huge step size to be reduced.
   bool optimal_found = false;
   for (auto &p : propagate)
   {
      auto module = p.second;
      if (!module->frozen && !module->freeze_integration)
      {
         for (State* state : module->states)
         {
            double computed = state->optimalTimeStep();
            if ((computed > 0.0) && (computed < dt_optimal))
            {
               dt_optimal = computed;
               optimal_found = true;
            }
         }
      }
   }

   if (optimal_found)
   {
      if (dt_optimal < EPS)
         dt_change = EPS;
      else
      {
         if (dt_optimal > 2.0*dtp) // Limit increasing the time step too abruptly
            dt_change = 2.0*dtp;
         else
            dt_change = dt_optimal;
      }
         
      change_dt = true;
   }
}

void Simulator::changeTimeStep()
{
   if (change_dt)
   {
      dt = dtp = dt_change;
      t1 = t + dt;
      change_dt = false;
   }
}

void Simulator::deleteModules()
{
   // IMPROVEMENT: Pass the previous size of the to_delete vector into this recursive function so that you don't try to assign nullptr to an already nullptr and waste time.

   size_t n = to_delete.size();
   if (n > 0)
   {
      for (size_t i = 0; i < n; ++i)
         to_delete[i] = nullptr;

      if (to_delete.size() > n) // if the to_delete vector has grown since removing modules
         deleteModules(); // call this function recurvisely until all shared_ptrs are nullptr

      if (to_delete.size() > 0) // could have been cleared from recursion
         to_delete.clear();
   }
}

bool Simulator::sample()
{
   if (kpass == 0)
      return true;
   return false;
}

bool Simulator::sample(double sdt) // only changes the timestep if the sample produces a time step less than the current time step
{
   if (!sample())
      return false; // if intermediate step
                    
   // calculate the end time if using the sample deltat (sdt)
   double n = floor((t + EPS) / sdt + 1); // number of sample time steps that have occurred + 1, rounded down to nearest whole number
   double ts = n * sdt; // number of time steps till next sample time, multiplied by the sample time step (sdt)
   if (ts < t1 - EPS)
      t1 = ts;

   dt = t1 - t;
   // check to see if it is time to sample
   // Note: the sample will always return true when t == 0.0
   if (t - ts + sdt < EPS)
      return true;
   else
      return false;
}

bool Simulator::event(double t_event)
{
   if (!sample())
      return false; // if intermediate step

   if (t_event < t1 - EPS && t_event >= t + EPS)
      t1 = t_event;

   dt = t1 - t;
   if (fabs(t_event - t) < EPS)
      return true;
   else
      return false;
}

void Simulator::integrationTolerance(double tolerance) // Set global adaptive step size tolerance
{
   for (auto& p : modules)
      p.second->integrationTolerance(tolerance);
}

void Simulator::createFiles()
{
   for (auto& p : tracking)
      p.second->outputTrack();
}

bool Simulator::setError(const std::string& description)
{
   error = true;
   error_descriptions.push_back(description);
   if (print_errors)
      cerr << "ERROR: " + description << '\n';
   return false;
}

void Simulator::runStoppers()
{
   std::vector<size_t> to_erase;

   for (size_t i = 0; i < stoppers.size(); ++i)
   {
      auto& s = stoppers[i];
      s->check();
      if (s->stoppers.size() == 0)
         to_erase.push_back(i);
   }

   for (auto i : to_erase)
      stoppers.erase(stoppers.begin() + i);
}