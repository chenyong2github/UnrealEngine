// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "ModelingOperators.h" //IDynamicMeshOperatorFactory
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
class UMeshOpPreviewWithBackgroundCompute;
class UGenerateStaticMeshLODAssetTool;
namespace GenerateStaticMeshLODAssetLocals
{
	class FGenerateStaticMeshLODAssetOperatorFactory;
}

UENUM()
enum class EGenerateLODAssetOutputMode : uint8
{
	UpdateExistingAsset = 0,
	CreateNewAsset = 1
};


/**
 * Tool builder
 */
UCLASS()
class MESHLODTOOLSET_API UGenerateStaticMeshLODAssetToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IAssetGenerationAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;

protected:
	virtual const FToolTargetTypeRequirements& GetTargetRequirements() const override;
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

	/** If true, the high-resolution input mesh is stored as HD source mesh in the Asset */
	UPROPERTY(EditAnywhere, Category = AssetOptions, meta = (EditCondition = "OutputMode == EGenerateLODAssetOutputMode::UpdateExistingAsset"))
	bool bSaveAsHDSource = true;


	UPROPERTY(EditAnywhere, Category = Settings)
	bool bParallelExecution = false;

	UPROPERTY(EditAnywhere, Category = Settings)
	FGenerateStaticMeshLODProcessSettings GeneratorSettings;

	// ------------
	// Code copied from UPolygroupLayersProperties

	UPROPERTY(EditAnywhere, Category = Settings, meta = (GetOptions = GetGroupLayersFunc))
	FName CollisionGroupLayerName = TEXT("Default");

	// this function is called provide set of available group layers
	UFUNCTION()
	TArray<FString> GetGroupLayersFunc()
	{
		return GroupLayersList;
	}

	// internal list used to implement above
	UPROPERTY(meta = (TransientToolProperty))
	TArray<FString> GroupLayersList;

	void InitializeGroupLayers(const FDynamicMesh3* Mesh)
	{
		GroupLayersList.Reset();
		GroupLayersList.Add(TEXT("Default"));		// always have standard group
		if (Mesh->Attributes())
		{
			for (int32 k = 0; k < Mesh->Attributes()->NumPolygroupLayers(); k++)
			{
				FName Name = Mesh->Attributes()->GetPolygroupLayer(k)->GetName();
				GroupLayersList.Add(Name.ToString());
			}
		}

		if (GroupLayersList.Contains(CollisionGroupLayerName.ToString()) == false)		// discard restored value if it doesn't apply
		{
			CollisionGroupLayerName = FName(GroupLayersList[0]);
		}
	}

	// ------------

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

	friend class GenerateStaticMeshLODAssetLocals::FGenerateStaticMeshLODAssetOperatorFactory;

	UPROPERTY()
	UGenerateStaticMeshLODAssetToolProperties* BasicProperties;

	UPROPERTY()
	UMeshOpPreviewWithBackgroundCompute* PreviewWithBackgroundCompute = nullptr;

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

	UPROPERTY()
	UGenerateStaticMeshLODProcess* GenerateProcess;

	TUniquePtr<UE::Geometry::IDynamicMeshOperatorFactory> OpFactory;

	void OnSettingsModified();

	bool bCollisionVisualizationDirty = false;
	void UpdateCollisionVisualization();


	void CreateNewAsset();
	void UpdateExistingAsset();



};
