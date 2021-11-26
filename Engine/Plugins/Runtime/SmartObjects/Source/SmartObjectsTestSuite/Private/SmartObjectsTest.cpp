// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "AITestsCommon.h"
#include "Engine/World.h"
#include "EngineDefines.h"
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

		// Setup definition
		USmartObjectDefinition* Definition = NewAutoDestroyObject<USmartObjectDefinition>();
		FSmartObjectSlot& Slot = Definition->DebugAddSlot();
		Slot.BehaviorDefinitions.Add(NewAutoDestroyObject<USmartObjectTestDefinition>());

		// Setup filter
		TestFilter.BehaviorDefinitionClass = USmartObjectTestDefinition::StaticClass();

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
		AITEST_TRUE("ID is expected to be valid", FindResult.SmartObjectID.IsValid());
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
		AITEST_TRUE("ID is expected to be valid", FirstFindResult.SmartObjectID.IsValid());

		// Claim object
		const FSmartObjectClaimHandle ClaimHandle = Subsystem->Claim(FirstFindResult);
		AITEST_TRUE("Claim Handle is expected to be valid", ClaimHandle.IsValid());

		// Try to find valid use in claimed object
		const FSmartObjectRequestResult FirstFindValidUseResult = Subsystem->FindSlot(FirstFindResult.SmartObjectID, TestFilter);
		AITEST_FALSE("Result is expected to be invalid since ID was claimed", FirstFindValidUseResult.IsValid());

		// Release claimed object
		const bool bSuccess = Subsystem->Release(ClaimHandle);
		AITEST_TRUE("Handle is expected to be unclaimed successfully", bSuccess);

		// Try to find valid use in unclaimed object
		const FSmartObjectRequestResult SecondFindValidUseResult = Subsystem->FindSlot(FirstFindResult.SmartObjectID, TestFilter);
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
		AITEST_TRUE("ID is expected to be valid", FirstFindResult.SmartObjectID.IsValid());

		// Claim first object
		const FSmartObjectClaimHandle FirstClaimHandle = Subsystem->Claim(FirstFindResult);
		AITEST_TRUE("Claim Handle is expected to be valid for the first claim", FirstClaimHandle.IsValid());

		// Find second object
		const FSmartObjectRequestResult SecondFindResult = Subsystem->FindSmartObject(Request);
		AITEST_TRUE("Result is expected to be valid", SecondFindResult.IsValid());
		AITEST_TRUE("ID is expected to be valid", SecondFindResult.SmartObjectID.IsValid());
		AITEST_NOT_EQUAL("Result is expected to point to a different ID since first ID was claimed", FirstFindResult.SmartObjectID, SecondFindResult.SmartObjectID);

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
		AITEST_TRUE("ID is expected to be valid", PreClaimResult.SmartObjectID.IsValid());

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
		AITEST_TRUE("ID is expected to be valid", PreClaimResult.SmartObjectID.IsValid());

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

		const uint32 ExpectedNumRegisteredObjects = 2;
		AITEST_EQUAL("Test expects only %s registerd smart objects", SOList.Num(), ExpectedNumRegisteredObjects);

		// Find first object
		const FSmartObjectRequestResult FirstFindResult = Subsystem->FindSmartObject(Request);
		AITEST_TRUE("Result is expected to be valid", FirstFindResult.IsValid());
		AITEST_TRUE("ID is expected to be valid", FirstFindResult.SmartObjectID.IsValid());

		// Claim & Use first object
		const FSmartObjectClaimHandle FirstClaimHandle = Subsystem->Claim(FirstFindResult);
		AITEST_TRUE("Claim Handle is expected to be valid for the first claim", FirstClaimHandle.IsValid());

		const USmartObjectBehaviorDefinition* FirstDefinition = Subsystem->Use<USmartObjectBehaviorDefinition>(FirstClaimHandle);
		AITEST_NOT_NULL("Behavior definition is expected to be valid", FirstDefinition);

		// Find second object
		const FSmartObjectRequestResult SecondFindResult = Subsystem->FindSmartObject(Request);
		AITEST_TRUE("Result is expected to be valid", SecondFindResult.IsValid());
		AITEST_TRUE("ID is expected to be valid", SecondFindResult.SmartObjectID.IsValid());
		AITEST_NOT_EQUAL("Result is expected to point to a different ID since first ID was claimed", FirstFindResult.SmartObjectID, SecondFindResult.SmartObjectID);

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

} // namespace FSmartObjectTest

PRAGMA_ENABLE_OPTIMIZATION

#undef LOCTEXT_NAMESPACE
