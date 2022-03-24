// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectRefTrackingTestBase.h"

#if WITH_DEV_AUTOMATION_TESTS

#if UE_WITH_OBJECT_HANDLE_TRACKING
ObjectHandleReferenceResolvedFunction* FObjectRefTrackingTestBase::PrevResolvedFunc = nullptr;
ObjectHandleReadFunction* FObjectRefTrackingTestBase::PrevReadFunc = nullptr;
#endif
thread_local uint32 FObjectRefTrackingTestBase::NumResolves = 0;
thread_local uint32 FObjectRefTrackingTestBase::NumFailedResolves = 0;
thread_local uint32 FObjectRefTrackingTestBase::NumReads = 0;

#endif // WITH_DEV_AUTOMATION_TESTS
