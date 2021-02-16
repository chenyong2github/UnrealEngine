// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UObject/ObjectHandle.h"

#if WITH_DEV_AUTOMATION_TESTS

class FObjectRefTrackingTestBase : public FAutomationTestBase
{
public:
	FObjectRefTrackingTestBase(const FString& InName, const bool bInComplexTask)
	: FAutomationTestBase(InName, bInComplexTask)
	{
	}

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
			return Test.TestEqual(What, OriginalNumResolves + ExpectedDelta, Test.GetNumResolves());
#else
			return true;
#endif
		}

		bool TestNumFailedResolves(const TCHAR* What, uint32 ExpectedDelta)
		{
#if UE_WITH_OBJECT_HANDLE_TRACKING
			return Test.TestEqual(What, OriginalNumFailedResolves + ExpectedDelta, Test.GetNumFailedResolves());
#else
			return true;
#endif
		}

		bool TestNumReads(const TCHAR* What, uint32 ExpectedDelta, bool bAllowAdditionalReads = false)
		{
#if UE_WITH_OBJECT_HANDLE_TRACKING
			if (bAllowAdditionalReads)
			{
				return Test.TestTrue(What, Test.GetNumReads() >= OriginalNumReads + ExpectedDelta);
			}
			else
			{
				return Test.TestEqual(What, OriginalNumReads + ExpectedDelta, Test.GetNumReads());
			}
#else
			return true;
#endif
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
#endif // WITH_DEV_AUTOMATION_TESTS
