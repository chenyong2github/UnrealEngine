// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SingleSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "DynamicMesh3.h"
#include "Physics/CollisionPropertySets.h"
#include "ExtractCollisionGeometryTool.generated.h"

class UPreviewGeometry;
class UPreviewMesh;

UCLASS()
class MESHMODELINGTOOLS_API UExtractCollisionGeometryToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};




/**
 * Mesh Inspector Tool for visualizing mesh information
 */
UCLASS()
class MESHMODELINGTOOLS_API UExtractCollisionGeometryTool : public USingleSelectionTool
{
	GENERATED_BODY()
public:
	virtual void SetWorld(UWorld* World) { this->TargetWorld = World; }
	virtual void SetAssetAPI(IToolsContextAssetAPI* InAssetAPI) { this->AssetAPI = InAssetAPI; }

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void OnTick(float DeltaTime) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;

protected:

	UPROPERTY()
	UCollisionGeometryVisualizationProperties* VizSettings = nullptr;

	UPROPERTY()
	UPhysicsObjectToolPropertySet* ObjectProps;

protected:
	UPROPERTY()
	UPreviewGeometry* PreviewElements;

	UPROPERTY()
	UPreviewMesh* PreviewMesh;

	// these are TSharedPtr because TPimplPtr cannot currently be added to a TArray?
	TSharedPtr<FPhysicsDataCollection> PhysicsInfo;

	UWorld* TargetWorld = nullptr;
	IToolsContextAssetAPI* AssetAPI = nullptr;

	FDynamicMesh3 CurrentMesh;
	bool bResultValid = false;
	void RecalculateMesh();

	bool bVisualizationDirty = false;
	void UpdateVisualization();
};
