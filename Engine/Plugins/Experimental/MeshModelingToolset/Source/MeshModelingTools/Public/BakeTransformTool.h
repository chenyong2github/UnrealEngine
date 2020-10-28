// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "MeshOpPreviewHelpers.h"
#include "DynamicMesh3.h"
#include "BaseTools/SingleClickTool.h"
#include "BakeTransformTool.generated.h"


// predeclarations
struct FMeshDescription;
class USimpleDynamicMeshComponent;





/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UBakeTransformToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};


UENUM()
enum class EBakeScaleMethod : uint8
{
	BakeFullScale,			// bake all scale information, so the component has scale of 1 on all axes
	BakeNonuniformScale,	// bake the non-uniform scale, so the component has a uniform scale
	DoNotBakeScale			// do not bake any scaling
};


/**
 * Standard properties
 */
UCLASS()
class MESHMODELINGTOOLS_API UBakeTransformToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UBakeTransformToolProperties();

	/** Bake rotation */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bBakeRotation = true;

	/** Bake scale */
	UPROPERTY(EditAnywhere, Category = Options)
	EBakeScaleMethod BakeScale = EBakeScaleMethod::BakeFullScale;

	/** Recenter pivot after baking transform */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bRecenterPivot = false;
};



/**
 * Simple tool to bake scene transform on meshes into the mesh assets
 */
UCLASS()
class MESHMODELINGTOOLS_API UBakeTransformTool : public UMultiSelectionTool
{
	GENERATED_BODY()

public:
	UBakeTransformTool();

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void SetWorld(UWorld* World);
	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPI);

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }

protected:

	UPROPERTY()
	UBakeTransformToolProperties* BasicProperties;

protected:
	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	TArray<int> MapToFirstOccurrences;

	void UpdateAssets();
};
