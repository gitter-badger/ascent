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

// Thirteen pass, eigth-order Dormand-Prince algorithm

#include "ascent/core/StateStepper.h"

namespace asc
{
   class DOPRI87 : public StateStepper
   {
   public:
      DOPRI87(Stepper &stepper) : StateStepper(x, xd, stepper) {}
      DOPRI87(double &x, double &xd, Stepper &stepper) : StateStepper(x, xd, stepper) {}

      DOPRI87* factory(double &x, double &xd) { return new DOPRI87(x, xd, static_cast<Stepper&>(*this)); }

      void propagate();
      void updateClock();
      double optimalTimeStep();
      bool adaptive() { return true; }

      double t0;
      double xd0, xd1, xd2, xd3, xd4, xd5, xd6, xd7, xd8, xd9, xd10, xd11;
   };
}