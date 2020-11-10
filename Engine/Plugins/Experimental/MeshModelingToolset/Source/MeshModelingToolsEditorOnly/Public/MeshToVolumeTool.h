// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "Engine/Classes/Engine/BlockingVolume.h"
#include "SingleSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "DynamicMesh3.h"
#include "PreviewMesh.h"
#include "Drawing/LineSetComponent.h"
#include "PropertySets/OnAcceptProperties.h"
#include "MeshToVolumeTool.generated.h"

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UMeshToVolumeToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()
public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState & SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState & SceneState) const override;
};




UENUM()
enum class EMeshToVolumeMode
{
	/** Create a separate Volume Face for each Triangle */
	TriangulatePolygons,
	/** Create Volume Faces based on Planar Polygons */
	MinimalPolygons
};



UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UMeshToVolumeToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Method for converting the input mesh to a set of Planar Polygonal Faces in the output Volume. */
	UPROPERTY(EditAnywhere, Category = ConversionOptions)
	EMeshToVolumeMode ConversionMode = EMeshToVolumeMode::MinimalPolygons;

	/** Type of new Volume to create on Accept */
	UPROPERTY(EditAnywhere, Category = NewVolume, meta = (EditCondition = "TargetVolume == nullptr") )
	TSubclassOf<class AVolume> NewVolumeType = ABlockingVolume::StaticClass();

	/** If set, the target Volume will be updated, rather than creating a new Volume. */
	UPROPERTY(EditAnywhere, Category = UpdateExisting)
	TLazyObjectPtr<AVolume> TargetVolume;
};


struct FModelFace
{
	FFrame3d Plane;
	TArray<FVector3d> BoundaryLoop;
};


/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UMeshToVolumeTool : public USingleSelectionTool
{
	GENERATED_BODY()

public:
	UMeshToVolumeTool();

	virtual void SetAssetAPI(IToolsContextAssetAPI* InAssetAPI) { this->AssetAPI = InAssetAPI; }

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void OnTick(float DeltaTime) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }

protected:
	UPROPERTY()
	UMeshToVolumeToolProperties* Settings;

	UPROPERTY()
	UOnAcceptHandleSourcesProperties* HandleSourcesProperties;

	UPROPERTY()
	UPreviewMesh* PreviewMesh;

	UPROPERTY()
	ULineSetComponent* VolumeEdgesSet;

protected:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	FDynamicMesh3 InputMesh;

	void RecalculateVolume();
	void RecalculateVolume_Polygons();
	void RecalculateVolume_Triangles();

	void UpdateLineSet();

	bool bVolumeValid = false;
	TArray<FModelFace> Faces;

	void BakeToVolume(AVolume* TargetVolume);

};
