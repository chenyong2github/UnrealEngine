// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/AssertionMacros.h"

#if CHAOS_CHECKED
#define CHAOS_CHECK(Condition) check(Condition)
#define CHAOS_ENSURE(Condition) ensure(Condition)
#define CHAOS_ENSURE_MSG(InExpression, InFormat, ... ) ensureMsgf(InExpression, InFormat, ##__VA_ARGS__)
#define CHAOS_LOG(InLog, InSeverity, InFormat, ...) UE_LOG(InLog, InSeverity, InFormat, ##__VA_ARGS__)
#define CHAOS_CLOG(InExpression, InLog, InSeverity, InFormat, ...) UE_CLOG(InExpression, InLog, InSeverity, InFormat, ##__VA_ARGS__)
#else
#define CHAOS_CHECK(Condition) (!!(Condition))
#define CHAOS_ENSURE(Condition) (!!(Condition))
#define CHAOS_ENSURE_MSG(InExpression, InFormat, ... ) (!!(InExpression))
#define CHAOS_LOG(InLog, InSeverity, InFormat, ...)
#define CHAOS_CLOG(InExpression, InLog, InSeverity, InFormat, ...) (!!(InExpression))
#endif
