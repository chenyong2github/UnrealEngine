// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "Physics/CollisionPropertySets.h"
#include "PhysicsInspectorTool.generated.h"

class UPreviewGeometry;

UCLASS()
class MESHMODELINGTOOLS_API UPhysicsInspectorToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};




/**
 * Mesh Inspector Tool for visualizing mesh information
 */
UCLASS()
class MESHMODELINGTOOLS_API UPhysicsInspectorTool : public UMultiSelectionTool
{
	GENERATED_BODY()
public:
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void OnTick(float DeltaTime) override;

	virtual bool HasCancel() const override { return false; }
	virtual bool HasAccept() const override { return false; }
	virtual bool CanAccept() const override { return false; }

protected:

	UPROPERTY()
	UCollisionGeometryVisualizationProperties* VizSettings = nullptr;

	UPROPERTY()
	TArray<UPhysicsObjectToolPropertySet*> ObjectData;

	UPROPERTY()
	UMaterialInterface* LineMaterial = nullptr;

protected:
	UPROPERTY()
	TArray<UPreviewGeometry*> PreviewElements;

	// these are TSharedPtr because TPimplPtr cannot currently be added to a TArray?
	TArray<TSharedPtr<FPhysicsDataCollection>> PhysicsInfos;

	void InitializeGeometry(const FPhysicsDataCollection& PhysicsData, UPreviewGeometry* PreviewGeom);
	void InitializeObjectProperties(const FPhysicsDataCollection& PhysicsData, UPhysicsObjectToolPropertySet* PropSet);

	bool bVisualizationDirty = false;
	void UpdateVisualization();
};
