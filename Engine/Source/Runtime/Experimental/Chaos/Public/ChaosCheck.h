// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/AssertionMacros.h"

#if CHAOS_CHECKED
#define CHAOS_CHECK(Condition) check(Condition)
#else
#define CHAOS_CHECK(Condition)
#endif
