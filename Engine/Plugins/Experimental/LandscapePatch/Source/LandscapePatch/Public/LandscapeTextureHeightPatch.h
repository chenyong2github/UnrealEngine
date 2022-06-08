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

UENUM(BlueprintType)
enum class ELandscapeTextureHeightPatchEncoding : uint8
{
	// Values in texture should be interpreted as being floats in the range [0,1]. User specifies what
	// value corresponds to height 0 (i.e. height when landscape is "cleared"), and the size of the 
	// range in world units.
	ZeroToOne,

	// Values in texture are direct world-space heights.
	WorldUnits,

	// Values in texture are stored the same way they are in landscape actors: as 16 bit integers packed 
	// into two bytes, mapping to [-256, 256 - 1/128] before applying landscape scale.
	NativePackedHeight

	//~ Note that currently ZeroToOne and WorldUnits actually work the same way- we subtract the center point (0 for WorldUnits),
	//~ then scale in some way (1.0 for WorldUnits). However, having separate options here allows us to initialize defaults
	//~ appropriately when setting the encoding mode.
};

//~ A struct in case we find that we need other encoding settings.
USTRUCT(BlueprintType)
struct LANDSCAPEPATCH_API FLandscapeTexturePatchEncodingSettings
{
	GENERATED_BODY()
public:
	/**
	 * The value in the patch data that corresponds to 0 landscape height (which is in line with patch Z when
	 * "Use Patch Z As Reference" is true, and at landscape zero/mid value when false).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	double ZeroInEncoding = 0;

	/**
	 * The scale that should be aplied to the data stored in the patch relative to the zero in the encoding, in world coordinates.
	 * For instance if the encoding is [0,1], and 0.5 correponds to 0, a WorldSpaceEncoding Scale of 100 means that the resulting
	 * values will lie in the range [-50, 50] in world space, which would be [-0.5, 0.5] in the landscape local heights if the Z
	 * scale is 100.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	double WorldSpaceEncodingScale = 1;
};

/**
 * A texture-based height patch
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = Landscape, meta = (BlueprintSpawnableComponent))
class LANDSCAPEPATCH_API ULandscapeTextureHeightPatch : public ULandscapeTexturePatchBase
{
	GENERATED_BODY()

public:

	// For now the patch is largely editor-only
#if WITH_EDITOR
	virtual UTextureRenderTarget2D* Render_Native(bool InIsHeightmap,
		UTextureRenderTarget2D* InCombinedResult,
		const FName& InWeightmapLayerName) override;
	
	// ULandscapeTexturePatchBase
	virtual bool SetTextureResolution(FVector2D ResolutionIn) override;
	virtual bool SetSourceMode(ELandscapeTexturePatchSourceMode NewMode, bool bDeleteUnusedInternalTextures = true) override;
	
	/**
	 * Just like SetSourceMode, but resets encoding mode and settings to mode-specific defaults
	 */
	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	bool ResetSourceMode(ELandscapeTexturePatchSourceMode NewMode);

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

	/**
	 * Changes the render target format of the internal render target. This will usually result
	 * in a clearing the render target since it usually has to be rebuild.
	 */
	UFUNCTION(BlueprintCallable, Category = LandscapePatch, meta = (ETextureRenderTargetFormat = "ETextureRenderTargetFormat::RTF_R32f"))
	void SetInternalRenderTargetFormat(ETextureRenderTargetFormat Format);

	/**
	 * Set the height encoding mode for the patch, which determines how stored values in the patch
	 * are translated into heights when applying to landscape. 
	 */
	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	void SetSourceEncodingMode(ELandscapeTextureHeightPatchEncoding EncodingMode) 
	{ 
		Modify();
		SourceEncoding = EncodingMode; 
	}

	/**
	 * Just like SetSourceEncodingMode, but resets ZeroInEncoding and WorldSpaceEncodingScale to mode-specific defaults.
	 */
	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	void ResetSourceEncodingMode(ELandscapeTextureHeightPatchEncoding EncodingMode);

	/**
	 * Set settings that determine how values in the patch are translated into heights. This is only
	 * used if the encoding mode is not NativePackedHeight, where values are expected to be already
	 * in the same space as the landscape heightmap.
	 */
	UFUNCTION(BlueprintCallable, Category = LandscapePatch)
	void SetEncodingSettings(const FLandscapeTexturePatchEncodingSettings& Settings)
	{
		Modify();
		EncodingSettings = Settings;
	}
#endif

	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	void SetFalloff(float FalloffIn) 
	{ 
		Modify();
		Falloff = FalloffIn; 
	}

	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	void SetBlendMode(ELandscapeTextureHeightPatchBlendMode BlendModeIn) 
	{ 
		Modify();
		BlendMode = BlendModeIn; 
	}

	/**
	 * Determine whether the patch Z relative to the landscape affects the results. See comment for bUsePatchZAsReference.
	 */
	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	void SetUsePatchZAsReference(bool bUsePatchZIn) 
	{ 
		Modify();
		bUsePatchZAsReference = bUsePatchZIn; 
	}

	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	void SetUseTextureAlphaChannel(bool bUse) 
	{ 
		Modify();
		bUseTextureAlphaChannel = bUse; 
	}


protected:

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

	/** How the values stored in the patch represent the height. */
	UPROPERTY(EditAnywhere, Category = Settings, AdvancedDisplay, meta = (EditConditionHides,
		EditCondition = "SourceMode != ELandscapeTexturePatchSourceMode::InternalTexture"))
	ELandscapeTextureHeightPatchEncoding SourceEncoding = ELandscapeTextureHeightPatchEncoding::NativePackedHeight;

	UPROPERTY(EditAnywhere, Category = Settings, AdvancedDisplay, meta = (UIMin = "0", UIMax = "1", EditConditionHides,
		EditCondition = "SourceMode != ELandscapeTexturePatchSourceMode::InternalTexture && SourceEncoding != ELandscapeTextureHeightPatchEncoding::NativePackedHeight"))
	FLandscapeTexturePatchEncodingSettings EncodingSettings;

	/**
	 * Whether to apply the patch Z scale to the height stored in the patch.
	 */
	UPROPERTY(EditAnywhere, Category = Settings, AdvancedDisplay, meta = (DisplayName = "Apply Component Z Scale"))
	bool bApplyComponentZScale = true;

	/**
	 * Controls how the patch is initialized when invoking Reinitialize().
	 */
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

	// Uprops can't be inside WITH_EDITOR, apparently. Could put in WITH_EDITOR_DATA.
	UPROPERTY()
	TEnumAsByte<ETextureRenderTargetFormat> InternalRenderTargetFormat = ETextureRenderTargetFormat::RTF_R32f;

	// When loading the patch, the landscape may not be available to provide the scale, so we save
	// it in case we need it.
	// TODO: It would be cleaner to save all conversion parameters but then we have to make FConvertToNativeLandscapePatchParams
	// into a UStruct, etc. Revisit if needed later, but for now definitely keep private.
	UPROPERTY()
	float SavedConversionHeightScale;

#if WITH_EDITOR
	// These are used to convert render targets whose formats are not the usual two-channel 16bit values that
	// we store in an internal texture. We convert these render targets to the native landscape height representation
	// and store that in an internal texture.
	UE::Landscape::FConvertToNativeLandscapePatchParams GetConversionParams();
	void ConvertInternalRenderTargetToNativeTexture(bool bBlock);
	void ConvertInternalRenderTargetBackFromNativeTexture(bool bLoading = false);

	void UpdateShaderParams(UE::Landscape::FApplyLandscapeTextureHeightPatchPS::FParameters& Params, 
		const FIntPoint& DestinationResolution, FIntRect& DestinationBoundsOut) const;

	bool ResizeRenderTargetIfNeeded(int32 TextureSizeX, int32 TextureSizeY);
	bool ResizeTextureIfNeeded(int32 TextureSizeX, int32 TextureSizeY, bool bClear, bool bUpdateResource);
#endif
};