// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/UMGSequenceTickManager.h"
#include "Animation/UMGSequencePlayer.h"
#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "Framework/Application/SlateApplication.h"

DECLARE_CYCLE_STAT(TEXT("Flush End of Frame Animations"), MovieSceneEval_FlushEndOfFrameAnimations, STATGROUP_MovieSceneEval);

static TAutoConsoleVariable<int32> CVarUMGMaxAnimationLatentActions(
	TEXT("Widget.MaxAnimationLatentActions"),
	100,
	TEXT("Defines the maximum number of latent actions that can be run in one frame."),
	ECVF_Default
);
int32 GFlushUMGAnimationsAtEndOfFrame = 1;
static FAutoConsoleVariableRef CVarUMGAnimationsAtEndOfFrame(
	TEXT("UMG.FlushAnimationsAtEndOfFrame"),
	GFlushUMGAnimationsAtEndOfFrame,
	TEXT("Whether to automatically flush any outstanding animations at the end of the frame, or just wait until next frame."),
	ECVF_Default
);

extern TAutoConsoleVariable<bool> CVarUserWidgetUseParallelAnimation;

UUMGSequenceTickManager::UUMGSequenceTickManager(const FObjectInitializer& Init)
	: Super(Init)
	, bIsTicking(false)
{
}

void UUMGSequenceTickManager::AddWidget(UUserWidget* InWidget)
{
	TWeakObjectPtr<UUserWidget> WeakWidget = InWidget;
	WeakUserWidgets.Add(WeakWidget);
}

void UUMGSequenceTickManager::RemoveWidget(UUserWidget* InWidget)
{
	TWeakObjectPtr<UUserWidget> WeakWidget = InWidget;
	WeakUserWidgets.Remove(WeakWidget);
}

void UUMGSequenceTickManager::BeginDestroy()
{
	if (SlateApplicationPreTickHandle.IsValid())
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication& SlateApp = FSlateApplication::Get();
			SlateApp.OnPreTick().Remove(SlateApplicationPreTickHandle);
			SlateApplicationPreTickHandle.Reset();

			SlateApp.OnPostTick().Remove(SlateApplicationPostTickHandle);
			SlateApplicationPostTickHandle.Reset();
		}
	}

	Super::BeginDestroy();
}

void UUMGSequenceTickManager::TickWidgetAnimations(float DeltaSeconds)
{
	if (!CVarUserWidgetUseParallelAnimation.GetValueOnGameThread())
	{
		return;
	}

	if (bIsTicking)
	{
		return;
	}

	TGuardValue<bool> IsTickingGuard(bIsTicking, true);

	// Tick all animations in all active widgets.
	//
	// In the main code path (the one where animations are just chugging along), the UMG sequence players
	// will queue evaluations on the global sequencer ECS linker. In some specific cases, though (pausing,
	// stopping, etc.), we might see some blocking (immediate) evaluations running here.
	//

	{
	#if STATS || ENABLE_STATNAMEDEVENTS
		const bool bShouldTrackObject = Stats::IsThreadCollectingData();
		FScopeCycleCounterUObject ContextScope(bShouldTrackObject ? this : nullptr);
	#endif

		for (auto WidgetIter = WeakUserWidgets.CreateIterator(); WidgetIter; ++WidgetIter)
		{
			UUserWidget* UserWidget = WidgetIter->Get();
			if (!UserWidget)
			{
				WidgetIter.RemoveCurrent();
			}
			else if (!UserWidget->IsConstructed())
			{
				UserWidget->TearDownAnimations();
				UserWidget->AnimationTickManager = nullptr;

				WidgetIter.RemoveCurrent();
			}
			else
			{
	#if STATS || ENABLE_STATNAMEDEVENTS
				FScopeCycleCounterUObject WidgetContextScope(bShouldTrackObject ? UserWidget : nullptr);
	#endif

	#if WITH_EDITOR
				const bool bTickAnimations = !UserWidget->IsDesignTime();
	#else
				const bool bTickAnimations = true;
	#endif
				if (bTickAnimations && UserWidget->IsVisible())
				{
					UserWidget->TickActionsAndAnimation(DeltaSeconds);
				}
			}
		}
	}

	ForceFlush();

	for (auto WidgetIter = WeakUserWidgets.CreateIterator(); WidgetIter; ++WidgetIter)
	{
		UUserWidget* UserWidget = WidgetIter->Get();
		ensureMsgf(UserWidget, TEXT("Widget became null during animation tick!"));

		if (UserWidget)
		{
			UserWidget->PostTickActionsAndAnimation(DeltaSeconds);

			// If this widget no longer has any animations playing, it doesn't need to be ticked any more
			if (UserWidget->ActiveSequencePlayers.Num() == 0)
			{
				UserWidget->AnimationTickManager = nullptr;
				WidgetIter.RemoveCurrent();
			}
		}
		else
		{
			WidgetIter.RemoveCurrent();
		}
	}
}

void UUMGSequenceTickManager::ForceFlush()
{
	if (Runner.IsAttachedToLinker())
	{
		Runner.Flush();
		LatentActionManager.RunLatentActions(Runner);
	}
}

void UUMGSequenceTickManager::HandleSlatePostTick(float DeltaSeconds)
{
	if (GFlushUMGAnimationsAtEndOfFrame && Runner.IsAttachedToLinker() && Runner.HasQueuedUpdates())
	{
		SCOPE_CYCLE_COUNTER(MovieSceneEval_FlushEndOfFrameAnimations);

		Runner.Flush();
		LatentActionManager.RunLatentActions(Runner);
	}
}

void UUMGSequenceTickManager::AddLatentAction(FMovieSceneSequenceLatentActionDelegate Delegate)
{
	LatentActionManager.AddLatentAction(Delegate);
}

void UUMGSequenceTickManager::ClearLatentActions(UObject* Object)
{
	LatentActionManager.ClearLatentActions(Object);
}

void UUMGSequenceTickManager::RunLatentActions()
{
	LatentActionManager.RunLatentActions(Runner);
}

UUMGSequenceTickManager* UUMGSequenceTickManager::Get(UObject* PlaybackContext)
{
	const TCHAR* TickManagerName = TEXT("GlobalUMGSequenceTickManager");

	check(PlaybackContext != nullptr && PlaybackContext->GetWorld() != nullptr);
	UWorld* World = PlaybackContext->GetWorld();

	UUMGSequenceTickManager* TickManager = FindObject<UUMGSequenceTickManager>(World, TickManagerName);
	if (!TickManager)
	{
		TickManager = NewObject<UUMGSequenceTickManager>(World, TickManagerName);

		TickManager->Linker = UMovieSceneEntitySystemLinker::FindOrCreateLinker(World, TEXT("UMGAnimationEntitySystemLinker"));
		check(TickManager->Linker);
		TickManager->Runner.AttachToLinker(TickManager->Linker);

		FSlateApplication& SlateApp = FSlateApplication::Get();
		FDelegateHandle PreTickHandle = SlateApp.OnPreTick().AddUObject(TickManager, &UUMGSequenceTickManager::TickWidgetAnimations);
		check(PreTickHandle.IsValid());
		TickManager->SlateApplicationPreTickHandle = PreTickHandle;

		FDelegateHandle PostTickHandle = SlateApp.OnPostTick().AddUObject(TickManager, &UUMGSequenceTickManager::HandleSlatePostTick);
		check(PostTickHandle.IsValid());
		TickManager->SlateApplicationPostTickHandle = PostTickHandle;
	}
	return TickManager;
}

