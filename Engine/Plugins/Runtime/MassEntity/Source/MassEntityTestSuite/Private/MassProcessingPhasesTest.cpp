// Copyright Epic Games, Inc. All Rights Reserved.

#include "AITestsCommon.h"
#include "MassEntityManager.h"
#include "MassProcessingPhaseManager.h"
#include "MassEntityTestTypes.h"
#include "MassProcessingPhaseManager.h"

#define LOCTEXT_NAMESPACE "MassTest"

UE_DISABLE_OPTIMIZATION_SHIP

namespace FMassProcessingPhasesTest
{

/** Test-time TaskGraph task for triggering processing phases. */
struct FMassTestPhaseTickTask
{
	FMassTestPhaseTickTask(const TSharedRef<FMassProcessingPhaseManager>& InPhaseManager, const EMassProcessingPhase InPhase, const float InDeltaTime)
		: PhaseManager(InPhaseManager)
		, Phase(InPhase)
		, DeltaTime(InDeltaTime)
	{
	}

	static TStatId GetStatId()
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FMassTestPhaseTickTask, STATGROUP_TaskGraphTasks);
	}

	static ENamedThreads::Type GetDesiredThread() { return ENamedThreads::GameThread; }
	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FMassTestPhaseTickTask);

		PhaseManager->TriggerPhase(Phase, DeltaTime, MyCompletionGraphEvent);
	}
private:
	const TSharedRef<FMassProcessingPhaseManager> PhaseManager;
	const EMassProcessingPhase Phase = EMassProcessingPhase::MAX;
	const float DeltaTime = 0.f;
};


/** The main point of this FMassProcessingPhaseManager extension is to disable world-based ticking, even if a world is available. */
struct FMassTestProcessingPhaseManager : public FMassProcessingPhaseManager
{
	void Start(const TSharedPtr<FMassEntityManager>& InEntityManager)
	{
		EntityManager = InEntityManager;

		OnNewArchetypeHandle = EntityManager->GetOnNewArchetypeEvent().AddRaw(this, &FMassTestProcessingPhaseManager::OnNewArchetype);

		// at this point FMassProcessingPhaseManager would call EnableTickFunctions if a world was available
		// here we're skipping it on purpose

		bIsAllowedToTick = true;
	}

	void OnNewArchetype(const FMassArchetypeHandle& NewArchetype)
	{
		FMassProcessingPhaseManager::OnNewArchetype(NewArchetype);
	}
};


struct FProcessingPhasesTestBase : FEntityTestBase
{
	using Super = FEntityTestBase;
		
	TSharedPtr<FMassTestProcessingPhaseManager> PhaseManager;
	FMassProcessingPhaseConfig PhasesConfig[int(EMassProcessingPhase::MAX)];
	int32 TickIndex = -1;
	FGraphEventRef CompletionEvent;
	float DeltaTime = 1.f/30;

	FProcessingPhasesTestBase()
	{
		bMakeWorldEntityManagersOwner = true;
	}

	virtual bool SetUp() override
	{
		if (Super::SetUp())
		{
			PhaseManager = MakeShareable(new FMassTestProcessingPhaseManager());

			EntityManager->Initialize();

			if (PopulatePhasesConfig())
			{
				TickIndex = -1;

				UWorld* World = FAITestHelpers::GetWorld();
				check(World);

				PhaseManager->Initialize(*World, PhasesConfig);

				PhaseManager->Start(EntityManager);

				return true;
			}
		}

		return false;
	}

	virtual bool Update() override
	{
		if (CompletionEvent.IsValid())
		{
			CompletionEvent->Wait();
		}

		for (int PhaseIndex = 0; PhaseIndex < (int)EMassProcessingPhase::MAX; ++PhaseIndex)
		{
			const FGraphEventArray Prerequisites = { CompletionEvent };
			CompletionEvent = TGraphTask<FMassTestPhaseTickTask>::CreateTask(&Prerequisites)
				.ConstructAndDispatchWhenReady(PhaseManager.ToSharedRef(), EMassProcessingPhase(PhaseIndex), DeltaTime);
		}

		++TickIndex;
		return false;
	}

	virtual void TearDown() override
	{
		if (CompletionEvent.IsValid())
		{
			CompletionEvent->Wait();
		}

		PhaseManager->Stop();
		PhaseManager.Reset();
		Super::TearDown();
	}

	virtual void VerifyLatentResults() override
	{
		// we need to make sure all the phases are done before attempting to test results
		if (CompletionEvent.IsValid())
		{
			CompletionEvent->Wait();
		}
	}

	virtual bool PopulatePhasesConfig() = 0;
};

/** this test is here to make sure that the set up that other tests rely on actually works, i.e. the phases are getting ticked at all*/
struct FTestSetupTest : FProcessingPhasesTestBase
{
	using Super = FProcessingPhasesTestBase;

	virtual bool SetUp() override
	{
		if (Super::SetUp())
		{
			UMassTestStaticCounterProcessor::StaticCounter = -1;
			return true;
		}
		return false;
	}

	virtual bool PopulatePhasesConfig()
	{
		PhasesConfig[0].ProcessorCDOs.Add(GetMutableDefault<UMassTestStaticCounterProcessor>());
		return true;
	}

	virtual bool Update() override
	{
		Super::Update();
		return TickIndex >= 3;
	}

	virtual void VerifyLatentResults() override
	{
		Super::VerifyLatentResults();
		AITEST_EQUAL_LATENT("Expecting the UMassTestStaticCounterProcessor getting ticked as many times as the test ticked", UMassTestStaticCounterProcessor::StaticCounter, TickIndex);
	}
};
IMPLEMENT_AI_LATENT_TEST(FTestSetupTest, "System.Mass.ProcessingPhases.SetupTest");

} // FMassProcessingPhasesTest

UE_ENABLE_OPTIMIZATION_SHIP

#undef LOCTEXT_NAMESPACE

