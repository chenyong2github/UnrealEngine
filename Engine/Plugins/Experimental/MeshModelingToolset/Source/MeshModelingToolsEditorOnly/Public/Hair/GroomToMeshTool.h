// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SingleSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "DynamicMesh3.h"
#include "PreviewMesh.h"
#include "Drawing/PreviewGeometryActor.h"

#include "GroomToMeshTool.generated.h"

class AGroomActor;
class AStaticMeshActor;

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UGroomToMeshToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()
public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};




UENUM()
enum class EGroomToMeshUVMode
{
	PlanarSplitting = 1,
	MinimalConformal = 2,
	PlanarSplitConformal = 3
};



UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UGroomToMeshToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** The size of the geometry bounding box major axis measured in voxels */
	UPROPERTY(EditAnywhere, Category = Meshing, meta = (UIMin = "8", UIMax = "512", ClampMin = "8", ClampMax = "1024"))
	int32 VoxelCount = 64;

	UPROPERTY(EditAnywhere, Category = Meshing, meta = (UIMin = "0.5", UIMax = "16.0", ClampMin = "0.1", ClampMax = "128.0"))
	float BlendPower = 1.0;

	UPROPERTY(EditAnywhere, Category = Meshing, meta = (UIMin = "0.1", UIMax = "4.0", ClampMin = "0.1", ClampMax = "128.0"))
	float RadiusScale = 0.5;


	UPROPERTY(EditAnywhere, Category = Morphology)
	bool bApplyMorphology = true;

	UPROPERTY(EditAnywhere, Category = Morphology, meta = (UIMin = "0.0", UIMax = "50.0", ClampMin = "0.0", ClampMax = "128.0"))
	float ClosingDist = 2.0;

	UPROPERTY(EditAnywhere, Category = Morphology, meta = (UIMin = "0.0", UIMax = "50.0", ClampMin = "0.0", ClampMax = "128.0"))
	float OpeningDist = 0.25;



	UPROPERTY(EditAnywhere, Category = Clipping)
	bool bClipToHead = true;

	// todo: this probably also needs to support skeletal mesh
	UPROPERTY(EditAnywhere, Category = Clipping)
	TLazyObjectPtr<AStaticMeshActor> ClipMeshActor;


	UPROPERTY(EditAnywhere, Category = Smoothing)
	bool bSmooth = true;

	UPROPERTY(EditAnywhere, Category = Smoothing, meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float Smoothness = 0.15;

	UPROPERTY(EditAnywhere, Category = Smoothing, meta = (UIMin = "-1.0", UIMax = "1.0", ClampMin = "-2.0", ClampMax = "2.0"))
	float VolumeCorrection = -0.25f;


	UPROPERTY(EditAnywhere, Category = Simplification)
	bool bSimplify = false;

	/** Target triangle count */
	UPROPERTY(EditAnywhere, Category = Simplification, meta = (UIMin = "4", UIMax = "5000", ClampMin = "1", ClampMax = "9999999999", EditCondition = "bSimplify == true"))
	int VertexCount = 500;


	UPROPERTY(EditAnywhere, Category = UVGeneration)
	EGroomToMeshUVMode UVMode = EGroomToMeshUVMode::MinimalConformal;


	UPROPERTY(EditAnywhere, Category = Visualization)
	bool bShowSideBySide = true;

	UPROPERTY(EditAnywhere, Category = Visualization)
	bool bShowGuides = false;

	UPROPERTY(EditAnywhere, Category = Visualization)
	bool bShowUVs = false;

};



/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UGroomToMeshTool : public UInteractiveTool
{
	GENERATED_BODY()

public:
	UGroomToMeshTool();

	virtual void SetWorld(UWorld* World) { this->TargetWorld = World; }
	virtual void SetAssetAPI(IToolsContextAssetAPI* InAssetAPI) { this->AssetAPI = InAssetAPI; }
	virtual void SetSelection(AGroomActor* Groom);

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void OnTick(float DeltaTime) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override;
	virtual bool CanAccept() const override;

protected:
	UPROPERTY()
	UGroomToMeshToolProperties* Settings;

	UPROPERTY()
	UPreviewMesh* PreviewMesh;

	UPROPERTY()
	TLazyObjectPtr<AGroomActor> TargetGroom;

	UPROPERTY()
	UPreviewGeometry* PreviewGeom;

protected:
	UPROPERTY()
	UMaterialInterface* MeshMaterial;

	UPROPERTY()
	UMaterialInterface* UVMaterial;

protected:
	UWorld* TargetWorld = nullptr;
	IToolsContextAssetAPI* AssetAPI = nullptr;

	FDynamicMesh3 CurrentMesh;

	void RecalculateMesh();

	void UpdateLineSet();

	bool bResultValid = false;
	bool bVisualizationChanged = false;

	struct FVoxelizeSettings
	{
		int32 VoxelCount;
		float BlendPower;
		float RadiusScale;
		
		bool operator==(const FVoxelizeSettings& Other) const
		{
			return VoxelCount == Other.VoxelCount && BlendPower == Other.BlendPower && RadiusScale == Other.RadiusScale;
		}
	};
	FVoxelizeSettings CachedVoxelizeSettings;
	TSharedPtr<FDynamicMesh3> CurrentVoxelizeResult;
	TSharedPtr<FDynamicMesh3> UpdateVoxelization();



	struct FMorphologySettings
	{
		TSharedPtr<FDynamicMesh3> InputMesh;
		int32 VoxelCount;
		float CloseDist;
		float OpenDist;

		bool operator==(const FMorphologySettings& Other) const
		{
			return InputMesh.Get() == Other.InputMesh.Get() && VoxelCount == Other.VoxelCount && OpenDist == Other.OpenDist && CloseDist == Other.CloseDist;
		}
	};
	FMorphologySettings CachedMorphologySettings;
	TSharedPtr<FDynamicMesh3> CachedMorphologyResult;
	TSharedPtr<FDynamicMesh3> UpdateMorphology(TSharedPtr<FDynamicMesh3> InputMesh);


	struct FClipMeshSettings
	{
		TSharedPtr<FDynamicMesh3> InputMesh;
		AActor* ClipSource;

		bool operator==(const FClipMeshSettings& Other) const
		{
			return InputMesh.Get() == Other.InputMesh.Get() && ClipSource == Other.ClipSource;
		}
	};
	FClipMeshSettings CachedClipMeshSettings;
	TSharedPtr<FDynamicMesh3> CachedClipMeshResult;
	TSharedPtr<FDynamicMesh3> UpdateClipMesh(TSharedPtr<FDynamicMesh3> InputMesh);


	struct FSmoothingSettings
	{
		TSharedPtr<FDynamicMesh3> InputMesh;
		float Smoothness;
		float VolumeCorrection;

		bool operator==(const FSmoothingSettings& Other) const
		{
			return InputMesh.Get() == Other.InputMesh.Get() && Smoothness == Other.Smoothness && VolumeCorrection == Other.VolumeCorrection;
		}
	};
	FSmoothingSettings CachedSmoothSettings;
	TSharedPtr<FDynamicMesh3> CachedSmoothResult;
	TSharedPtr<FDynamicMesh3> UpdateSmoothing(TSharedPtr<FDynamicMesh3> InputMesh);


	struct FSimplifySettings
	{
		TSharedPtr<FDynamicMesh3> InputMesh;
		int32 TargetCount;

		bool operator==(const FSimplifySettings& Other) const
		{
			return TargetCount == Other.TargetCount && InputMesh.Get() == Other.InputMesh.Get();
		}
	};
	FSimplifySettings CachedSimplifySettings;
	TSharedPtr<FDynamicMesh3> CachedSimplifyResult;
	TSharedPtr<FDynamicMesh3> UpdateSimplification(TSharedPtr<FDynamicMesh3> InputMesh);


	struct FPostprocessSettings
	{
		TSharedPtr<FDynamicMesh3> InputMesh;
		EGroomToMeshUVMode UVGenMode;

		bool operator==(const FPostprocessSettings& Other) const
		{
			return InputMesh.Get() == Other.InputMesh.Get() && UVGenMode == Other.UVGenMode;
		}
	};
	FPostprocessSettings CachedPostprocessSettings;
	TSharedPtr<FDynamicMesh3> CachedPostprocessResult;
	TSharedPtr<FDynamicMesh3> UpdatePostprocessing(TSharedPtr<FDynamicMesh3> InputMesh);


	TSharedPtr<FDynamicMesh3> UpdateUVs(TSharedPtr<FDynamicMesh3> InputMesh, EGroomToMeshUVMode UVMode);

	TSharedPtr<FDynamicMesh3> UpdateUVs_ExpMapPlaneSplits(TSharedPtr<FDynamicMesh3> InputMesh, bool bRecalcAsConformal);
	TSharedPtr<FDynamicMesh3> UpdateUVs_MinimalConformal(TSharedPtr<FDynamicMesh3> InputMesh);

	void UpdatePreview(TSharedPtr<FDynamicMesh3> ResultMesh);
};
