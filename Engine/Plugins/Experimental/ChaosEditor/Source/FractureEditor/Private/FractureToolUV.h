// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FractureTool.h"

#include "FractureToolUV.generated.h"

class FFractureToolContext;

UENUM()
enum class EAutoUVTextureResolution : int32
{
	Resolution16 = 16 UMETA(DisplayName = "16 x 16"),
	Resolution32 = 32 UMETA(DisplayName = "32 x 32"),
	Resolution64 = 64 UMETA(DisplayName = "64 x 64"),
	Resolution128 = 128 UMETA(DisplayName = "128 x 128"),
	Resolution256 = 256 UMETA(DisplayName = "256 x 256"),
	Resolution512 = 512 UMETA(DisplayName = "512 x 512"),
	Resolution1024 = 1024 UMETA(DisplayName = "1024 x 1024"),
	Resolution2048 = 2048 UMETA(DisplayName = "2048 x 2048"),
	Resolution4096 = 4096 UMETA(DisplayName = "4096 x 4096"),
	Resolution8192 = 8192 UMETA(DisplayName = "8192 x 8192")
};

UENUM()
enum class ETextureType
{
	ThicknessAndSurfaceAttributes,
	SpatialGradients
};

/** Settings specifically related to the one-time destructive fracturing of a mesh **/
UCLASS(config = EditorPerProjectUserSettings)
class UFractureAutoUVSettings : public UFractureToolSettings
{
public:

	GENERATED_BODY()

	UFractureAutoUVSettings(const FObjectInitializer& ObjInit)
		: Super(ObjInit)
	{}

	/** Whether to layout UVs (to ensure unique texels for each face), or just use the existing UVs */
	UPROPERTY(EditAnywhere, Category = Atlas)
	bool bDoUVLayout = true;

	/** The pixel resolution of the generated map */
	UPROPERTY(EditAnywhere, Category = MapSettings)
	EAutoUVTextureResolution Resolution = EAutoUVTextureResolution::Resolution512;

	/** Space to leave between UV islands, measured in texels */
	UPROPERTY(EditAnywhere, Category = MapSettings, meta = (ClampMin = "1", ClampMax = "10", UIMax = "4"))
	int32 GutterSize = 2;

	/** The resulting automatically-generated texture map */
	UPROPERTY(VisibleAnywhere, Category = MapSettings)
	UTexture2D* Result;

	/** Whether to prompt user for an asset name for each generated texture, or automatically place them next to the source geometry collections */
	UPROPERTY(EditAnywhere, Category = MapSettings)
	bool bPromptToSave = true;

	/** Whether to allow the new texture to overwrite an existing texture */
	UPROPERTY(EditAnywhere, Category = MapSettings)
	bool bReplaceExisting = true;

	/** Which standard set of texture channels to bake */
	UPROPERTY(EditAnywhere, Category = AttributesToBake)
	ETextureType BakeTextureType = ETextureType::ThicknessAndSurfaceAttributes;

	/** Bake the distance to the external surface to a texture channel (red) */
	UPROPERTY(EditAnywhere, Category = AttributesToBake, meta = (EditCondition = "BakeTextureType == ETextureType::ThicknessAndSurfaceAttributes", EditConditionHides))
	bool bDistToOuter = true;

	/** Bake the ambient occlusion of each bone (considered separately) to a texture channel (green) */
	UPROPERTY(EditAnywhere, Category = AttributesToBake, meta = (EditCondition = "BakeTextureType == ETextureType::ThicknessAndSurfaceAttributes", EditConditionHides))
	bool bAmbientOcclusion = true;

	/**
	 * Bake a smoothed curvature metric to a texture channel (blue)
	 * Specifically, this is the mean curvature of a smoothed copy of each fractured piece, baked back to the respective fracture piece.
	 */
	UPROPERTY(EditAnywhere, Category = AttributesToBake, meta = (EditCondition = "BakeTextureType == ETextureType::ThicknessAndSurfaceAttributes", EditConditionHides))
	bool bSmoothedCurvature = true;

	/** Bake the Z-component of the normal to a texture channel (alpha) */
	UPROPERTY(EditAnywhere, Category = AttributesToBake, meta = (EditCondition = "BakeTextureType == ETextureType::ThicknessAndSurfaceAttributes", EditConditionHides))
	bool bZNormal = true;

	/** Max distance to search for the outer mesh surface */
	UPROPERTY(EditAnywhere, Category = DistToOuterSettings, meta = (EditCondition = "BakeTextureType == ETextureType::ThicknessAndSurfaceAttributes && bDistToOuter", EditConditionHides, UIMin = "1", UIMax = "100", ClampMin = ".01", ClampMax = "1000"))
	double MaxDistance = 100;

	/** Number of occlusion rays */
	UPROPERTY(EditAnywhere, Category = AmbientOcclusionSettings, meta = (EditCondition = "BakeTextureType == ETextureType::ThicknessAndSurfaceAttributes && bAmbientOcclusion", EditConditionHides, UIMin = "1", UIMax = "1024", ClampMin = "0", ClampMax = "50000"))
	int OcclusionRays = 16;

	/** Pixel Radius of Gaussian Blur Kernel applied to AO map (0 will apply no blur) */
	UPROPERTY(EditAnywhere, Category = AmbientOcclusionSettings, meta = (EditCondition = "BakeTextureType == ETextureType::ThicknessAndSurfaceAttributes && bAmbientOcclusion", EditConditionHides, UIMin = "0", UIMax = "10.0", ClampMin = "0", ClampMax = "100.0"))
	double OcclusionBlurRadius = 2.25;

	/** Pixel Radius of Gaussian Blur Kernel applied to Curvature map (0 will apply no blur) */
	UPROPERTY(EditAnywhere, Category = SmoothedCurvatureSettings, meta = (EditCondition = "BakeTextureType == ETextureType::ThicknessAndSurfaceAttributes && bSmoothedCurvature", EditConditionHides, UIMin = "0", UIMax = "10.0", ClampMin = "0", ClampMax = "100.0"))
	double CurvatureBlurRadius = 2.25;

	/** Voxel resolution of smoothed shape representation */
	UPROPERTY(EditAnywhere, Category = SmoothedCurvatureSettings, meta = (EditCondition = "BakeTextureType == ETextureType::ThicknessAndSurfaceAttributes && bSmoothedCurvature", EditConditionHides, UIMin = "8", UIMax = "512", ClampMin = "4", ClampMax = "1024"))
	int VoxelResolution = 128;

	/** Amount of smoothing iterations to apply before computing curvature */
	UPROPERTY(EditAnywhere, Category = SmoothedCurvatureSettings, meta = (EditCondition = "BakeTextureType == ETextureType::ThicknessAndSurfaceAttributes && bSmoothedCurvature", EditConditionHides, UIMin = "2", UIMax = "100", ClampMin = "2", ClampMax = "1000"))
	int SmoothingIterations = 10;

	/** Distance to search for correspondence between fractured shape and smoothed shape, as factor of voxel size */
	UPROPERTY(EditAnywhere, Category = SmoothedCurvatureSettings, meta = (EditCondition = "BakeTextureType == ETextureType::ThicknessAndSurfaceAttributes && bSmoothedCurvature", EditConditionHides, UIMin = "2", UIMax = "10.0", ClampMin = "1", ClampMax = "100.0"))
	double ThicknessFactor = 4;

	/** Curvatures in the range [-MaxCurvature, MaxCurvature] will be mapped from [0,1]. Values outside that range will be clamped. */
	UPROPERTY(EditAnywhere, Category = SmoothedCurvatureSettings, meta = (EditCondition = "BakeTextureType == ETextureType::ThicknessAndSurfaceAttributes && bSmoothedCurvature", EditConditionHides, UIMin = ".01", UIMax = "1", ClampMin = ".0001", ClampMax = "10"))
	double MaxCurvature = .1;

	/** Whether to use the absolute value of the Z-Component of the normal */
	UPROPERTY(EditAnywhere, Category = ZNormalSettings, meta = (EditCondition = "BakeTextureType == ETextureType::ThicknessAndSurfaceAttributes && bZNormal", EditConditionHides))
	bool bUseAbsoluteValue = true;

};


UCLASS(DisplayName = "AutoUV Tool", Category = "FractureTools")
class UFractureToolAutoUV : public UFractureModalTool
{
public:
	GENERATED_BODY()

	UFractureToolAutoUV(const FObjectInitializer& ObjInit);

	///
	/// UFractureModalTool Interface
	///

	/** This is the Text that will appear on the tool button to execute the tool **/
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	/** This is the Text that will appear on the button to execute the fracture **/
	virtual FText GetApplyText() const override { return FText(NSLOCTEXT("AutoUV", "ExecuteAutoUV", "AutoUV")); }
	virtual FSlateIcon GetToolIcon() const override;
	virtual void RegisterUICommand(FFractureEditorCommands* BindingContext) override;
	virtual TArray<UObject*> GetSettingsObjects() const override;
	virtual void FractureContextChanged() override;
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	/** Executes function that generates new geometry. Returns the first new geometry index. */
	virtual int32 ExecuteFracture(const FFractureToolContext& FractureContext) override;
	virtual bool CanExecute() const override;
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;

	/** Gets the UI command info for this command */
	const TSharedPtr<FUICommandInfo>& GetUICommandInfo() const;

	virtual TArray<FFractureToolContext> GetFractureToolContexts() const override;


protected:
	UPROPERTY(EditAnywhere, Category = Slicing)
	TObjectPtr<UFractureAutoUVSettings> AutoUVSettings;

	bool SaveGeneratedTexture(UTexture2D* GeneratedTexture, FString ObjectBaseName, const UObject* RelativeToAsset, bool bPromptToSave, bool bAllowReplace);
};
