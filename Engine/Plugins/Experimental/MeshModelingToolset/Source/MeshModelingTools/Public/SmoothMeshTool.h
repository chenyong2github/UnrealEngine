// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "SingleSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "DynamicMesh3.h"
#include "MeshOpPreviewHelpers.h"
#include "SmoothMeshTool.generated.h"


// predeclarations
struct FMeshDescription;
class USimpleDynamicMeshComponent;


UENUM()
enum class ESmoothMeshToolSmoothType : uint8
{
	/** Fast smooth with N iterations */
	Iterative UMETA(DisplayName = "Fast Smoothing"),

	/** Implicit smoothing, usually better with UV preservation */
	BiHarmonic_Cotan UMETA(DisplayName = "Implicit Smoothing"),

};


/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API USmoothMeshToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:

	IToolsContextAssetAPI* AssetAPI;

	USmoothMeshToolBuilder()
	{
		AssetAPI = nullptr;
	}

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};





/**
 * Simple Mesh Smoothing Tool
 */
UCLASS()
class MESHMODELINGTOOLS_API USmoothMeshTool : public USingleSelectionTool, public IDynamicMeshOperatorFactory
{
	GENERATED_BODY()

public:
	USmoothMeshTool() = default;

	virtual void SetWorld(UWorld* World);
	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn);

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void Tick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override;
	virtual bool CanAccept() const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

	virtual TSharedPtr<FDynamicMeshOperator> MakeNewOperator() override;


	
protected:
	// need to update bResultValid if these are modified, so we don't publicly expose them. 
	// @todo setters/getters for these

	/** primary brush mode */
	UPROPERTY(EditAnywhere, Category = Options)
	ESmoothMeshToolSmoothType SmoothType = ESmoothMeshToolSmoothType::Iterative;

	/** Smoothing speed */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float SmoothSpeed = 0.1f;

	/** Smoothing speed */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "0", UIMax = "100", ClampMin = "0", ClampMax = "1000"))
	int32 SmoothIterations = 1;


protected:
	void UpdateResult();

	bool bResultValid = false;

	UPROPERTY()
	UMeshOpPreviewWithBackgroundCompute* Preview = nullptr;
	FDynamicMesh3 SrcDynamicMesh;


	UWorld* TargetWorld = nullptr;
	IToolsContextAssetAPI* AssetAPI = nullptr;
};
