// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Engine/TextureRenderTarget2D.h"

#include "LandscapeTextureHeightPatchPS.h" // FApplyLandscapeTextureHeightPatchPS::FParameters
#include "LandscapeTexturePatchBase.h"
#include "MatrixTypes.h"

#include "LandscapeTextureHeightPatch.generated.h"

class UTexture2D;

// Used for Reinitialize, to determine how to initialize the texture.
UENUM()
enum class ELandscapeTextureHeightPatchInitMode : uint8
{
	// Sample the current landscape covered by the patch to initialize.
	FromLandscape,
	
	// Initialize from currently set texture asset.
	TextureAsset,

	// Initialize to landscape mid value.
	Blank,
};

// Determines how the patch is combined with the previous state of the landscape.
UENUM(BlueprintType)
enum class ELandscapeTextureHeightPatchBlendMode : uint8
{
	// Let the patch specify the actual target height, and blend that with the existing
	// height using falloff/alpha. E.g. with no falloff and alpha 1, the landscape will
	// be set directly to the height sampled from patch. With alpha 0.5, landscape height 
	// will be averaged evenly with patch height.
	AlphaBlend,
	
	// Interpreting the landscape mid value as 0, use the texture patch as an offset to
	// apply to the landscape. Falloff/alpha will just affect the degree to which the offset
	// is applied (e.g. alpha of 0.5 will apply just half the offset).
	Additive,
};

// Determines falloff method for the patch's influence.
UENUM(BlueprintType)
enum class ELandscapeTextureHeightPatchFalloffMode : uint8
{
	// Affect landscape in a circle inscribed in the patch, and fall off across
	// a margin extending into that circle.
	Circle,

	// Affect entire rectangle of patch (except for circular corners), and fall off
	// across a margin extending inward from the boundary.
	RoundedRectangle,
};

/**
 * A texture-based height patch
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = Landscape, meta = (BlueprintSpawnableComponent))
class LANDSCAPEPATCH_API ULandscapeTextureHeightPatch : public ULandscapeTexturePatchBase
{
	GENERATED_BODY()

public:

	virtual UTextureRenderTarget2D* Render_Native(bool InIsHeightmap,
		UTextureRenderTarget2D* InCombinedResult,
		const FName& InWeightmapLayerName) override;
	
	// ULandscapeTexturePatchBase
	virtual bool SetTextureResolution(FVector2D ResolutionIn) override;
	virtual bool SetSourceMode(ELandscapeTexturePatchSourceMode NewMode, bool bDeleteUnusedInternalTextures = true) override;

	// For now the patch is largely editor-only
#if WITH_EDITOR
	// UActorComponent
	virtual void OnComponentCreated() override;

	// UObject
	virtual void PreSave(FObjectPreSaveContext SaveContext) override;
	virtual void PostLoad() override;
	virtual void PreDuplicate(FObjectDuplicationParameters& DupParams) override;
	virtual void ExportCustomProperties(FOutputDevice& Out, uint32 Indent) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostInitProperties() override;

	/**
	 * Deletes the internal render target and internal texture.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = Initialization)
	void DeleteInternalTextures();

	/**
	 * Given the current initialization settings, reinitialize the height patch.
	 */
	UFUNCTION(CallInEditor, Category = Initialization)
	void Reinitialize();

	/**
	 * Adjusts patch rotation to be aligned to a 90 degree increment relative to the landscape,
	 * adjusts UnscaledPatchCoverage such that it becomes a multiple of landscape quad size, and
	 * adjusts patch location so that the boundaries of the covered area lie on the nearest
	 * landscape vertices.
	 * Note that this doesn't adjust the resolution of the texture that the patch uses, so landscape
	 * vertices within the inside of the patch may still not always align with texture patch pixel
	 * centers (if the resolutions aren't multiples of each other).
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = Initialization)
	void SnapToLandscape();
#endif

	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	void SetFalloff(float FalloffIn) { Falloff = FalloffIn; }

	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	void SetBlendMode(ELandscapeTextureHeightPatchBlendMode BlendModeIn) { BlendMode = BlendModeIn; }

	/**
	 * Determine whether the patch Z relative to the landscape affects the results. See comment for bUsePatchZAsReference.
	 */
	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	void SetUsePatchZAsReference(bool bUsePatchZIn) { bUsePatchZAsReference = bUsePatchZIn; }

	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	void SetUseTextureAlphaChannel(bool bUse) { bUseTextureAlphaChannel = bUse; }

protected:

	/**
	 * Extra scaling to apply to patch height. Useful when using adjusting existing texture assets.
	 */
	UPROPERTY(EditAnywhere, Category = Settings, meta = (UIMin = "0", UIMax = "1"))
	float PatchHeightScale = 1;

	/**
	 * Whether to apply the patch Z scale to the height stored in the patch.
	 */
	UPROPERTY(EditAnywhere, Category = Settings, AdvancedDisplay, meta = (DisplayName = "Apply Component Z Scale"))
	bool bApplyComponentZScale = true;

	/**
	 * Determine whether the patch Z relative to the landscape affects the results. When true, moving the
	 * patch up and down will move the results up and down, which is useful for using the patch to move
	 * landscape edits with a mesh. When false, the patch is always applied as if it is at z=0 relative
	 * to landscape, which removes the need to worry about its vertical location and is useful when using
	 * the patch in Blueprints.
	 * Note that this setting also affects reinitialization from landscape, and it should probably match
	 * usage when initializing.
	 */
	UPROPERTY(EditAnywhere, Category = Settings, AdvancedDisplay)
	bool bUsePatchZAsReference = true;

	UPROPERTY(EditAnywhere, Category = Settings, AdvancedDisplay)
	ELandscapeTextureHeightPatchBlendMode BlendMode = ELandscapeTextureHeightPatchBlendMode::AlphaBlend;
	
	/** When true, texture alpha channel will be used when applying the patch. */
	UPROPERTY(EditAnywhere, Category = Settings, AdvancedDisplay)
	bool bUseTextureAlphaChannel = false;

	UPROPERTY(EditAnywhere, Category = Settings)
	ELandscapeTextureHeightPatchFalloffMode FalloffMode = ELandscapeTextureHeightPatchFalloffMode::RoundedRectangle;

	/**
	 * Distance (in unscaled world coordinates) across which to smoothly fall off the patch effects.
	 */
	UPROPERTY(EditAnywhere, Category = Settings, meta = (ClampMin = "0", UIMax = "2000"))
	float Falloff = 0;

	UPROPERTY(EditAnywhere, Category = Initialization, meta = (
		EditCondition = "SourceMode != ELandscapeTexturePatchSourceMode::TextureAsset", EditConditionHides))
	ELandscapeTextureHeightPatchInitMode InitializationMode = ELandscapeTextureHeightPatchInitMode::FromLandscape;

	/** When initializing from an texture asset, set the internal texture to have the same resolution. */
	UPROPERTY(EditAnywhere, Category = Initialization, meta = (
		EditCondition = "SourceMode != ELandscapeTexturePatchSourceMode::TextureAsset && InitializationMode == ELandscapeTextureHeightPatchInitMode::TextureAsset", 
		EditConditionHides))
	bool bUseSameTextureDimensions = false;

	/** When initializing from landscape, set resolution based off of the landscape (and a multiplier). */
	UPROPERTY(EditAnywhere, Category = Initialization, meta = (
		EditCondition = "SourceMode != ELandscapeTexturePatchSourceMode::TextureAsset && (InitializationMode != ELandscapeTextureHeightPatchInitMode::TextureAsset || !bUseSameTextureDimensions)", 
		EditConditionHides))
	bool bBaseResolutionOffLandscape = true;

	/** 
	 * Multiplier to apply to landscape resolution when initializing patch resolution. A value greater than 1.0 will use higher
	 * resolution than the landscape (perhaps useful for slightly more accurate results while not aligned to landscape), and
	 * a value less that 1.0 will use lower.
	 */
	UPROPERTY(EditAnywhere, Category = Initialization, meta = (
		EditCondition = "SourceMode != ELandscapeTexturePatchSourceMode::TextureAsset && (InitializationMode != ELandscapeTextureHeightPatchInitMode::TextureAsset || !bUseSameTextureDimensions) && bBaseResolutionOffLandscape", 
		EditConditionHides))
	float ResolutionMultiplier = 1;

	/** Texture width to use when reinitializing. */
	UPROPERTY(EditAnywhere, Category = Initialization, meta = (
		EditCondition = "SourceMode != ELandscapeTexturePatchSourceMode::TextureAsset && (InitializationMode != ELandscapeTextureHeightPatchInitMode::TextureAsset || !bUseSameTextureDimensions)", 
		EditConditionHides, ClampMin = "1"))
	int32 InitTextureSizeX = 64;

	/** Texture height to use when reinitializing */
	UPROPERTY(EditAnywhere, Category = Initialization, meta = (
		EditCondition = "SourceMode != ELandscapeTexturePatchSourceMode::TextureAsset && (InitializationMode != ELandscapeTextureHeightPatchInitMode::TextureAsset || !bUseSameTextureDimensions)", 
		EditConditionHides, ClampMin = "1"))
	int32 InitTextureSizeY = 64;

	// Used to properly transition the source mode when editing it via the detail panel.
	UPROPERTY()
	ELandscapeTexturePatchSourceMode PreviousSourceMode = SourceMode;

private:

#if WITH_EDITOR
	// Uncertain whether these may end up in the base class once we have weight patches.
	void CopyInternalRenderTargetToInternalTexture(bool bBlock);
	void CopyInternalTextureToInternalRenderTarget();

	void UpdateShaderParams(UE::Landscape::FApplyLandscapeTextureHeightPatchPS::FParameters& Params, 
		const FIntPoint& DestinationResolution, FIntRect& DestinationBoundsOut) const;

	bool ResizeRenderTargetIfNeeded(int32 TextureSizeX, int32 TextureSizeY);
	bool ResizeTextureIfNeeded(int32 TextureSizeX, int32 TextureSizeY, bool bClear, bool bUpdateResource);
#endif
};