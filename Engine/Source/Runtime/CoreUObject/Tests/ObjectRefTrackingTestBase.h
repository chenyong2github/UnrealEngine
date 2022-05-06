// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_LOW_LEVEL_TESTS

#include "TestHarness.h"

#include "CoreMinimal.h"
#include "UObject/ObjectHandle.h"
#include "Misc/ScopeLock.h"


class FObjectRefTrackingTestBase
{
public:
	uint32 GetNumResolves() const { return NumResolves; }
	uint32 GetNumFailedResolves() const { return NumFailedResolves; }
	uint32 GetNumReads() const { return NumReads; }

	struct FSnapshotObjectRefMetrics
	{
	public:
		FSnapshotObjectRefMetrics(FObjectRefTrackingTestBase& InTest)
		: Test(InTest)
		, OriginalNumResolves(Test.GetNumResolves())
		, OriginalNumFailedResolves(Test.GetNumFailedResolves())
		, OriginalNumReads(Test.GetNumReads())
		{
			Test.ConditionalInstallCallbacks();
		}

		bool TestNumResolves(const TCHAR* What, uint32 ExpectedDelta)
		{
#if UE_WITH_OBJECT_HANDLE_TRACKING
			bool bValue = OriginalNumResolves + ExpectedDelta == Test.GetNumResolves();
			TEST_TRUE(What, bValue);
			return bValue;
#endif
			return true;
		}

		bool TestNumFailedResolves(const TCHAR* What, uint32 ExpectedDelta)
		{
#if UE_WITH_OBJECT_HANDLE_TRACKING
			bool bValue = OriginalNumFailedResolves + ExpectedDelta == Test.GetNumFailedResolves();
			TEST_TRUE(What, bValue);
			return bValue;
#endif
			return true;

		}

		bool TestNumReads(const TCHAR* What, uint32 ExpectedDelta, bool bAllowAdditionalReads = false)
		{
#if UE_WITH_OBJECT_HANDLE_TRACKING
			bool bValue = false;
			if (bAllowAdditionalReads)
			{
				bValue = Test.GetNumReads() >= OriginalNumReads + ExpectedDelta;
				TEST_TRUE(What, bValue);
			}
			else
			{
				bValue = OriginalNumReads + ExpectedDelta == Test.GetNumReads();
				TEST_TRUE(What, bValue);
			}
			return bValue;
#endif
			return true;

		}
	private:
		FObjectRefTrackingTestBase& Test;
		uint32 OriginalNumResolves;
		uint32 OriginalNumFailedResolves;
		uint32 OriginalNumReads;
	};

private:
#if UE_WITH_OBJECT_HANDLE_TRACKING
	static void OnRefResolved(const FObjectRef& ObjectRef, UPackage* Pkg, UObject* Obj)
	{
		NumResolves++;
		if (!IsObjectRefNull(ObjectRef) && !Obj)
		{
			NumFailedResolves++;
		}

		if (PrevResolvedFunc)
		{
			PrevResolvedFunc(ObjectRef, Pkg, Obj);
		}
	}
	static void OnRefRead(UObject* Obj)
	{
		NumReads++;
		if (PrevReadFunc)
		{
			PrevReadFunc(Obj);
		}
	}
#endif
	
	static void ConditionalInstallCallbacks()
	{
		static bool bCallbacksInstalled = false;
		static FCriticalSection CallbackInstallationLock;

		if (bCallbacksInstalled)
		{
			return;
		}

		FScopeLock ScopeLock(&CallbackInstallationLock);
		if (!bCallbacksInstalled)
		{
#if UE_WITH_OBJECT_HANDLE_TRACKING
			PrevResolvedFunc = SetObjectHandleReferenceResolvedCallback(OnRefResolved);
			PrevReadFunc = SetObjectHandleReadCallback(OnRefRead);
#endif
			bCallbacksInstalled = true;
		}
	}

#if UE_WITH_OBJECT_HANDLE_TRACKING
	static ObjectHandleReferenceResolvedFunction* PrevResolvedFunc;
	static ObjectHandleReadFunction* PrevReadFunc;
#endif
	static thread_local uint32 NumResolves;
	static thread_local uint32 NumFailedResolves;
	static thread_local uint32 NumReads;
};

#endif