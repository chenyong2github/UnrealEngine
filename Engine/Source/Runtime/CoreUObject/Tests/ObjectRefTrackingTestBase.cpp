// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_LOW_LEVEL_TESTS

#include "ObjectRefTrackingTestBase.h"

thread_local uint32 FObjectRefTrackingTestBase::NumResolves = 0;
thread_local uint32 FObjectRefTrackingTestBase::NumFailedResolves = 0;
thread_local uint32 FObjectRefTrackingTestBase::NumReads = 0;

#endif