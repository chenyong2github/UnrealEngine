// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterBodyInfoMeshComponent.h"
#include "StaticMeshSceneProxy.h"
#include  "UObject/UObjectIterator.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(WaterBodyInfoMeshComponent)

static void OnCVarWaterInfoSceneProxiesValueChanged(IConsoleVariable*)
{
	for (UWaterBodyInfoMeshComponent* WaterBodyInfoMeshComponent : TObjectRange<UWaterBodyInfoMeshComponent>(RF_ClassDefaultObject | RF_ArchetypeObject, true, EInternalObjectFlags::Garbage))
	{
		WaterBodyInfoMeshComponent->MarkRenderStateDirty();
	}
}

static TAutoConsoleVariable<bool> CVarShowWaterInfoSceneProxies(
	TEXT("r.Water.WaterInfo.ShowSceneProxies"),
	false,
	TEXT("When enabled, always shows the water scene proxies in the main viewport. Useful for debugging only"),
	FConsoleVariableDelegate::CreateStatic(OnCVarWaterInfoSceneProxiesValueChanged),
	ECVF_RenderThreadSafe
);

UWaterBodyInfoMeshComponent::UWaterBodyInfoMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAffectDistanceFieldLighting = false;
	bSelectable = false;
}

FPrimitiveSceneProxy* UWaterBodyInfoMeshComponent::CreateSceneProxy()
{
	if (!CanCreateSceneProxy())
	{
		return nullptr;
	}
	return new FWaterBodyInfoMeshSceneProxy(this);
}

FWaterBodyInfoMeshSceneProxy::FWaterBodyInfoMeshSceneProxy(UWaterBodyInfoMeshComponent* Component)
	: FStaticMeshSceneProxy(Component, true)
{
	SetEnabled(false);
}

void FWaterBodyInfoMeshSceneProxy::SetEnabled(bool bInEnabled)
{
	SetForceHidden(!(bInEnabled || CVarShowWaterInfoSceneProxies.GetValueOnAnyThread()));
}
