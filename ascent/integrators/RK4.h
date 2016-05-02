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

#pragma once

// Fourth order, four pass Runge Kutta

#include "ascent/core/StateStepper.h"

namespace asc
{
   class RK4 : public StateStepper
   {
   public:
      RK4(Stepper &stepper) : StateStepper(x, xd, stepper) {}
      RK4(double &x, double &xd, Stepper &stepper) : StateStepper(x, xd, stepper) {}

      RK4* factory(double &x, double &xd) { return new RK4(x, xd, static_cast<Stepper&>(*this)); }

      void propagate();
      void updateClock();

      double xd0, xd1, xd2, xd3;
   };
}