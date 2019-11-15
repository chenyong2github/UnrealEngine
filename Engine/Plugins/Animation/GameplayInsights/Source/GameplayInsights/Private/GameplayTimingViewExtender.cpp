// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GameplayTimingViewExtender.h"
#include "Insights/ITimingViewSession.h"
#include "GameplaySharedData.h"
#include "AnimationSharedData.h"
#include "UObject/WeakObjectPtr.h"

#if WITH_ENGINE
#include "Engine/World.h"
#endif

void FGameplayTimingViewExtender::OnBeginSession(Insights::ITimingViewSession& InSession)
{
	FPerSessionData* PerSessionData = PerSessionDataMap.Find(&InSession);
	if(PerSessionData == nullptr)
	{
		PerSessionData = &PerSessionDataMap.Add(&InSession);
		PerSessionData->GameplaySharedData = new FGameplaySharedData();
		PerSessionData->AnimationSharedData = new FAnimationSharedData(*PerSessionData->GameplaySharedData);
	}

	PerSessionData->GameplaySharedData->OnBeginSession(InSession);
	PerSessionData->AnimationSharedData->OnBeginSession(InSession);
}

void FGameplayTimingViewExtender::OnEndSession(Insights::ITimingViewSession& InSession)
{
	FPerSessionData* PerSessionData = PerSessionDataMap.Find(&InSession);
	if(PerSessionData != nullptr)
	{
		PerSessionData->GameplaySharedData->OnEndSession(InSession);
		PerSessionData->AnimationSharedData->OnEndSession(InSession);

		delete PerSessionData->GameplaySharedData;
		PerSessionData->GameplaySharedData = nullptr;
		delete PerSessionData->AnimationSharedData;
		PerSessionData->AnimationSharedData = nullptr;
	}

	PerSessionDataMap.Remove(&InSession);
}

void FGameplayTimingViewExtender::Tick(Insights::ITimingViewSession& InSession, const Trace::IAnalysisSession& InAnalysisSession)
{
	FPerSessionData* PerSessionData = PerSessionDataMap.Find(&InSession);
	if(PerSessionData != nullptr)
	{
		PerSessionData->GameplaySharedData->Tick(InSession, InAnalysisSession);
		PerSessionData->AnimationSharedData->Tick(InSession, InAnalysisSession);
	}
}

void FGameplayTimingViewExtender::ExtendFilterMenu(Insights::ITimingViewSession& InSession, FMenuBuilder& InMenuBuilder)
{
	FPerSessionData* PerSessionData = PerSessionDataMap.Find(&InSession);
	if(PerSessionData != nullptr)
	{
		PerSessionData->GameplaySharedData->ExtendFilterMenu(InMenuBuilder);
		PerSessionData->AnimationSharedData->ExtendFilterMenu(InMenuBuilder);
	}
}

#if WITH_ENGINE
void FGameplayTimingViewExtender::AddVisualizerWorld(UWorld* InWorld)
{
	Worlds.Add(InWorld);
}
#endif

void FGameplayTimingViewExtender::TickVisualizers(float DeltaTime)
{
#if WITH_ENGINE
	// Trim invalid worlds
	Worlds.RemoveAll([](const TWeakObjectPtr<UWorld>& InWorld){ return InWorld.Get() == nullptr; });

	for(auto& PerSessionData : PerSessionDataMap)
	{
		// Draw using line batchers
		for(TWeakObjectPtr<UWorld>& World : Worlds)
		{
			if(World->LineBatcher)
			{
				PerSessionData.Value.AnimationSharedData->DrawPoses(World->LineBatcher);
			}
		}
	}
#endif
}