// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "Engine/MeshMerging.h"
#include "MergeActorsTool.h"

#include "MeshApproximationTool.generated.h"

class SMeshApproximationDialog;



UENUM()
enum class EMeshApproximationSimplificationPolicy : uint8
{
	FixedTriangleCount = 0,
	TrianglesPerArea = 1
};


USTRUCT(Blueprintable)
struct FMeshApproximationToolSettings
{
	GENERATED_BODY()

	/** Approximation Accuracy in Meters, will determine (eg) voxel resolution */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = ShapeSettings, meta = (ClampMin = "0.1"))
	float ApproximationAccuracy = 1.0;

	/** Maximum allowable voxel count along main directions. This is a limit on ApproximationAccuracy */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, AdvancedDisplay, Category = ShapeSettings, meta = (ClampMin = "64"))
	int32 ClampVoxelDimension = 512;

	/** */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = ShapeSettings, meta = (ClampMin = "0.01", ClampMax = "0.99"))
	float WindingThreshold = 0.5;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = ShapeSettings)
	bool bFillGaps = true;

	/** Distance in Meters to expand approximation to fill gaps */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = ShapeSettings, meta = (ClampMin = "0.1", EditCondition = "bFillGaps"))
	float GapDistance = 0.1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = ShapeSettings)
	EMeshApproximationSimplificationPolicy SimplifyMethod = EMeshApproximationSimplificationPolicy::FixedTriangleCount;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = ShapeSettings, meta = (ClampMin = "16", EditCondition = "SimplifyMethod == EMeshApproximationSimplificationPolicy::FixedTriangleCount" ))
	int32 TargetTriCount = 2000;

	/** Approximate Number of triangles per Square Meter */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = ShapeSettings, meta = (ClampMin = "0.01", EditCondition = "SimplifyMethod == EMeshApproximationSimplificationPolicy::TrianglesPerArea" ))
	float TrianglesPerM = 2.0f;



	/** Material simplification */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = MaterialSettings)
	FMaterialProxySettings MaterialSettings;

	/** If Value is > 1, Multisample output baked textures by this amount in each direction (eg 4 == 16x supersampling) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, AdvancedDisplay, Category = MaterialSettings, meta = (ClampMin = "0", ClampMax = "8", UIMin = "0", UIMax = "4"))
	int32 MultiSamplingAA = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, AdvancedDisplay, Category = MaterialSettings, meta = (ClampMin = "5.0", ClampMax = "160.0"))
	float CaptureFieldOfView = 30.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, AdvancedDisplay, Category = MaterialSettings, meta = (ClampMin = "0.001", ClampMax = "1000.0"))
	float NearPlaneDist = 1.0f;

	/** If Value is zero, use MaterialSettings resolution, otherwise override the render capture resolution */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, AdvancedDisplay, Category = MaterialSettings, meta = (ClampMin = "128"))
	int32 RenderCaptureResolution = 0;

	/** Equality operator. */
	bool operator==(const FMeshApproximationToolSettings& Other) const
	{
		return ApproximationAccuracy == Other.ApproximationAccuracy
			&& ClampVoxelDimension == Other.ClampVoxelDimension
			&& WindingThreshold == Other.WindingThreshold
			&& bFillGaps == Other.bFillGaps
			&& GapDistance == Other.GapDistance
			&& SimplifyMethod == Other.SimplifyMethod
			&& TargetTriCount == Other.TargetTriCount
			&& TrianglesPerM == Other.TrianglesPerM

			&& MaterialSettings == Other.MaterialSettings
			&& MultiSamplingAA == Other.MultiSamplingAA
			&& CaptureFieldOfView == Other.CaptureFieldOfView
			&& NearPlaneDist == Other.NearPlaneDist
			&& RenderCaptureResolution == Other.RenderCaptureResolution;
	}

	/** Inequality. */
	bool operator!=(const FMeshApproximationToolSettings& Other) const
	{
		return !(*this == Other);
	}

#if WITH_EDITORONLY_DATA
	/** Handles deprecated properties */
	void PostLoadDeprecated() {}		// none currently
#endif
};





/** Singleton wrapper to allow for using the setting structure in SSettingsView */
UCLASS(config = Engine)
class UMeshApproximationSettingsObject : public UObject
{
	GENERATED_BODY()
public:
	UMeshApproximationSettingsObject()
	{
	}

	static UMeshApproximationSettingsObject* Get()
	{	
		// This is a singleton, duplicate default object
		if (!bInitialized)
		{
			DefaultSettings = DuplicateObject(GetMutableDefault<UMeshApproximationSettingsObject>(), nullptr);
			DefaultSettings->AddToRoot();
			bInitialized = true;
		}

		return DefaultSettings;
	}

	static void Destroy()
	{
		if (bInitialized)
		{
			if (UObjectInitialized() && DefaultSettings)
			{
				DefaultSettings->RemoveFromRoot();
				DefaultSettings->MarkPendingKill();
			}

			DefaultSettings = nullptr;
			bInitialized = false;
		}
	}

protected:
	static bool bInitialized;
	static UMeshApproximationSettingsObject* DefaultSettings;
public:
	UPROPERTY(EditAnywhere, meta = (ShowOnlyInnerProperties), Category = ApproximationSettings)
	FMeshApproximationToolSettings Settings;
};

/**
* Mesh Proxy Tool
*/
class FMeshApproximationTool : public FMergeActorsTool
{
	friend class SMeshApproximationDialog;

public:
	FMeshApproximationTool();
	~FMeshApproximationTool();

	// IMergeActorsTool interface
	virtual TSharedRef<SWidget> GetWidget() override;
	virtual FName GetIconName() const override { return "MergeActors.MeshApproximationTool"; }
	virtual FText GetTooltipText() const override;
	virtual FString GetDefaultPackageName() const override;

protected:
	virtual bool RunMerge(const FString& PackageName, const TArray<TSharedPtr<FMergeComponentData>>& SelectedComponents) override;
	virtual const TArray<TSharedPtr<FMergeComponentData>>& GetSelectedComponentsInWidget() const override;

protected:

	/** Pointer to the mesh merging dialog containing settings for the merge */
	TSharedPtr<SMeshApproximationDialog> ProxyDialog;

	/** Pointer to singleton settings object */
	UMeshApproximationSettingsObject* SettingsObject;
};

