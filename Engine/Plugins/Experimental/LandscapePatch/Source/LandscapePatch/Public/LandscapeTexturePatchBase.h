// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "LandscapePatchComponent.h"

#include "LandscapeTexturePatchBase.generated.h"

class UTexture;
class UTexture2D;
class UTextureRenderTarget2D;

/**
 * Determines how the patch stores its information, which affects its memory usage in editor (not in runtime,
 * since patches are baked directly into landscape and removed for runtime).
 */
UENUM(BlueprintType)
enum class ELandscapeTexturePatchSourceMode : uint8
{
	/**
	 * The data will be read from an internally-stored UTexture2D. In this mode, the patch can't be written-to via 
	 * blueprints, but it avoids storing the extra render target needed for TextureBackedRenderTarget.
	 */
	 InternalTexture,

	 /**
	 * The patch data will be read from an internally-stored render target, which can be written to via Blueprints
	 * and which gets serialized to an internally stored UTexture2D when needed. Uses double the memory of InternalTexture.
	 */
	TextureBackedRenderTarget,

	 /**
	  * The data will be read from a UTexture asset (which can be a render target). Allows multiple patches
	  * to share the same texture.
	  */
	  TextureAsset
};

/**
 * A texture-based landscape patch. Base class for height patches (and, to be implemented, for weight patches).
 */
//~ We use the Base suffix here in part to reserve the ULandscapeTexturePatch name for potential later use.
UCLASS(Blueprintable, BlueprintType, Abstract)
class LANDSCAPEPATCH_API ULandscapeTexturePatchBase : public ULandscapePatchComponent
{
	GENERATED_BODY()

public:

	/**
	 * Gets an internally-stored render target that can be written to by blueprints.  However, will be null if
	 * source mode is not set to TextureBackedRenderTarget.
	 */
	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	UTextureRenderTarget2D* GetInternalRenderTarget() { return InternalRenderTarget;}

	// This needs to be public so that we can take the internal texture and write it to an external one,
	// but unclear whether we want to expose it to blueprints, since it's a fairly internal thing.
	UTexture2D* GetInternalTexture() { return InternalTexture; }

	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	void SetTextureAsset(UTexture* TextureIn);

	virtual ELandscapeTexturePatchSourceMode GetSourceMode() const { return SourceMode; }
	/**
	 * Changes source mode. When changing between internal texture/rendertarget modes, existing data 
	 * is copied from one to the other.
	 * 
	 * @return true If successful.
	 */
	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	virtual bool SetSourceMode(ELandscapeTexturePatchSourceMode NewMode, bool bInitializeIfRenderTarget = true) { return false; }

	/**
	 * Gets the transform from patch to world. The transform is based off of the component
	 * transform, but with rotation changed to align to the landscape, only using the yaw
	 * to rotate it relative to the landscape.
	 */
	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	virtual FTransform GetPatchToWorldTransform() const;

	/**
	 * Gives size in unscaled world coordinates (ie before applying patch transform) of the patch as measured 
	 * between the centers of the outermost pixels. Measuring the coverage this way means that a patch can 
	 * affect the same region of the landscape regardless of patch resolution.
	 * This is also the range across which bilinear interpolation always has correct values, so the area outside 
	 * this center portion is usually set as a "dead" border that doesn't affect the landscape.
	 */
	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	virtual FVector2D GetUnscaledCoverage() const { return FVector2D(UnscaledPatchCoverage); }

	/**
	 * Set the patch coverage (see GetUnscaledCoverage for description).
	 */
	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	virtual void SetUnscaledCoverage(FVector2D Coverage) { UnscaledPatchCoverage = Coverage; }

	/**
	 * Gives size in unscaled world coordinates of the patch in the world, based off of UnscaledCoverage and
	 * texture resolution (i.e., adds a half-pixel around UnscaledCoverage).
	 */
	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	virtual FVector2D GetFullUnscaledWorldSize() const;

	/** 
	 * Gets the size (in pixels) of the currently used texture. Depends on SourceMode, 
	 * and returns false if currently used texture is not allocated/set.
	 */
	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	virtual UPARAM(DisplayName = "Success") bool GetTextureResolution(FVector2D& SizeOut) const;

	/**
	 * Sets the resolution of the currently used internal texture or render target. Has no effect
	 * if the source mode is set to an external texture asset.
	 * 
	 * @return true if successful.
	 */
	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	virtual UPARAM(DisplayName = "Success") bool SetTextureResolution(FVector2D ResolutionIn) { return false; }

	/**
	 * Given the landscape resolution, current patch coverage, and a landscape resolution multiplier, gives the
	 * needed resolution of the landscape patch. I.e., figures out the number of pixels in the landcape that
	 * would be in a region of such size, and then uses the resolution multiplier to give a result.
	 * 
	 * @return true if successful (may fail if landscape is not set, for instance)
	 */
	UFUNCTION(BlueprintCallable, Category = "LandscapePatch", meta = (ResolutionMultiplier = "1.0"))
	virtual UPARAM(DisplayName = "Success") bool GetInitResolutionFromLandscape(float ResolutionMultiplier, FVector2D& ResolutionOut) const;

protected:
	/**
	 * How the heightmap of the patch is stored.
	 */
	UPROPERTY(EditAnywhere, Category = Settings, meta = (DisplayPriority = 10))
	ELandscapeTexturePatchSourceMode SourceMode = ELandscapeTexturePatchSourceMode::InternalTexture;

	/**
	 * Texture used when source mode is set to a texture asset, or for intializing from a texture asset.
	 */
	UPROPERTY(EditAnywhere, Category = Settings, meta = (EditConditionHides, DisplayPriority = 20, DisplayName = "Texture Asset", 
		EditCondition = "SourceMode == ELandscapeTexturePatchSourceMode::TextureAsset || bShowTextureAssetProperty", 
		DisallowedAssetDataTags = "VirtualTextureStreaming=True"))
	TObjectPtr<UTexture> TextureAsset = nullptr;

	/** At scale 1.0, the X and Y of the region affected by the height patch. This corresponds to the distance from the center
	 of the first pixel to the center of the last pixel in the patch texture in the X and Y directions. */
	UPROPERTY(EditAnywhere, Category = Settings, meta = (UIMin = "0", ClampMin = "0", DisplayPriority = 30))
	FVector2D UnscaledPatchCoverage = FVector2D(2000, 2000);

	/** Not directly settable via detail panel- for display/debugging purposes only. */
	UPROPERTY(VisibleAnywhere, Category = Settings, AdvancedDisplay, meta = (EditConditionHides, DisplayPriority = 40,
		EditCondition = "SourceMode != ELandscapeTexturePatchSourceMode::TextureAsset"))
	TObjectPtr<UTexture2D> InternalTexture = nullptr;

	/** Not directly settable via detail panel- for display/debugging purposes only. */
	UPROPERTY(VisibleAnywhere, Category = Settings, Transient, DuplicateTransient, AdvancedDisplay, meta = (DisplayPriority = 50,
		EditConditionHides, EditCondition = "SourceMode == ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget"))
	TObjectPtr<UTextureRenderTarget2D> InternalRenderTarget = nullptr;

	// Lets subclasses control whether the TextureAsset property is visible.
	UPROPERTY()
	bool bShowTextureAssetProperty;

	// Adds a half-pixel around the border of UnscaledPatchCoverage, where pixel size depends on given resolution
	FVector2D GetFullUnscaledWorldSizeForResolution(const FVector2D& ResolutionIn) const;
};