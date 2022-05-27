// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapePatchManager.h"

#include "Landscape.h"
#include "LandscapePatchComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "LandscapeDataAccess.h"
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
	for (TWeakObjectPtr<ULandscapePatchComponent>& Component : PatchComponents)
	{
		// Theoretically when components are marked for destruction, they should remove themselves from
		// the patch manager, hence the ensure here
		if (ensure(Component.IsValid()))
		{
			InCombinedResult = Component->Render_Native(InIsHeightmap, InCombinedResult, InWeightmapLayerName);
		}
		else
		{
			bHaveInvalidPatches = true;
		}
	}

	if (bHaveInvalidPatches)
	{
		PatchComponents.RemoveAll([](TWeakObjectPtr<ULandscapePatchComponent> Component) {
			return !Component.IsValid();
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
		PatchComponents.AddUnique(Patch);
		RequestLandscapeUpdate();
	}
}

bool ALandscapePatchManager::RemovePatch(TObjectPtr<ULandscapePatchComponent> Patch)
{
	bool bRemoved = false;

	if (Patch)
	{
		Modify();
		bRemoved = PatchComponents.Remove(Patch) > 0;
		RequestLandscapeUpdate();
	}
	
	return bRemoved;
}

#if WITH_EDITOR
void ALandscapePatchManager::PostEditUndo()
{
	RequestLandscapeUpdate();
}
#endif

#undef LOCTEXT_NAMESPACE