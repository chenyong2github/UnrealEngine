// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Viewport/DisplayClusterViewport_VisibilitySettings.h"
#include "Render/Viewport/DisplayClusterViewportManager.h"
#include "Render/Viewport/DisplayClusterViewport.h"

#include "EngineUtils.h"
#include "SceneView.h"

static void GetPrimitiveComponentsFromLayers(UWorld* World, const TArray<FName>& SourceLayers, TSet<FPrimitiveComponentId>& OutPrimitives)
{
	if (SourceLayers.Num())
	{
		// Iterate over all actors, looking for actors in the specified layers.
		for (const TWeakObjectPtr<AActor> WeakActor : FActorRange(World))
		{
			AActor* Actor = WeakActor.Get();
			if (Actor)
			{
				bool bActorFoundOnSourceLayers = false;

				// Search actor on source layers
				for (const FName& LayerIt : SourceLayers)
				{
					if (Actor && Actor->Layers.Contains(LayerIt))
					{
						bActorFoundOnSourceLayers = true;
						break;
					}
				}

				if (bActorFoundOnSourceLayers)
				{
					// Save all actor components to OutPrimitives
					for (UActorComponent* Component : Actor->GetComponents())
					{
						if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Component))
						{
							OutPrimitives.Add(PrimComp->ComponentId);
						}
					}
				}
			}
		}
	}
}

#if WITH_EDITOR
static void GetEditorVisualComponentsFromWorld(UWorld* World, TSet<FPrimitiveComponentId>& OutPrimitives)
{
	// Iterate over all actors, looking for editor components.
	for (const TWeakObjectPtr<AActor>& WeakActor : FActorRange(World))
	{
		if (AActor* Actor = WeakActor.Get())
		{
			TArray<UPrimitiveComponent*> PrimitiveComponents;
			Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
			for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
			{
				if (PrimComp->GetName().EndsWith(TEXT("_impl")))
				{
					OutPrimitives.Add(PrimComp->ComponentId);
				}
			}
		}
	}
}
#endif

static void AppendComponents(TSet<FPrimitiveComponentId>& InOutPrimitives, const TArray<UActorComponent*>& InComponents)
{
}

void FDisplayClusterViewport_VisibilitySettings::SetActorLayers(const TArray<FActorLayer>& InActorLayers)
{
	ActorLayers.Empty();
	for (const FActorLayer& ActorLayerIt : InActorLayers)
	{
		ActorLayers.Add(ActorLayerIt.Name);
	}
}

void FDisplayClusterViewport_VisibilitySettings::SetupSceneView(class UWorld* World, FSceneView& InOutView) const
{
	check(World);

#if WITH_EDITOR
	// Don't capture editor only visual components.
	GetEditorVisualComponentsFromWorld(World, InOutView.HiddenPrimitives);
#endif
	
	switch (LayersMode)
	{
	case EDisplayClusterViewport_VisibilityMode::ShowOnly:
	{
		if (ActorLayers.Num() > 0 || AdditionalComponentsList.Num() > 0)
		{
			InOutView.ShowOnlyPrimitives.Emplace();
			GetPrimitiveComponentsFromLayers(World, ActorLayers, InOutView.ShowOnlyPrimitives.GetValue());
			InOutView.ShowOnlyPrimitives.GetValue().Append(AdditionalComponentsList);
			return;
		}
		break;
	}
	case EDisplayClusterViewport_VisibilityMode::Hide:
	{
		GetPrimitiveComponentsFromLayers(World, ActorLayers, InOutView.HiddenPrimitives);
		InOutView.HiddenPrimitives.Append(AdditionalComponentsList);
		break;
	}
	default:
		break;
	}

	// Also hide components from root actor 
	InOutView.HiddenPrimitives.Append(RootActorHidePrimitivesList);
}
