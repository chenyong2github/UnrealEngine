// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterViewExtension.h"
#include "WaterBodyComponent.h"
#include "WaterSubsystem.h"
#include "WaterModule.h"
#include "WaterZoneActor.h"
#include "WaterInfoRendering.h"

FWaterViewExtension::FWaterViewExtension(const FAutoRegister& AutoReg, UWorld* InWorld)
	: FWorldSceneViewExtension(AutoReg, InWorld)
{
}

FWaterViewExtension::~FWaterViewExtension()
{
}

void FWaterViewExtension::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
	if (WaterInfoContextsToRender.Num() > 0)
	{
		const TWeakObjectPtr<UWorld> WorldPtr = GetWorld();
		check(WorldPtr.IsValid())

		// Move out of the cache list to prevent re-entering this function when we set up the view extensions for the water info update itself
		TMap<AWaterZone*, UE::WaterInfo::FRenderingContext> ContextsToRender = MoveTemp(WaterInfoContextsToRender);
		WaterInfoContextsToRender.Empty();

		for (const TPair<AWaterZone*, UE::WaterInfo::FRenderingContext>& Pair : ContextsToRender)
		{
			UE::WaterInfo::UpdateWaterInfoRendering(WorldPtr.Get()->Scene, Pair.Value);
		}
	}
}

void FWaterViewExtension::PreRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily)
{
}

void FWaterViewExtension::MarkWaterInfoTextureForRebuild(const UE::WaterInfo::FRenderingContext& RenderContext)
{
	WaterInfoContextsToRender.Emplace(RenderContext.ZoneToRender, RenderContext);
}

