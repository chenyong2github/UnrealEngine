// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "AITestsCommon.h"
#include "Engine/World.h"
#include "EngineDefines.h"
#include "MassExecutor.h"
#include "MassEntitySubsystem.h"
#include "SmartObjectSubsystem.h"
#include "SmartObjectComponent.h"
#include "SmartObjectTestTypes.h"

#define LOCTEXT_NAMESPACE "AITestSuite_SmartObjectsTest"

PRAGMA_DISABLE_OPTIMIZATION

namespace FSmartObjectTest
{

struct FSmartObjectTestBase : FAITestBase
{
	FSmartObjectRequestFilter TestFilter;
	USmartObjectSubsystem* Subsystem = nullptr;
	TArray<USmartObjectComponent*> SOList;

	virtual bool SetUp() override
	{
		UWorld* World = FAITestHelpers::GetWorld();
		Subsystem = USmartObjectSubsystem::GetCurrent(World);
		if (Subsystem == nullptr)
		{
			return false;
		}

		// Setup main definition
		USmartObjectDefinition* Definition = NewAutoDestroyObject<USmartObjectDefinition>();
		FSmartObjectSlotDefinition& Slot = Definition->DebugAddSlot();

		// Add some test behavior definition
		Slot.BehaviorDefinitions.Add(NewAutoDestroyObject<USmartObjectTestBehaviorDefinition>());

		// Add some test slot definition data
		FSmartObjectSlotTestDefinitionData DefinitionData;
		DefinitionData.SomeSharedFloat = 123.456f;
		Slot.Data.Add(FInstancedStruct::Make(DefinitionData));

		// Setup filter
		TestFilter.BehaviorDefinitionClass = USmartObjectTestBehaviorDefinition::StaticClass();

		// Create some smart objects
		SOList =
		{
			NewAutoDestroyObject<USmartObjectComponent>(World),
			NewAutoDestroyObject<USmartObjectComponent>(World)
		};

		// Register all to the subsystem
		for (USmartObjectComponent* SO : SOList)
		{
			if (SO != nullptr)
			{
				SO->SetDefinition(Definition);
				Subsystem->RegisterSmartObject(*SO);
			}
		}

#if WITH_SMARTOBJECT_DEBUG
		// Force registration to the runtime simulation
		Subsystem->DebugRegisterAllSmartObjects();
#endif

		if (UMassEntitySubsystem* System = UWorld::GetSubsystem<UMassEntitySubsystem>(World))
		{
			FMassProcessingContext ProcessingContext(*System, /* DeltaSeconds */ 0.f);
			UE::Mass::Executor::RunProcessorsView(TArrayView<UMassProcessor*>(), ProcessingContext);
		}

		return true;
	}

	virtual void TearDown() override
	{
		if (Subsystem == nullptr)
		{
			return;
		}

#if WITH_SMARTOBJECT_DEBUG
		// Force removal from the runtime simulation
		Subsystem->DebugUnregisterAllSmartObjects();
#endif

		// Unregister all from the current test
		for (USmartObjectComponent* SO : SOList)
		{
			if (SO != nullptr)
			{
				Subsystem->UnregisterSmartObject(*SO);
			}
		}

		FAITestBase::TearDown();
	}
};

struct FFindSmartObject : FSmartObjectTestBase
{
	virtual bool InstantTest() override
	{
		const FSmartObjectRequest Request(FBox(EForceInit::ForceInit).ExpandBy(FVector(HALF_WORLD_MAX), FVector(HALF_WORLD_MAX)), TestFilter);

		// Find object
		const FSmartObjectRequestResult FindResult = Subsystem->FindSmartObject(Request);
		AITEST_TRUE("Result is expected to be valid", FindResult.IsValid());
		AITEST_TRUE("ID is expected to be valid", FindResult.SmartObjectHandle.IsValid());
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FFindSmartObject, "System.AI.SmartObjects.Find");

struct FClaimAndReleaseSmartObject : FSmartObjectTestBase
{
	virtual bool InstantTest() override
	{
		const FSmartObjectRequest Request(FBox(EForceInit::ForceInit).ExpandBy(FVector(HALF_WORLD_MAX), FVector(HALF_WORLD_MAX)), TestFilter);

		// Find object
		const FSmartObjectRequestResult FirstFindResult = Subsystem->FindSmartObject(Request);
		AITEST_TRUE("Result is expected to be valid", FirstFindResult.IsValid());
		AITEST_TRUE("ID is expected to be valid", FirstFindResult.SmartObjectHandle.IsValid());

		// Claim object
		const FSmartObjectClaimHandle ClaimHandle = Subsystem->Claim(FirstFindResult);
		AITEST_TRUE("Claim Handle is expected to be valid", ClaimHandle.IsValid());

		// Try to find valid use in claimed object
		const FSmartObjectRequestResult FirstFindValidUseResult = Subsystem->FindSlot(FirstFindResult.SmartObjectHandle, TestFilter);
		AITEST_FALSE("Result is expected to be invalid since ID was claimed", FirstFindValidUseResult.IsValid());

		// Release claimed object
		const bool bSuccess = Subsystem->Release(ClaimHandle);
		AITEST_TRUE("Handle is expected to be unclaimed successfully", bSuccess);

		// Try to find valid use in unclaimed object
		const FSmartObjectRequestResult SecondFindValidUseResult = Subsystem->FindSlot(FirstFindResult.SmartObjectHandle, TestFilter);
		AITEST_TRUE("Result is expected to be valid since ID was released", SecondFindValidUseResult.IsValid());

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FClaimAndReleaseSmartObject, "System.AI.SmartObjects.Claim & Release");

struct FFindAfterClaimSmartObject : FSmartObjectTestBase
{
	virtual bool InstantTest() override
	{
		const FSmartObjectRequest Request(FBox(EForceInit::ForceInit).ExpandBy(FVector(HALF_WORLD_MAX), FVector(HALF_WORLD_MAX)), TestFilter);

		// Find first object
		const FSmartObjectRequestResult FirstFindResult = Subsystem->FindSmartObject(Request);
		AITEST_TRUE("Result is expected to be valid", FirstFindResult.IsValid());
		AITEST_TRUE("ID is expected to be valid", FirstFindResult.SmartObjectHandle.IsValid());

		// Claim first object
		const FSmartObjectClaimHandle FirstClaimHandle = Subsystem->Claim(FirstFindResult);
		AITEST_TRUE("Claim Handle is expected to be valid for the first claim", FirstClaimHandle.IsValid());

		// Find second object
		const FSmartObjectRequestResult SecondFindResult = Subsystem->FindSmartObject(Request);
		AITEST_TRUE("Result is expected to be valid", SecondFindResult.IsValid());
		AITEST_TRUE("ID is expected to be valid", SecondFindResult.SmartObjectHandle.IsValid());
		AITEST_NOT_EQUAL("Result is expected to point to a different ID since first ID was claimed", FirstFindResult.SmartObjectHandle, SecondFindResult.SmartObjectHandle);

		// Claim second object
		const FSmartObjectClaimHandle SecondClaimHandle = Subsystem->Claim(SecondFindResult);
		AITEST_TRUE("Claim Handle is expected to be valid for the second claim", SecondClaimHandle.IsValid());

		// Try to find an available object
		const FSmartObjectRequestResult ThirdFindResult = Subsystem->FindSmartObject(Request);
		AITEST_FALSE("Result is expected to be invalid: all objects are claimed", ThirdFindResult.IsValid());

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FFindAfterClaimSmartObject, "System.AI.SmartObjects.Find after Claim");

struct FDoubleClaimSmartObject : FSmartObjectTestBase
{
	virtual bool InstantTest() override
	{
		const FSmartObjectRequest Request(FBox(EForceInit::ForceInit).ExpandBy(FVector(HALF_WORLD_MAX), FVector(HALF_WORLD_MAX)), TestFilter);

		// Find object
		const FSmartObjectRequestResult PreClaimResult = Subsystem->FindSmartObject(Request);
		AITEST_TRUE("Result is expected to be valid", PreClaimResult.IsValid());
		AITEST_TRUE("ID is expected to be valid", PreClaimResult.SmartObjectHandle.IsValid());

		// Claim first object
		const FSmartObjectClaimHandle FirstHdl = Subsystem->Claim(PreClaimResult);
		AITEST_TRUE("Claim Handle is expected to be valid for the first claim", FirstHdl.IsValid());

		// Claim first object again
		const FSmartObjectClaimHandle SecondHdl = Subsystem->Claim(PreClaimResult);
		AITEST_FALSE("Claim Handle is expected to be invalid for the second claim", SecondHdl.IsValid());

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FDoubleClaimSmartObject, "System.AI.SmartObjects.Double Claim");

struct FUseAndReleaseSmartObject : FSmartObjectTestBase
{
	virtual bool InstantTest() override
	{
		const FSmartObjectRequest Request(FBox(EForceInit::ForceInit).ExpandBy(FVector(HALF_WORLD_MAX), FVector(HALF_WORLD_MAX)), TestFilter);

		// Find object
		const FSmartObjectRequestResult PreClaimResult = Subsystem->FindSmartObject(Request);
		AITEST_TRUE("Result is expected to be valid", PreClaimResult.IsValid());
		AITEST_TRUE("ID is expected to be valid", PreClaimResult.SmartObjectHandle.IsValid());

		// Claim & Use object
		const FSmartObjectClaimHandle Hdl = Subsystem->Claim(PreClaimResult);
		AITEST_TRUE("Claim Handle is expected to be valid", Hdl.IsValid());

		const USmartObjectBehaviorDefinition* BehaviorDefinition = Subsystem->Use<USmartObjectBehaviorDefinition>(Hdl);
		AITEST_NOT_NULL("Bahavior definition is expected to be valid", BehaviorDefinition);

		// Release object
		const bool bSuccess = Subsystem->Release(Hdl);
		AITEST_TRUE("Handle is expected to be released successfully", bSuccess);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FUseAndReleaseSmartObject, "System.AI.SmartObjects.Use & Release");

struct FFindAfterUseSmartObject : FSmartObjectTestBase
{
	virtual bool InstantTest() override
	{
		const FSmartObjectRequest Request(FBox(EForceInit::ForceInit).ExpandBy(FVector(HALF_WORLD_MAX), FVector(HALF_WORLD_MAX)), TestFilter);

		constexpr uint32 ExpectedNumRegisteredObjects = 2;
		AITEST_EQUAL("Test expects only %s registerd smart objects", SOList.Num(), ExpectedNumRegisteredObjects);

		// Find first object
		const FSmartObjectRequestResult FirstFindResult = Subsystem->FindSmartObject(Request);
		AITEST_TRUE("Result is expected to be valid", FirstFindResult.IsValid());
		AITEST_TRUE("ID is expected to be valid", FirstFindResult.SmartObjectHandle.IsValid());

		// Claim & Use first object
		const FSmartObjectClaimHandle FirstClaimHandle = Subsystem->Claim(FirstFindResult);
		AITEST_TRUE("Claim Handle is expected to be valid for the first claim", FirstClaimHandle.IsValid());

		const USmartObjectBehaviorDefinition* FirstDefinition = Subsystem->Use<USmartObjectBehaviorDefinition>(FirstClaimHandle);
		AITEST_NOT_NULL("Behavior definition is expected to be valid", FirstDefinition);

		// Find second object
		const FSmartObjectRequestResult SecondFindResult = Subsystem->FindSmartObject(Request);
		AITEST_TRUE("Result is expected to be valid", SecondFindResult.IsValid());
		AITEST_TRUE("ID is expected to be valid", SecondFindResult.SmartObjectHandle.IsValid());
		AITEST_NOT_EQUAL("Result is expected to point to a different ID since first ID was claimed", FirstFindResult.SmartObjectHandle, SecondFindResult.SmartObjectHandle);

		// Claim & use second object
		const FSmartObjectClaimHandle SecondClaimHandle = Subsystem->Claim(SecondFindResult);
		AITEST_TRUE("Claim Handle is expected to be valid for the second claim", SecondClaimHandle.IsValid());
		const USmartObjectBehaviorDefinition* SecondDefinition = Subsystem->Use<USmartObjectBehaviorDefinition>(SecondClaimHandle);
		AITEST_NOT_NULL("Behavior definition is expected to be valid", SecondDefinition);

		// Try to find a third one
		const FSmartObjectRequestResult ThirdFindResult = Subsystem->FindSmartObject(Request);
		AITEST_FALSE("Result is expected to be invalid; all objects are claimed", ThirdFindResult.IsValid());

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FFindAfterUseSmartObject, "System.AI.SmartObjects.Find after Use");

struct FSlotCustomData : FSmartObjectTestBase
{
	virtual bool InstantTest() override
	{
		const FSmartObjectRequest Request(FBox(EForceInit::ForceInit).ExpandBy(FVector(HALF_WORLD_MAX), FVector(HALF_WORLD_MAX)), TestFilter);

		// Find an object
		const FSmartObjectRequestResult FindResult = Subsystem->FindSmartObject(Request);
		AITEST_TRUE("Result is expected to be valid", FindResult.IsValid());
		AITEST_TRUE("ID is expected to be valid", FindResult.SmartObjectHandle.IsValid());
		AITEST_TRUE("SlotHandle is expected to be valid", FindResult.SlotHandle.IsValid());

		const FSmartObjectSlotView SlotView = Subsystem->GetSlotView(FindResult);
		AITEST_TRUE("Slot Handle is expected to be valid", SlotView.GetSlotHandle().IsValid());

		const FSmartObjectSlotTestDefinitionData* DefinitionData = SlotView.GetDefinitionDataPtr<FSmartObjectSlotTestDefinitionData>();
		AITEST_NOT_NULL("Data definition for cooldown is expected to be valid", DefinitionData);

		const FSmartObjectSlotTestRuntimeData* RuntimeData = SlotView.GetStateDataPtr<FSmartObjectSlotTestRuntimeData>();
		AITEST_NULL("Runtime data is expected to be not valid", RuntimeData);

		// Claim
		const FSmartObjectClaimHandle ClaimHandle = Subsystem->Claim(FindResult);
		AITEST_TRUE("Claim Handle is expected to be valid for the first claim", ClaimHandle.IsValid());

		// Add new data, note that this will invalidate the view...
		FSmartObjectSlotTestRuntimeData NewRuntimeData;
		constexpr float SomeFloatConstant = 654.321f;
		NewRuntimeData.SomePerInstanceSharedFloat = SomeFloatConstant;
		Subsystem->AddSlotDataDeferred(ClaimHandle, FConstStructView::Make(NewRuntimeData));

		// We need to run Mass to flush deferred commands
		if (UMassEntitySubsystem* System = UWorld::GetSubsystem<UMassEntitySubsystem>(FAITestHelpers::GetWorld()))
		{
			FMassProcessingContext ProcessingContext(*System, /* DeltaSeconds */ 0.f);
			UE::Mass::Executor::RunProcessorsView(TArrayView<UMassProcessor*>(), ProcessingContext);
		}

		// Fetch a fresh slot view
		const FSmartObjectSlotView SlotViewAfter = Subsystem->GetSlotView(ClaimHandle);
		const FSmartObjectSlotTestRuntimeData* RuntimeDataAfter = SlotViewAfter.GetStateDataPtr<FSmartObjectSlotTestRuntimeData>();
		AITEST_NOT_NULL("Runtime data is expected to be valid", RuntimeDataAfter);
		AITEST_EQUAL("Runtime data float from view is expected to be the same as the 'pushed' one", RuntimeDataAfter->SomePerInstanceSharedFloat, SomeFloatConstant);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FSlotCustomData, "System.AI.SmartObjects.Slot custom data");

} // namespace FSmartObjectTest

PRAGMA_ENABLE_OPTIMIZATION

#undef LOCTEXT_NAMESPACE
