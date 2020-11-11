// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SubdividePoly.h"
#include "CoreMinimal.h"
#include "InteractiveTool.h"
#include "InteractiveToolBuilder.h"
#include "ModelingOperators.h"
#include "SingleSelectionTool.h"
#include "SimpleDynamicMeshComponent.h"
#include "MeshOpPreviewHelpers.h"
#include "SubdividePolyTool.generated.h"

class USubdividePolyTool;
class USimpleDynamicMeshComponent;
class UPreviewGeometry;

/**
 * Tool builder
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API USubdividePolyToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()
public:

	bool CanBuildTool(const FToolBuilderState& SceneState) const override;

	UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};

/**
 * Properties
 */

UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API USubdividePolyToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category=Settings, meta = (UIMin = "1", ClampMin = "1"))
	int SubdivisionLevel = 3;

	UPROPERTY(EditAnywhere, Category=Settings)
	ESubdivisionOutputNormals NormalComputationMethod = ESubdivisionOutputNormals::Generated;

	UPROPERTY(EditAnywhere, Category=Settings)
	ESubdivisionOutputUVs UVComputationMethod = ESubdivisionOutputUVs::Interpolated;

	UPROPERTY(EditAnywhere, Category = Settings)
	bool bRenderGroups = true;

};


/**
 * Tool actual
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API USubdividePolyTool : public USingleSelectionTool
{
	GENERATED_BODY()

public:

	virtual void SetWorld(UWorld* World) { TargetWorld = World; }

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;

	void OnTick(float DeltaTime);

protected:

	friend class USubdividePolyOperatorFactory;

	UWorld* TargetWorld;

	UPROPERTY()
	USimpleDynamicMeshComponent* PreviewDynamicMeshComponent = nullptr;

	UPROPERTY()
	USubdividePolyToolProperties* Properties = nullptr;

	// The temporary actor we create internally to own the preview DynamicMeshComponent
	UPROPERTY()
	APreviewMeshActor* TemporaryParentActor = nullptr;

	// Input mesh
	TSharedPtr<FDynamicMesh3> OriginalMesh;

	UPROPERTY()
	UPreviewGeometry* PreviewGeometry = nullptr;

	bool bPreviewGeometryNeedsUpdate;
	void CreateOrUpdatePreviewGeometry();
};

