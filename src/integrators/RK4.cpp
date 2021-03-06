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

#include "ascent/integrators/RK4.h"

using namespace asc;

void RK4::propagate()
{
   switch (kpass)
   {
   case 0:
      x0 = x;
      xd0 = xd;
      x = x0 + 0.5 * dt * xd0;
      break;
   case 1:
      xd1 = xd;
      x = x0 + 0.5 * dt * xd1;
      break;
   case 2:
      xd2 = xd;
      x = x0 + dt * xd2;
      break;
   case 3:
      xd3 = xd;
      x = x0 + dt / 6.0 * (xd0 + 2 * xd1 + 2 * xd2 + xd3);
      break;
   }
}

void RK4::updateClock()
{
   if (kpass == 0)
      t += 0.5*dt;
   else if (kpass == 2)
      t = t1;

   ++kpass;
   kpass = kpass % 4;

   if (kpass == 0)
      t1 = floor((t + EPS) / dtp + 1) * dtp;
}