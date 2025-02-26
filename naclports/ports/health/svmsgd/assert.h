// -*- C++ -*-
// Simple assertions
// Copyright (C) 2007- Leon Bottou


// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA



#ifndef ASSERT_H
#define ASSERT_H 1

#include <iostream>
#include <cstdlib>
#undef assert
#define assertfail(msg) do { \
  std::cerr << "(" << __FILE__ << ":" << __LINE__ << ") " \
            << msg << std::endl; ::exit(10); } while(0)

#define assert(expr) \
  do { if (!(expr)) assertfail("Assertion failed: " << #expr); } while(0)

#endif
