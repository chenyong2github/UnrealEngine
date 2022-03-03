// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "AITestsCommon.h"
#include "GameplayTagsManager.h"
#include "Engine/World.h"
#include "MassExecutor.h"
#include "MassEntitySubsystem.h"
#include "SmartObjectSubsystem.h"
#include "SmartObjectComponent.h"
#include "SmartObjectTestTypes.h"

#define LOCTEXT_NAMESPACE "AITestSuite_SmartObjectsTest"

PRAGMA_DISABLE_OPTIMIZATION

namespace FSmartObjectTest
{
	// Helper struct to define some test tags
	struct FNativeGameplayTags : public FGameplayTagNativeAdder
	{
		FGameplayTag TestTag1;
		FGameplayTag TestTag2;
		FGameplayTag TestTag3;

		virtual void AddTags() override
		{
			UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
			TestTag1 = Manager.AddNativeGameplayTag(TEXT("Test.SmartObject.Tag1"));
			TestTag2 = Manager.AddNativeGameplayTag(TEXT("Test.SmartObject.Tag2"));
			TestTag3 = Manager.AddNativeGameplayTag(TEXT("Test.SmartObject.Tag3"));
		}

		FORCEINLINE static const FNativeGameplayTags& Get()
		{
			return StaticInstance;
		}
		static FNativeGameplayTags StaticInstance;
	};
	FNativeGameplayTags FNativeGameplayTags::StaticInstance;

struct FSmartObjectTestBase : FAITestBase
{
	FSmartObjectRequestFilter TestFilter;
	USmartObjectDefinition* Definition = nullptr;
	USmartObjectSubsystem* Subsystem = nullptr;
	TArray<USmartObjectComponent*> SOList;
	int32 NumCreatedSlots = 0;

	/** Callback that derived classes can override to tweak the SmartObjectDefinition before the runtime gets initialized */
	virtual bool SetupDefinition() { return true; }

	virtual bool SetUp() override
	{
		UWorld* World = FAITestHelpers::GetWorld();
		Subsystem = USmartObjectSubsystem::GetCurrent(World);
		if (Subsystem == nullptr)
		{
			return false;
		}

		// Setup main definition
		Definition = NewAutoDestroyObject<USmartObjectDefinition>();

		// Set activity tags
		Definition->SetActivityTags(FGameplayTagContainer(FNativeGameplayTags::Get().TestTag1));

		FSmartObjectSlotDefinition& FirstSlot = Definition->DebugAddSlot();
		FSmartObjectSlotDefinition& SecondSlot = Definition->DebugAddSlot();
		FSmartObjectSlotDefinition& ThirdSlot = Definition->DebugAddSlot();

		// Add some test behavior definition
		FirstSlot.BehaviorDefinitions.Add(NewAutoDestroyObject<USmartObjectTestBehaviorDefinition>());
		SecondSlot.BehaviorDefinitions.Add(NewAutoDestroyObject<USmartObjectTestBehaviorDefinition>());
		ThirdSlot.BehaviorDefinitions.Add(NewAutoDestroyObject<USmartObjectTestBehaviorDefinition>());

		// Add some test slot definition data
		FSmartObjectSlotTestDefinitionData DefinitionData;
		DefinitionData.SomeSharedFloat = 123.456f;
		FirstSlot.Data.Add(FInstancedStruct::Make(DefinitionData));
		SecondSlot.Data.Add(FInstancedStruct::Make(DefinitionData));
		ThirdSlot.Data.Add(FInstancedStruct::Make(DefinitionData));

		// Setup filter
		TestFilter.BehaviorDefinitionClass = USmartObjectTestBehaviorDefinition::StaticClass();

		// Allow derived classes to tweak the definitions before we initialize the runtime
		const bool DefinitionValid = SetupDefinition();
		if (!DefinitionValid)
		{
			return false;
		}

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
				NumCreatedSlots += Definition->GetSlots().Num();
			}
		}

#if WITH_SMARTOBJECT_DEBUG
		// Force runtime initialization
		Subsystem->DebugInitializeRuntime();
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
		Subsystem->DebugCleanupRuntime();
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

		// Find candidate
		const FSmartObjectRequestResult FindResult = Subsystem->FindSmartObject(Request);
		AITEST_TRUE("Result.IsValid()", FindResult.IsValid());
		AITEST_TRUE("Result.SmartObjectHandle.IsValid()", FindResult.SmartObjectHandle.IsValid());
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FFindSmartObject, "System.AI.SmartObjects.Find");

struct FFindMultipleSmartObjects : FSmartObjectTestBase
{
	virtual bool InstantTest() override
	{
		const FSmartObjectRequest Request(FBox(EForceInit::ForceInit).ExpandBy(FVector(HALF_WORLD_MAX), FVector(HALF_WORLD_MAX)), TestFilter);

		// Find all candidates
		TArray<FSmartObjectRequestResult> Results;
		Subsystem->FindSmartObjects(Request, Results);
		AITEST_EQUAL("Results.Num()", Results.Num(), NumCreatedSlots);
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FFindMultipleSmartObjects, "System.AI.SmartObjects.Find multiple");

struct FClaimAndReleaseSmartObject : FSmartObjectTestBase
{
	virtual bool InstantTest() override
	{
		const FSmartObjectRequest Request(FBox(EForceInit::ForceInit).ExpandBy(FVector(HALF_WORLD_MAX), FVector(HALF_WORLD_MAX)), TestFilter);

		// Find candidate
		const FSmartObjectRequestResult FirstFindResult = Subsystem->FindSmartObject(Request);
		AITEST_TRUE("Result.IsValid()", FirstFindResult.IsValid());
		AITEST_TRUE("Result.SmartObjectHandle.IsValid()", FirstFindResult.SmartObjectHandle.IsValid());

		// Gather all available candidates before claiming
		TArray<FSmartObjectSlotHandle> ResultsBeforeClaim;
		Subsystem->FindSlots(FirstFindResult.SmartObjectHandle, TestFilter, ResultsBeforeClaim);

		// Claim candidate
		const FSmartObjectClaimHandle ClaimHandle = Subsystem->Claim(FirstFindResult);
		AITEST_TRUE("ClaimHandle.IsValid()", ClaimHandle.IsValid());

		// Gather remaining available candidates
		TArray<FSmartObjectSlotHandle> ResultsAfterClaim;
		Subsystem->FindSlots(FirstFindResult.SmartObjectHandle, TestFilter, ResultsAfterClaim);
		AITEST_NOT_EQUAL("Number of available slots before and after a claim", ResultsBeforeClaim.Num(), ResultsAfterClaim.Num());

		// Release claimed candidate
		const bool bSuccess = Subsystem->Release(ClaimHandle);
		AITEST_TRUE("Release() return status", bSuccess);

		// Gather all available candidates after releasing
		TArray<FSmartObjectSlotHandle> ResultsAfterRelease;
		Subsystem->FindSlots(FirstFindResult.SmartObjectHandle, TestFilter, ResultsAfterRelease);
		AITEST_EQUAL("Number of available slots before claiming and after releasing", ResultsBeforeClaim.Num(), ResultsAfterRelease.Num());

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FClaimAndReleaseSmartObject, "System.AI.SmartObjects.Claim & Release");

struct FFindAfterClaimSmartObject : FSmartObjectTestBase
{
	virtual bool InstantTest() override
	{
		const FSmartObjectRequest Request(FBox(EForceInit::ForceInit).ExpandBy(FVector(HALF_WORLD_MAX), FVector(HALF_WORLD_MAX)), TestFilter);

		// Find first candidate
		const FSmartObjectRequestResult FirstFindResult = Subsystem->FindSmartObject(Request);
		AITEST_TRUE("Result.IsValid()", FirstFindResult.IsValid());
		AITEST_TRUE("Result.SmartObjectHandle.IsValid()", FirstFindResult.SmartObjectHandle.IsValid());

		// Claim first candidate
		const FSmartObjectClaimHandle FirstClaimHandle = Subsystem->Claim(FirstFindResult);
		AITEST_TRUE("ClaimHandle.IsValid() after first find result", FirstClaimHandle.IsValid());

		// Find second candidate
		const FSmartObjectRequestResult SecondFindResult = Subsystem->FindSmartObject(Request);
		AITEST_TRUE("Result.IsValid()", SecondFindResult.IsValid());
		AITEST_TRUE("Result.SmartObjectHandle.IsValid()", SecondFindResult.SmartObjectHandle.IsValid());
		AITEST_TRUE("Result.SlotHandle.IsValid()", SecondFindResult.SlotHandle.IsValid());
		AITEST_NOT_EQUAL("Result is expected to point to a different slot since first slot was claimed", FirstFindResult.SlotHandle, SecondFindResult.SlotHandle);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FFindAfterClaimSmartObject, "System.AI.SmartObjects.Find after Claim");

struct FDoubleClaimSmartObject : FSmartObjectTestBase
{
	virtual bool InstantTest() override
	{
		const FSmartObjectRequest Request(FBox(EForceInit::ForceInit).ExpandBy(FVector(HALF_WORLD_MAX), FVector(HALF_WORLD_MAX)), TestFilter);

		// Find candidate
		const FSmartObjectRequestResult PreClaimResult = Subsystem->FindSmartObject(Request);
		AITEST_TRUE("Result.IsValid()", PreClaimResult.IsValid());
		AITEST_TRUE("Result.SmartObjectHandle.IsValid()", PreClaimResult.SmartObjectHandle.IsValid());

		// Claim first candidate
		const FSmartObjectClaimHandle FirstHdl = Subsystem->Claim(PreClaimResult);
		AITEST_TRUE("ClaimHandle.IsValid() after first claim", FirstHdl.IsValid());

		// Claim first candidate again
		const FSmartObjectClaimHandle SecondHdl = Subsystem->Claim(PreClaimResult);
		AITEST_FALSE("ClaimHandle.IsValid() after second claim", SecondHdl.IsValid());

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FDoubleClaimSmartObject, "System.AI.SmartObjects.Double Claim");

struct FUseAndReleaseSmartObject : FSmartObjectTestBase
{
	virtual bool InstantTest() override
	{
		const FSmartObjectRequest Request(FBox(EForceInit::ForceInit).ExpandBy(FVector(HALF_WORLD_MAX), FVector(HALF_WORLD_MAX)), TestFilter);

		// Find candidate
		const FSmartObjectRequestResult PreClaimResult = Subsystem->FindSmartObject(Request);
		AITEST_TRUE("Result.IsValid()", PreClaimResult.IsValid());
		AITEST_TRUE("Result.SmartObjectHandle.IsValid()", PreClaimResult.SmartObjectHandle.IsValid());

		// Claim & Use candidate
		const FSmartObjectClaimHandle Hdl = Subsystem->Claim(PreClaimResult);
		AITEST_TRUE("ClaimHandle.IsValid()", Hdl.IsValid());

		// Use specific behavior
		const USmartObjectBehaviorDefinition* BehaviorDefinition = Subsystem->Use<USmartObjectBehaviorDefinition>(Hdl);
		AITEST_NOT_NULL("Behavior definition pointer", BehaviorDefinition);

		// Release candidate
		const bool bSuccess = Subsystem->Release(Hdl);
		AITEST_TRUE("Release() return status", bSuccess);

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
		AITEST_EQUAL("Number of registerd smart objects", SOList.Num(), ExpectedNumRegisteredObjects);

		// Find first candidate
		const FSmartObjectRequestResult FirstFindResult = Subsystem->FindSmartObject(Request);
		AITEST_TRUE("Result.IsValid()", FirstFindResult.IsValid());
		AITEST_TRUE("Result.SmartObjectHandle.IsValid()", FirstFindResult.SmartObjectHandle.IsValid());

		// Claim & Use first candidate
		const FSmartObjectClaimHandle FirstClaimHandle = Subsystem->Claim(FirstFindResult);
		AITEST_TRUE("ClaimHandle.IsValid() after first claim", FirstClaimHandle.IsValid());
		const USmartObjectBehaviorDefinition* FirstDefinition = Subsystem->Use<USmartObjectBehaviorDefinition>(FirstClaimHandle);
		AITEST_NOT_NULL("Behavior definition pointer", FirstDefinition);

		// Find second candidate
		const FSmartObjectRequestResult SecondFindResult = Subsystem->FindSmartObject(Request);
		AITEST_TRUE("Result.IsValid()", SecondFindResult.IsValid());
		AITEST_TRUE("Result.SmartObjectHandle.IsValid()", SecondFindResult.SmartObjectHandle.IsValid());
		AITEST_TRUE("Result.SlotHandle.IsValid()", SecondFindResult.SlotHandle.IsValid());
		AITEST_NOT_EQUAL("Result is expected to point to a different slot since first slot was claimed", FirstFindResult.SlotHandle, SecondFindResult.SlotHandle);

		// Claim & use second candidate
		const FSmartObjectClaimHandle SecondClaimHandle = Subsystem->Claim(SecondFindResult);
		AITEST_TRUE("ClaimHandle.IsValid() after second claim", SecondClaimHandle.IsValid());
		const USmartObjectBehaviorDefinition* SecondDefinition = Subsystem->Use<USmartObjectBehaviorDefinition>(SecondClaimHandle);
		AITEST_NOT_NULL("Behavior definition pointer", SecondDefinition);

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
		AITEST_TRUE("Result.IsValid()", FindResult.IsValid());
		AITEST_TRUE("Result.SmartObjectHandle.IsValid()", FindResult.SmartObjectHandle.IsValid());
		AITEST_TRUE("Result.SlotHandle.IsValid()", FindResult.SlotHandle.IsValid());

		const FSmartObjectSlotView SlotView = Subsystem->GetSlotView(FindResult);
		AITEST_TRUE("SlotHandle.IsValid()", SlotView.GetSlotHandle().IsValid());

		const FSmartObjectSlotTestDefinitionData* DefinitionData = SlotView.GetDefinitionDataPtr<FSmartObjectSlotTestDefinitionData>();
		AITEST_NOT_NULL("Data definition pointer (for cooldown)", DefinitionData);

		const FSmartObjectSlotTestRuntimeData* RuntimeData = SlotView.GetStateDataPtr<FSmartObjectSlotTestRuntimeData>();
		AITEST_NULL("Runtime data pointer", RuntimeData);

		// Claim
		const FSmartObjectClaimHandle ClaimHandle = Subsystem->Claim(FindResult);
		AITEST_TRUE("ClaimHandle.IsValid() after first claim", ClaimHandle.IsValid());

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
		AITEST_NOT_NULL("Runtime data pointer", RuntimeDataAfter);
		AITEST_EQUAL("Runtime data float from SlotView", RuntimeDataAfter->SomePerInstanceSharedFloat, SomeFloatConstant);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FSlotCustomData, "System.AI.SmartObjects.Slot custom data");

struct FActivityTagsMergingPolicy : FSmartObjectTestBase
{
	virtual bool SetupDefinition() override
	{
		if (!ensureMsgf(Definition->GetMutableSlots().Num() >= 2, TEXT("Expecting at least three slots")))
		{
			return false;
		}

		// Tags setup looks like:
		// Object:	ActivityTags:	TestTag1
		// Slot1:	ActivityTags:	TestTag2
		// Slot2:	ActivityTags:	TestTag1, TestTag2
		// Slot3:	ActivityTags:	---

		FSmartObjectSlotDefinition& FirstSlot = Definition->GetMutableSlots()[0];
		FSmartObjectSlotDefinition& SecondSlot = Definition->GetMutableSlots()[1];
		FirstSlot.ActivityTags.AddTag(FNativeGameplayTags::Get().TestTag2);
		SecondSlot.ActivityTags.AddTag(FNativeGameplayTags::Get().TestTag1);
		SecondSlot.ActivityTags.AddTag(FNativeGameplayTags::Get().TestTag2);
		return true;
	}
};

struct FActivityTagsMergingPolicyCombine : FActivityTagsMergingPolicy
{
	virtual bool SetupDefinition() override
	{
		if (!FActivityTagsMergingPolicy::SetupDefinition())
		{
			return false;
		}

		Definition->SetActivityTagsMergingPolicy(ESmartObjectTagMergingPolicy::Combine);
		return true;
	}

	virtual bool InstantTest() override
	{
#if WITH_SMARTOBJECT_DEBUG
		FSmartObjectRequest DefaultRequest(FBox(EForceInit::ForceInit).ExpandBy(FVector(HALF_WORLD_MAX), FVector(HALF_WORLD_MAX)), TestFilter);

		{
			// No activity requirements, should return registered slots
			TArray<FSmartObjectRequestResult> Results;
			Subsystem->FindSmartObjects(DefaultRequest, Results);
			AITEST_EQUAL("Results.Num() using 'Combine' policy with an empty query", Results.Num(), NumCreatedSlots);
		}
		{
			// Adding activity requirements to the query
			FSmartObjectRequest ModifiedRequest = DefaultRequest;
			ModifiedRequest.Filter.ActivityRequirements = FGameplayTagQuery::BuildQuery(
				FGameplayTagQueryExpression()
				.NoTagsMatch()
				.AddTag(FNativeGameplayTags::Get().TestTag1)
			);
			TArray<FSmartObjectRequestResult> Results;
			Subsystem->FindSmartObjects(ModifiedRequest, Results);
			// All slots inherit TestTag1 from parent object, so all invalid
			AITEST_EQUAL("Results.Num() using 'Combine' policy with NoMatch(TestTag1)", Results.Num(), 0);
		}
		{
			// Adding activity requirements to the query
			FSmartObjectRequest ModifiedRequest = DefaultRequest;
			ModifiedRequest.Filter.ActivityRequirements = FGameplayTagQuery::BuildQuery(
				FGameplayTagQueryExpression()
				.AnyTagsMatch()
				.AddTag(FNativeGameplayTags::Get().TestTag1)
				.AddTag(FNativeGameplayTags::Get().TestTag2)
			);
			TArray<FSmartObjectRequestResult> Results;
			Subsystem->FindSmartObjects(ModifiedRequest, Results);
			// (Slot 1 & 2 & 3) = 3 matching slots / object
			AITEST_EQUAL("Results.Num() using 'Combine' policy with AnyMatch(TestTag1, TestTag2)", Results.Num(), SOList.Num() * 3);
		}
		{
			// Adding activity requirements to the query
			FSmartObjectRequest ModifiedRequest = DefaultRequest;
			ModifiedRequest.Filter.ActivityRequirements = FGameplayTagQuery::BuildQuery(
				FGameplayTagQueryExpression()
				.AllTagsMatch()
				.AddTag(FNativeGameplayTags::Get().TestTag1)
				.AddTag(FNativeGameplayTags::Get().TestTag2)
			);
			TArray<FSmartObjectRequestResult> Results;
			Subsystem->FindSmartObjects(ModifiedRequest, Results);
			// (Slot 1 & 2) = 2 matching slots / object
			AITEST_EQUAL("Results.Num() using 'Combine' policy with AllMatch(TestTag1, TestTag2)", Results.Num(), SOList.Num() * 2);
		}
#endif // WITH_SMARTOBJECT_DEBUG

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FActivityTagsMergingPolicyCombine, "System.AI.SmartObjects.Merging policy 'Combine' on ActivityTags");

struct FActivityTagsMergingPolicyOverride : FActivityTagsMergingPolicy
{
	virtual bool SetupDefinition() override
	{
		if (!FActivityTagsMergingPolicy::SetupDefinition())
		{
			return false;
		}

		Definition->SetActivityTagsMergingPolicy(ESmartObjectTagMergingPolicy::Override);
		return true;
	}

	virtual bool InstantTest() override
	{
#if WITH_SMARTOBJECT_DEBUG
		FSmartObjectRequest DefaultRequest(FBox(EForceInit::ForceInit).ExpandBy(FVector(HALF_WORLD_MAX), FVector(HALF_WORLD_MAX)), TestFilter);

		{
			// No activity requirements, should return registered slots
			TArray<FSmartObjectRequestResult> Results;
			Subsystem->FindSmartObjects(DefaultRequest, Results);
			AITEST_EQUAL("Results.Num() using 'Override' policy with an empty query", Results.Num(), NumCreatedSlots);
		}
		{
			// Adding activity requirements to the query
			FSmartObjectRequest ModifiedRequest = DefaultRequest;
			ModifiedRequest.Filter.ActivityRequirements = FGameplayTagQuery::BuildQuery(
				FGameplayTagQueryExpression()
				.NoTagsMatch()
				.AddTag(FNativeGameplayTags::Get().TestTag1)
			);
			TArray<FSmartObjectRequestResult> Results;
			Subsystem->FindSmartObjects(ModifiedRequest, Results);
			// Slot 1 only has TestTag2 (Slot 2 has TestTag1 in override and Slot 3 inherits from parent) = 1 matching slots / object
			AITEST_EQUAL("Results.Num() using 'Override' policy with NoMatch(TestTag1)", Results.Num(), SOList.Num() * 1);
		}
		{
			// Adding activity requirements to the query
			FSmartObjectRequest ModifiedRequest = DefaultRequest;
			ModifiedRequest.Filter.ActivityRequirements = FGameplayTagQuery::BuildQuery(
				FGameplayTagQueryExpression()
				.AllTagsMatch()
				.AddTag(FNativeGameplayTags::Get().TestTag1)
				.AddTag(FNativeGameplayTags::Get().TestTag2)
			);
			TArray<FSmartObjectRequestResult> Results;
			Subsystem->FindSmartObjects(ModifiedRequest, Results);
			// (Slot 2) = 1 matching slot / object
			AITEST_EQUAL("Results.Num() using 'Override' policy with AllMatch(TestTag1, TestTag2)", Results.Num(), SOList.Num() * 1);
		}
#endif // WITH_SMARTOBJECT_DEBUG

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FActivityTagsMergingPolicyOverride, "System.AI.SmartObjects.Merging policy 'Override' on ActivityTags");

struct FUserTagsFilterPolicy : FSmartObjectTestBase
{
	virtual bool SetupDefinition() override
	{
		if (!ensureMsgf(Definition->GetMutableSlots().Num() >= 2, TEXT("Expecting at least three slots")))
		{
			return false;
		}

		// Tags setup looks like:
		// Object:	UserTagFilter:	Match(TestTag1)
		// Slot1:	UserTagFilter:	NoMatch(TestTag2)
		// Slot2:	UserTagFilter:	AnyMatch(TestTag1, TestTag2, TestTag3)
		// Slot3:	UserTagFilter:	---

		FSmartObjectSlotDefinition& FirstSlot = Definition->GetMutableSlots()[0];
		FSmartObjectSlotDefinition& SecondSlot = Definition->GetMutableSlots()[1];

		// Set first slot user tag filter
		FirstSlot.UserTagFilter = FGameplayTagQuery::BuildQuery(
			FGameplayTagQueryExpression()
			.NoTagsMatch()
			.AddTag(FNativeGameplayTags::Get().TestTag2)
		);

		// Set second slot user tag filter
		SecondSlot.UserTagFilter = FGameplayTagQuery::BuildQuery(
			FGameplayTagQueryExpression()
			.AnyTagsMatch()
			.AddTag(FNativeGameplayTags::Get().TestTag1)
			.AddTag(FNativeGameplayTags::Get().TestTag3)
		);


		// Set user tag filter
		Definition->SetUserTagFilter(FGameplayTagQuery::BuildQuery(
			FGameplayTagQueryExpression()
			.AllTagsMatch()
			.AddTag(FNativeGameplayTags::Get().TestTag1)
		));
		return true;
	}
};

struct FUserTagsFilterPolicyNoFilter : FUserTagsFilterPolicy
{
	virtual bool SetupDefinition() override
	{
		if (!FUserTagsFilterPolicy::SetupDefinition())
		{
			return false;
		}

		Definition->SetUserTagsFilteringPolicy(ESmartObjectTagFilteringPolicy::NoFilter);
		return true;
	}

	virtual bool InstantTest() override
	{
#if WITH_SMARTOBJECT_DEBUG

		FSmartObjectRequest DefaultRequest(FBox(EForceInit::ForceInit).ExpandBy(FVector(HALF_WORLD_MAX), FVector(HALF_WORLD_MAX)), TestFilter);
		{
			// Providing user tags to the query
			FSmartObjectRequest ModifiedRequest = DefaultRequest;
			ModifiedRequest.Filter.UserTags.AddTag(FNativeGameplayTags::Get().TestTag2);
			TArray<FSmartObjectRequestResult> Results;
			Subsystem->FindSmartObjects(ModifiedRequest, Results);
			AITEST_EQUAL("Results.Num() using 'NoFilter' policy with user tags = TestTag2", Results.Num(), NumCreatedSlots);
		}
#endif // WITH_SMARTOBJECT_DEBUG
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FUserTagsFilterPolicyNoFilter, "System.AI.SmartObjects.Filter policy 'NoFilter' on UserTags");

struct FUserTagsFilterPolicyCombine : FUserTagsFilterPolicy
{
	virtual bool SetupDefinition() override
	{
		if (!FUserTagsFilterPolicy::SetupDefinition())
		{
			return false;
		}

		Definition->SetUserTagsFilteringPolicy(ESmartObjectTagFilteringPolicy::Combine);
		return true;
	}

	virtual bool InstantTest() override
	{
#if WITH_SMARTOBJECT_DEBUG
		FSmartObjectRequest DefaultRequest(FBox(EForceInit::ForceInit).ExpandBy(FVector(HALF_WORLD_MAX), FVector(HALF_WORLD_MAX)), TestFilter);

		{
			// User tags are empty so none should be found
			TArray<FSmartObjectRequestResult> Results;
			Subsystem->FindSmartObjects(DefaultRequest, Results);
			AITEST_EQUAL("Results.Num() using 'Combine' policy with an empty query", Results.Num(), 0);
		}
		{
			// Add TestTag1 to user tags
			FSmartObjectRequest ModifiedRequest = DefaultRequest;
			ModifiedRequest.Filter.UserTags.AddTag(FNativeGameplayTags::Get().TestTag1);
			TArray<FSmartObjectRequestResult> Results;
			Subsystem->FindSmartObjects(ModifiedRequest, Results);
			// (Slot 1 & 2 & 3) = 3 matching slots / object
			AITEST_EQUAL("Results.Num() using 'Combine' policy with user tags = TestTag1", Results.Num(), SOList.Num() * 3);

			// Add TestTag2 to User tags so first slot should match
			ModifiedRequest.Filter.UserTags.AddTag(FNativeGameplayTags::Get().TestTag2);
			Results.Reset();
			Subsystem->FindSmartObjects(ModifiedRequest, Results);
			// (Slot 2 & 3) = 2 matching slots / object
			AITEST_EQUAL("Results.Num() using 'Combine' policy with user tags = TestTag1, TestTag2", Results.Num(), SOList.Num() * 2);
		}
		{
			// Add TestTag3 to User tags so first slot should match
			FSmartObjectRequest ModifiedRequest = DefaultRequest;
			ModifiedRequest.Filter.UserTags.AddTag(FNativeGameplayTags::Get().TestTag3);
			TArray<FSmartObjectRequestResult> Results;
			Subsystem->FindSmartObjects(ModifiedRequest, Results);
			AITEST_EQUAL("Results.Num() using 'Combine' policy with user tags = TestTag3", Results.Num(), 0);
		}
#endif // WITH_SMARTOBJECT_DEBUG
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FUserTagsFilterPolicyCombine, "System.AI.SmartObjects.Filter policy 'Combine' on UserTags");

struct FUserTagsFilterPolicyOverride : FUserTagsFilterPolicy
{
	virtual bool SetupDefinition() override
	{
		if (!FUserTagsFilterPolicy::SetupDefinition())
		{
			return false;
		}

		Definition->SetUserTagsFilteringPolicy(ESmartObjectTagFilteringPolicy::Override);
		return true;
	}

	virtual bool InstantTest() override
	{
#if WITH_SMARTOBJECT_DEBUG
		FSmartObjectRequest DefaultRequest(FBox(EForceInit::ForceInit).ExpandBy(FVector(HALF_WORLD_MAX), FVector(HALF_WORLD_MAX)), TestFilter);

		{
			// User tags are empty
			TArray<FSmartObjectRequestResult> Results;
			Subsystem->FindSmartObjects(DefaultRequest, Results);
			// (Slot 1) = 1 matching slots / object
			AITEST_EQUAL("Results.Num() using 'Override' policy with an empty query", Results.Num(), SOList.Num() * 1);
		}
		{
			// Add TestTag1 to user tags
			FSmartObjectRequest ModifiedRequest = DefaultRequest;
			ModifiedRequest.Filter.UserTags.AddTag(FNativeGameplayTags::Get().TestTag1);
			TArray<FSmartObjectRequestResult> Results;
			Subsystem->FindSmartObjects(ModifiedRequest, Results);
			// (Slot 1 & 2 & 3) = 3 matching slots / object
			AITEST_EQUAL("Results.Num() using 'Override' policy with user tags = TestTag1", Results.Num(), SOList.Num() * 3);

			// Add TestTag2 to User tags so first slot should match
			ModifiedRequest.Filter.UserTags.AddTag(FNativeGameplayTags::Get().TestTag2);
			Results.Reset();
			Subsystem->FindSmartObjects(ModifiedRequest, Results);
			// (Slot 2 & 3) = 2 matching slots / object
			AITEST_EQUAL("Results.Num() using 'Override' policy with user tags = TestTag1, TestTag2", Results.Num(), SOList.Num() * 2);
		}
		{
			// Add TestTag3 to User tags so first slot should match
			FSmartObjectRequest ModifiedRequest = DefaultRequest;
			ModifiedRequest.Filter.UserTags.AddTag(FNativeGameplayTags::Get().TestTag3);
			TArray<FSmartObjectRequestResult> Results;
			Subsystem->FindSmartObjects(ModifiedRequest, Results);
			// (Slot 1 & 2) = 2 matching slots / object
			AITEST_EQUAL("Results.Num() using 'Override' policy with user tags = TestTag3", Results.Num(), SOList.Num() * 2);
		}
#endif // WITH_SMARTOBJECT_DEBUG
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FUserTagsFilterPolicyOverride, "System.AI.SmartObjects.Filter policy 'Override' on UserTags");

} // namespace FSmartObjectTest

PRAGMA_ENABLE_OPTIMIZATION

#undef LOCTEXT_NAMESPACE
