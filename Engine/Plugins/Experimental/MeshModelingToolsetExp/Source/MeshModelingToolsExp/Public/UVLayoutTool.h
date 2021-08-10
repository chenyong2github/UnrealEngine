// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MultiSelectionTool.h"
#include "GeometryBase.h"
#include "InteractiveToolBuilder.h"
#include "MeshOpPreviewHelpers.h"
#include "ToolDataVisualizer.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "BaseTools/SingleClickTool.h"
#include "Properties/MeshMaterialProperties.h"
#include "Properties/MeshUVChannelProperties.h"
#include "Drawing/UVLayoutPreview.h"

#include "UVLayoutTool.generated.h"


// predeclarations
struct FMeshDescription;
class UDynamicMeshComponent;
class UUVLayoutProperties;
PREDECLARE_GEOMETRY(class FDynamicMesh3);

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEXP_API UUVLayoutToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;

protected:
	virtual const FToolTargetTypeRequirements& GetTargetRequirements() const override;
};


/**
 * The level editor version of the UV layout tool.
 */
UCLASS()
class MESHMODELINGTOOLSEXP_API UUVLayoutTool : public UMultiSelectionTool
{
	GENERATED_BODY()

public:

	UUVLayoutTool();

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void SetWorld(UWorld* World, UInteractiveGizmoManager* GizmoManagerIn);

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;


public:
	int32 GetSelectedUVChannel() const;


protected:

	UPROPERTY()
	TObjectPtr<UMeshUVChannelProperties> UVChannelProperties = nullptr;

	UPROPERTY()
	TObjectPtr<UUVLayoutProperties> BasicProperties = nullptr;

	UPROPERTY()
	TObjectPtr<UExistingMeshMaterialProperties> MaterialSettings = nullptr;

	UPROPERTY()
	TArray<TObjectPtr<UMeshOpPreviewWithBackgroundCompute>> Previews;

protected:
	TArray<TSharedPtr<UE::Geometry::FDynamicMesh3, ESPMode::ThreadSafe>> OriginalDynamicMeshes;

	UWorld* TargetWorld = nullptr;

	FViewCameraState CameraState;

	void UpdateNumPreviews();

	void UpdateVisualization();

	void OnPreviewMeshUpdated(UMeshOpPreviewWithBackgroundCompute* Compute);

	void GenerateAsset(const TArray<FDynamicMeshOpResult>& Results);


protected:
	UPROPERTY()
	TObjectPtr<UUVLayoutPreview> UVLayoutView = nullptr;
};
