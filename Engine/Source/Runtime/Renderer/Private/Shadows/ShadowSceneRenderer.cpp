// Copyright Epic Games, Inc. All Rights Reserved.
/*=============================================================================
	ShadowSceneRenderer.cpp:
=============================================================================*/
#include "ShadowSceneRenderer.h"
#include "../ScenePrivate.h"
#include "../DeferredShadingRenderer.h"
#include "../VirtualShadowMaps/VirtualShadowMapCacheManager.h"

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#include "DynamicPrimitiveDrawing.h"
#endif

TAutoConsoleVariable<int32> CVarMaxDistantLightsPerFrame(
	TEXT("r.Shadow.Virtual.MaxDistantUpdatePerFrame"),
	1,
	TEXT("Maximum number of distant lights to update each frame."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);


static TAutoConsoleVariable<int32> CVarDistantLightMode(
	TEXT("r.Shadow.Virtual.DistantLightMode"),
	0,
	TEXT("Control whether distant light mode is enabled for local lights.\n0 == Off (default), \n1 == On, \n2 == Force All."),
	ECVF_RenderThreadSafe
);


FShadowSceneRenderer::FShadowSceneRenderer(FDeferredShadingSceneRenderer& InSceneRenderer)
	: SceneRenderer(InSceneRenderer)
	, Scene(*InSceneRenderer.Scene)
	, VirtualShadowMapArray(InSceneRenderer.VirtualShadowMapArray)
{
}

TSharedPtr<FVirtualShadowMapPerLightCacheEntry> FShadowSceneRenderer::AddLocalLightShadow(const FWholeSceneProjectedShadowInitializer& ProjectedShadowInitializer, FProjectedShadowInfo* ProjectedShadowInfo, FLightSceneInfo* LightSceneInfo, float MaxScreenRadius)
{
	FVirtualShadowMapArrayCacheManager* CacheManager = VirtualShadowMapArray.CacheManager;

	const int32 LocalLightShadowIndex = LocalLights.Num();
	FLocalLightShadowFrameSetup& LocalLightShadowFrameSetup = LocalLights.AddDefaulted_GetRef();
	LocalLightShadowFrameSetup.ProjectedShadowInfo = ProjectedShadowInfo;
	LocalLightShadowFrameSetup.LightSceneInfo = LightSceneInfo;

	const int32 NumMaps = ProjectedShadowInitializer.bOnePassPointLightShadow ? 6 : 1;
	for (int32 Index = 0; Index < NumMaps; ++Index)
	{
		FVirtualShadowMap* VirtualShadowMap = VirtualShadowMapArray.Allocate();
		// TODO: redundant
		ProjectedShadowInfo->VirtualShadowMaps.Add(VirtualShadowMap);
		LocalLightShadowFrameSetup.VirtualShadowMaps.Add(VirtualShadowMap);
	}

	TSharedPtr<FVirtualShadowMapPerLightCacheEntry> PerLightCacheEntry = CacheManager->FindCreateLightCacheEntry(LightSceneInfo->Id);
	if (PerLightCacheEntry.IsValid())
	{
		LocalLightShadowFrameSetup.PerLightCacheEntry = PerLightCacheEntry;
		// Single page res, at this point we force the VSM to be single page
		// TODO: this computation does not match up with page marking logic super-well, particularly for long spot lights,
		//       we can absolutely mirror the page marking calc better, just unclear how much it helps. 
		//       Also possible to feed back from gpu - which would be more accurate wrt partially visible lights (e.g., a spot going through the ground).
		//       Of course this creates jumps if visibility changes, which may or may not create unsolvable artifacts.
		const bool bIsDistantLight = CVarDistantLightMode.GetValueOnRenderThread() != 0
			&& (MaxScreenRadius <= FVirtualShadowMap::PageSize || CVarDistantLightMode.GetValueOnRenderThread() == 2);

		PerLightCacheEntry->UpdateLocal(ProjectedShadowInitializer, bIsDistantLight);

		for (int32 Index = 0; Index < NumMaps; ++Index)
		{
			FVirtualShadowMap* VirtualShadowMap = LocalLightShadowFrameSetup.VirtualShadowMaps[Index];

			TSharedPtr<FVirtualShadowMapCacheEntry> VirtualSmCacheEntry = PerLightCacheEntry->FindCreateShadowMapEntry(Index);
			VirtualSmCacheEntry->UpdateLocal(VirtualShadowMap->ID, *PerLightCacheEntry);
			VirtualShadowMap->VirtualShadowMapCacheEntry = VirtualSmCacheEntry;
		}

		if (bIsDistantLight)
		{
			// This priority could be calculated based also on whether the light has actually been invalidated or not (currently not tracked on host).
			// E.g., all things being equal update those with an animated mesh in, for example. Plus don't update those the don't need it at all.
			int32 FramesSinceLastRender = int32(Scene.GetFrameNumber()) - int32(PerLightCacheEntry->GetLastScheduledFrameNumber());
			DistantLightUpdateQueue.Add(-FramesSinceLastRender, LocalLightShadowIndex);
		}
	}
	return PerLightCacheEntry;
}

void FShadowSceneRenderer::PostInitDynamicShadowsSetup()
{
	UpdateDistantLightPriorityRender();

	PostSetupDebugRender();
}

void FShadowSceneRenderer::UpdateDistantLightPriorityRender()
{
	int32 UpdateBudgetNumLights = CVarMaxDistantLightsPerFrame.GetValueOnRenderThread() < 0 ? int32(DistantLightUpdateQueue.Num()) : FMath::Min(int32(DistantLightUpdateQueue.Num()), CVarMaxDistantLightsPerFrame.GetValueOnRenderThread());
	for (int32 Index = 0; Index < UpdateBudgetNumLights; ++Index)
	{
		const int32 LocalLightShadowIndex = DistantLightUpdateQueue.Top();
		const int32 Age = DistantLightUpdateQueue.GetKey(LocalLightShadowIndex);
		// UE_LOG(LogTemp, Log, TEXT("Index: %d Age: %d"), LocalLightShadowIndex, Age);
		DistantLightUpdateQueue.Pop();

		FLocalLightShadowFrameSetup& LocalLightShadowFrameSetup = LocalLights[LocalLightShadowIndex];
		
		// Force fully cached to be off.
		LocalLightShadowFrameSetup.ProjectedShadowInfo->bShouldRenderVSM = true;
		LocalLightShadowFrameSetup.PerLightCacheEntry->CurrenScheduledFrameNumber = Scene.GetFrameNumber();
		// Should trigger invalidations also.
		LocalLightShadowFrameSetup.PerLightCacheEntry->Invalidate();
	}
}


void FShadowSceneRenderer::PostSetupDebugRender()
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// TODO: Move to debug rendering function in FShadowSceneRenderer
	if ((SceneRenderer.ViewFamily.EngineShowFlags.DebugDrawDistantVirtualSMLights))
	{
		int32 NumDistant = 0;
		for (FViewInfo& View : SceneRenderer.Views)
		{
			FViewElementPDI DebugPDI(&View, nullptr, &View.DynamicPrimitiveCollector);

			for (const FLocalLightShadowFrameSetup& LightSetup : LocalLights)
			{			
				FLinearColor Color = FLinearColor(FColor::Blue);
				if (LightSetup.PerLightCacheEntry && LightSetup.PerLightCacheEntry->bCurrentIsDistantLight)
				{
					++NumDistant;
					int32 FramesSinceLastRender = int32(Scene.GetFrameNumber()) - int32(LightSetup.PerLightCacheEntry->GetLastScheduledFrameNumber());
					float Fade = FMath::Min(0.8f, float(FramesSinceLastRender) / float(LocalLights.Num()));
					Color = LightSetup.PerLightCacheEntry->IsFullyCached() ? FMath::Lerp(FLinearColor(FColor::Green), FLinearColor(FColor::Red), Fade) : FLinearColor(FColor::Red);
				}

				Color.A = 1.0f;
				if (LightSetup.LightSceneInfo->Proxy->GetLightType() == LightType_Spot)
				{
					FTransform TransformNoScale = FTransform(LightSetup.LightSceneInfo->Proxy->GetLightToWorld());
					TransformNoScale.RemoveScaling();

					DrawWireSphereCappedCone(&DebugPDI, TransformNoScale, LightSetup.LightSceneInfo->Proxy->GetRadius(), FMath::RadiansToDegrees(LightSetup.LightSceneInfo->Proxy->GetOuterConeAngle()), 16, 4, 8, Color, SDPG_World);
				}
				else
				{
					DrawWireSphereAutoSides(&DebugPDI, -LightSetup.ProjectedShadowInfo->PreShadowTranslation, Color, LightSetup.LightSceneInfo->Proxy->GetRadius(), SDPG_World);
				}
			}
		}
		SceneRenderer.OnGetOnScreenMessages.AddLambda([this, NumDistant](FScreenMessageWriter& ScreenMessageWriter)->void
		{
			ScreenMessageWriter.DrawLine(FText::FromString(FString::Printf(TEXT("Distant Light Count: %d"), NumDistant)), 10, FColor::Yellow);
			ScreenMessageWriter.DrawLine(FText::FromString(FString::Printf(TEXT("Active Local Light Count: %d"), LocalLights.Num())), 10, FColor::Yellow);
			ScreenMessageWriter.DrawLine(FText::FromString(FString::Printf(TEXT("Scene Light Count: %d"), Scene.Lights.Num())), 10, FColor::Yellow);
		});
	}
#endif
}