// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapePatchManager.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Landscape.h"
#include "LandscapeDataAccess.h"
#include "LandscapePatchComponent.h"
#include "LandscapePatchLogging.h"
#include "RenderGraph.h" // RDG_EVENT_NAME

#define LOCTEXT_NAMESPACE "LandscapePatchManager"

// TODO: Not sure if using this kind of constructor is a proper thing to do vs some other hook...
ALandscapePatchManager::ALandscapePatchManager(const FObjectInitializer& ObjectInitializer)
	: ALandscapeBlueprintBrushBase(ObjectInitializer)
{
#if WITH_EDITOR
	SetAffectsHeightmap(true);
	SetAffectsWeightmap(true);
#endif
}

void ALandscapePatchManager::Initialize_Native(const FTransform & InLandscapeTransform,
	const FIntPoint& InLandscapeSize,
	const FIntPoint& InLandscapeRenderTargetSize)
{
	// Get a transform from pixel coordinate in heightmap to world space coordinate. Note that we can't
	// store the inverse directly because a FTransform can't properly represent a TRS inverse when the
	// original TRS has non-uniform scaling).

	// The pixel to landscape-space transform is unrotated, (S_p * x + T_p). The landscape to world
	// transform gets applied on top of this: (R_l * S_l * (S_p * x + T_p)) + T_L. Collapsing this
	// down to pixel to world TRS, we get: R_l * (S_l * S_p) * x + (R_l * S_l * T_p + T_L)

	// To go from stored height value to unscaled height, we divide by 128 and subtract 256. We can get these
	// values from the constants in LandscapeDataAccess.h (we distribute the multiplication by LANDSCAPE_ZSCALE
	// so that translation happens after scaling like in TRS)
	const double HEIGHTMAP_TO_OBJECT_HEIGHT_SCALE = LANDSCAPE_ZSCALE;
	const double HEIGHTMAP_TO_OBJECT_HEIGHT_OFFSET = -LandscapeDataAccess::MidValue * LANDSCAPE_ZSCALE;

	// S_p: the pixel coordinate scale is actually the same as xy object-space coordinates because one quad is 1 unit,
	// so we only need to scale the height.
	FVector3d PixelToObjectSpaceScale = FVector3d(
		1,
		1,
		HEIGHTMAP_TO_OBJECT_HEIGHT_SCALE
	);

	// T_p: the center of the pixel
	FVector3d PixelToObjectSpaceTranslate = FVector3d(
		-0.5,
		-0.5,
		HEIGHTMAP_TO_OBJECT_HEIGHT_OFFSET
	);

	// S_l* S_p: composed scale
	HeightmapCoordsToWorld.SetScale3D(InLandscapeTransform.GetScale3D() * PixelToObjectSpaceScale);

	// R_l
	HeightmapCoordsToWorld.SetRotation(InLandscapeTransform.GetRotation());

	// R_l * S_l * T_p + T_L: composed translation
	HeightmapCoordsToWorld.SetTranslation(InLandscapeTransform.TransformVector(PixelToObjectSpaceTranslate)
		+ InLandscapeTransform.GetTranslation());
}

UTextureRenderTarget2D* ALandscapePatchManager::Render_Native(bool InIsHeightmap,
	UTextureRenderTarget2D* InCombinedResult,
	const FName& InWeightmapLayerName)
{
	// Used to determine whether we need to remove any invalid brushes
	bool bHaveInvalidPatches = false;

	// TODO: There are many uncertainties in how we iterate across the height patches and have them
	// apply themselves. For one thing we may want to pass around a render graph, in which case this
	// loop will happen on the render thread somehow. For another, it's not yet determined what all
	// of this will look like when we have the ability to render to just a subsection of the entire
	// height map.
	// So for now we do the simplest thing, and that is to have the height patches act as if they were
	// independent brushes.
	for (TSoftObjectPtr<ULandscapePatchComponent>& Component : PatchComponents)
	{
		if (Component.IsPending())
		{
			Component.LoadSynchronous();
		}

		if (Component.IsValid())
		{
			if (Component->IsEnabled())
			{
				InCombinedResult = Component->Render_Native(InIsHeightmap, InCombinedResult, InWeightmapLayerName);
			}
		}
		else if (Component.IsNull())
		{
			// Theoretically when components are marked for destruction, they should remove themselves from
			// the patch manager in their OnComponentDestroyed call. However there seem to be ways to end up
			// with destroyed patches not being removed, for instance through saving the manager but not the
			// patch actor.
			UE_LOG(LogLandscapePatch, Warning, TEXT("ALandscapePatchManager: Found an invalid patch in patch manager. It will be removed."));
			bHaveInvalidPatches = true;
		}
		else
		{
			// This means that IsPending() was true, but LoadSynchronous() failed, which we don't expect to happen.
			ensure(false);
		}
	}

	if (bHaveInvalidPatches)
	{
		PatchComponents.RemoveAll([](TSoftObjectPtr<ULandscapePatchComponent> Component) {
			return Component.IsNull();
		});
	}

	return InCombinedResult;
}

void ALandscapePatchManager::SetTargetLandscape(ALandscape* InTargetLandscape)
{
#if WITH_EDITOR
	if (OwningLandscape != InTargetLandscape)
	{
		if (OwningLandscape)
		{
			OwningLandscape->RemoveBrush(this);
		}

		if (InTargetLandscape && ensure(InTargetLandscape->CanHaveLayersContent()))
		{
			FName LayerName = FName("LandscapePatches");
			int32 ExistingPatchLayerIndex = InTargetLandscape->GetLayerIndex(LayerName);
			if (ExistingPatchLayerIndex == INDEX_NONE)
			{
				ExistingPatchLayerIndex = InTargetLandscape->CreateLayer(LayerName);

				// If there's a layer with a water brush manager, put the layer we created under that
				// TODO: It's uncertain whether we want this approach long term. We might want to have a popup ask where to
				// insert the layer, for instance. However, this is what the first artists to test the module suggested,
				// and it's fairly easy to swap layers around, so it is easy for users to change if they are unhappy. 
				int32 LayerIndex = 0;
				for (; LayerIndex < ExistingPatchLayerIndex; ++LayerIndex)
				{
					TArray<ALandscapeBlueprintBrushBase*> LayerBrushes = InTargetLandscape->GetBrushesForLayer(LayerIndex);
					if (LayerBrushes.ContainsByPredicate([](ALandscapeBlueprintBrushBase* Brush) 
						{ 
							// It would be preferable to do a Cast<ALandscapeBlueprintBrushBase> to be robust to subclassing, etc.,
							// but that doesn't seem worth having to add a dependency on the water plugin, hence the comparison by name.
							// Note that the "A" prefix is removed in GetName.
							FString BrushManagerName = Brush->GetClass()->GetName();
							return Brush->GetClass()->GetName() == TEXT("WaterBrushManager");
						}))
					{
						// Found a water brush manager.
						break;
					}
				}

				if (LayerIndex < ExistingPatchLayerIndex)
				{
					InTargetLandscape->ReorderLayer(ExistingPatchLayerIndex, LayerIndex);
					ExistingPatchLayerIndex = LayerIndex;
				}
			}
			InTargetLandscape->AddBrushToLayer(ExistingPatchLayerIndex, this);
		}
	}
#endif
}

void ALandscapePatchManager::AddPatch(TObjectPtr<ULandscapePatchComponent> Patch)
{
	if (Patch)
	{
		Modify();
		PatchComponents.AddUnique(TSoftObjectPtr<ULandscapePatchComponent>(Patch.Get()));
		RequestLandscapeUpdate();
	}
}

bool ALandscapePatchManager::RemovePatch(TObjectPtr<ULandscapePatchComponent> Patch)
{
	bool bRemoved = false;

	if (Patch)
	{
		Modify();
		bRemoved = PatchComponents.Remove(TSoftObjectPtr<ULandscapePatchComponent>(Patch.Get())) > 0;
		RequestLandscapeUpdate();
	}
	
	return bRemoved;
}

#if WITH_EDITOR
bool ALandscapePatchManager::IsAffectingWeightmapLayer(const FName& InLayerName) const
{
	for (const TSoftObjectPtr<ULandscapePatchComponent>& Component : PatchComponents)
	{
		if (Component.IsPending())
		{
			Component.LoadSynchronous();
		}

		if (Component.IsValid() && Component->IsEnabled() && Component->IsAffectingWeightmapLayer(InLayerName))
		{
			return true;
		}
	}

	return false;
}

void ALandscapePatchManager::PostEditUndo()
{
	RequestLandscapeUpdate();
}
#endif

#undef LOCTEXT_NAMESPACE