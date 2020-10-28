// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "MeshOpPreviewHelpers.h"
#include "ToolDataVisualizer.h"
#include "DynamicMesh3.h"
#include "BaseTools/SingleClickTool.h"
#include "Properties/MeshMaterialProperties.h"
#include "Drawing/UVLayoutPreview.h"

#include "UVLayoutTool.generated.h"


// predeclarations
struct FMeshDescription;
class USimpleDynamicMeshComponent;


/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UUVLayoutToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};



UENUM()
enum class EUVLayoutType
{
	Transform,
	Stack,
	Repack
};



/**
 * Standard properties
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UUVLayoutToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UUVLayoutToolProperties();

	/** Type of transformation to apply to input UV islands */
	UPROPERTY(EditAnywhere, Category = UVLayout)
	EUVLayoutType LayoutType = EUVLayoutType::Repack;

	/** Expected resolution of output textures; controls spacing left between charts */
	UPROPERTY(EditAnywhere, Category = UVLayout, meta = (UIMin = "64", UIMax = "2048", ClampMin = "2", ClampMax = "4096"))
	int TextureResolution = 1024;

	/** Apply this uniform scaling to the UVs after any layout recalculation */
	UPROPERTY(EditAnywhere, Category = UVLayout, meta = (UIMin = "0.1", UIMax = "5.0", ClampMin = "0.0001", ClampMax = "10000") )
	float UVScaleFactor = 1;

	/** Apply this 2D translation to the UVs after any layout recalculation, and after scaling */
	UPROPERTY(EditAnywhere, Category = UVLayout)
	FVector2D UVTranslate = FVector2D(0,0);

	/** Allow the packer to flip the orientation of UV islands if it save space. May cause problems for downstream operations, not recommended. */
	UPROPERTY(EditAnywhere, Category = UVLayout, AdvancedDisplay)
	bool bAllowFlips = false;

};






/**
 * Factory with enough info to spawn the background-thread Operator to do a chunk of work for the tool
 *  stores a pointer to the tool and enough info to know which specific operator it should spawn
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UUVLayoutOperatorFactory : public UObject, public IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:
	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<FDynamicMeshOperator> MakeNewOperator() override;

	UPROPERTY()
	UUVLayoutTool *Tool;

	int ComponentIndex;

};

/**
 * Simple Mesh Normal Updating Tool
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UUVLayoutTool : public UMultiSelectionTool
{
	GENERATED_BODY()

public:

	friend UUVLayoutOperatorFactory;

	UUVLayoutTool();

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void SetWorld(UWorld* World, UInteractiveGizmoManager* GizmoManagerIn);
	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPI);

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override;

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

protected:

	UPROPERTY()
	UUVLayoutToolProperties* BasicProperties;

	UPROPERTY()
	UExistingMeshMaterialProperties* MaterialSettings = nullptr;

	UPROPERTY()
	TArray<UMeshOpPreviewWithBackgroundCompute*> Previews;

protected:
	TArray<TSharedPtr<FDynamicMesh3>> OriginalDynamicMeshes;

	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	FViewCameraState CameraState;

	void UpdateNumPreviews();

	void UpdateVisualization();

	void OnPreviewMeshUpdated(UMeshOpPreviewWithBackgroundCompute* Compute);

	void GenerateAsset(const TArray<FDynamicMeshOpResult>& Results);


protected:
	UPROPERTY()
	UUVLayoutPreview* UVLayoutView = nullptr;
};
