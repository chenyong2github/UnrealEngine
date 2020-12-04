// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "DynamicMesh3.h"
#include "PreviewMesh.h"
#include "Drawing/PreviewGeometryActor.h"
#include "Graphs/GenerateStaticMeshLODProcess.h"
#include "Physics/CollisionPropertySets.h"
#include "GenerateStaticMeshLODAssetTool.generated.h"


// predeclarations
struct FMeshDescription;
class USimpleDynamicMeshComponent;
class IAssetGenerationAPI;


UENUM()
enum class EGenerateLODAssetOutputMode : uint8
{
	UpdateExistingAsset = 0,
	CreateNewAsset = 1
};


/**
 *
 */
UCLASS()
class MESHLODTOOLSET_API UGenerateStaticMeshLODAssetToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IAssetGenerationAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};


/**
 * Standard properties
 */
UCLASS()
class MESHLODTOOLSET_API UGenerateStaticMeshLODAssetToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = AssetOptions, meta = (TransientToolProperty))
	EGenerateLODAssetOutputMode OutputMode = EGenerateLODAssetOutputMode::UpdateExistingAsset;

	UPROPERTY(EditAnywhere, Category = AssetOptions, meta = (TransientToolProperty))
	FString OutputName;

	/** Base name for newly-generated asset */
	UPROPERTY(EditAnywhere, Category = AssetOptions, meta = (TransientToolProperty))
	FString GeneratedSuffix;

	UPROPERTY(EditAnywhere, Category = Settings)
	bool bParallelExecution = false;

	/** Base name for newly-generated asset */
	UPROPERTY(EditAnywhere, Category = Settings)
	FGenerateStaticMeshLODProcessSettings GeneratorSettings;

	UPROPERTY(VisibleAnywhere, Category = Previews)
	TArray<UTexture2D*> PreviewTextures;

};






/**
 * Simple tool to combine multiple meshes into a single mesh asset
 */
UCLASS()
class MESHLODTOOLSET_API UGenerateStaticMeshLODAssetTool : public UMultiSelectionTool
{
	GENERATED_BODY()

public:
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void SetWorld(UWorld* World);
	virtual void SetAssetAPI(IAssetGenerationAPI* AssetAPI);

	virtual void OnTick(float DeltaTime);

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;

protected:

	UPROPERTY()
	UGenerateStaticMeshLODAssetToolProperties* BasicProperties;

	UPROPERTY()
	UPreviewMesh* PreviewMesh;

	UPROPERTY()
	TArray<UTexture2D*> PreviewTextures;

	UPROPERTY()
	TArray<UMaterialInterface*> PreviewMaterials;


protected:

	UPROPERTY()
	UCollisionGeometryVisualizationProperties* CollisionVizSettings = nullptr;

	UPROPERTY()
	UPhysicsObjectToolPropertySet* ObjectData = nullptr;

	UPROPERTY()
	UMaterialInterface* LineMaterial = nullptr;

	UPROPERTY()
	UPreviewGeometry* CollisionPreview;

protected:
	UWorld* TargetWorld;
	IAssetGenerationAPI* AssetAPI;

	TUniquePtr<FGenerateStaticMeshLODProcess> GenerateProcess;

	void OnSettingsModified();

	bool bPreviewValid;
	void ValidatePreview();

	bool bCollisionVisualizationDirty = false;
	void UpdateCollisionVisualization();


	void CreateNewAsset();
	void UpdateExistingAsset();



};
