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





UENUM()
enum class ESmoothMeshToolSmoothType : uint8
{
	/** Iterative smoothing with N iterations */
	Iterative UMETA(DisplayName = "Fast Iterative"),

	/** Implicit smoothing, produces smoother output and does a better job at preserving UVs, but can be very slow on large meshes */
	Implicit UMETA(DisplayName = "Implicit Single"),

	/** Iterative implicit-diffusion smoothing with N iterations */
	Diffusion UMETA(DisplayName = "Implicit Diffusion")
};



/** PropertySet for properties affecting the Smoother. */
UCLASS()
class MESHMODELINGTOOLS_API USmoothMeshToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Type of smoothing to apply */
	UPROPERTY(EditAnywhere, Category = Options)
	ESmoothMeshToolSmoothType SmoothingType = ESmoothMeshToolSmoothType::Iterative;
};



/** Properties for Iterative Smoothing */
UCLASS()
class MESHMODELINGTOOLS_API UIterativeSmoothProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** Amount of smoothing allowed per step. Smaller steps will avoid things like collapse of small/thin features. */
	UPROPERTY(EditAnywhere, Category = IterativeSmoothing, meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float SmoothingPerStep = 1.0f;

	/** Number of Smoothing iterations */
	UPROPERTY(EditAnywhere, Category = IterativeSmoothing, meta = (UIMin = "0", UIMax = "100", ClampMin = "0", ClampMax = "1000"))
	int32 Steps = 1;

	/** If this is false, the smoother will try to reshape the triangles to be more regular, which will distort UVs */
	UPROPERTY(EditAnywhere, Category = IterativeSmoothing)
	bool bSmoothBoundary = true;
};



/** Properties for Diffusion Smoothing */
UCLASS()
class MESHMODELINGTOOLS_API UDiffusionSmoothProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Amount of smoothing allowed per step. Smaller steps will avoid things like collapse of small/thin features. */
	UPROPERTY(EditAnywhere, Category = DiffusionSmoothing, meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float SmoothingPerStep = 1.0f;

	/** Number of Smoothing iterations */
	UPROPERTY(EditAnywhere, Category = DiffusionSmoothing, meta = (UIMin = "0", UIMax = "100", ClampMin = "0", ClampMax = "1000"))
	int32 Steps = 1;

	/** If this is false, the smoother will try to reshape the triangles to be more regular, which will distort UVs */
	UPROPERTY(EditAnywhere, Category = DiffusionSmoothing)
	bool bPreserveUVs = true;
};




/** Properties for Implicit smoothing */
UCLASS()
class MESHMODELINGTOOLS_API UImplicitSmoothProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Smoothing speed */
	//UPROPERTY(EditAnywhere, Category = ImplicitSmoothing, meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	UPROPERTY()
	float SmoothSpeed = 0.1f;

	/** Desired Smoothness. This is not a linear quantity, but larger numbers produce smoother results */
	UPROPERTY(EditAnywhere, Category = ImplicitSmoothing, meta = (UIMin = "0.0", UIMax = "100.0", ClampMin = "0.0", ClampMax = "10000.0"))
	float Smoothness = 1.0f;

	/** If this is false, the smoother will try to reshape the triangles to be more regular, which will distort UVs */
	UPROPERTY(EditAnywhere, Category = ImplicitSmoothing)
	bool bPreserveUVs = true;
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

	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override;
	virtual bool CanAccept() const override;

	virtual TUniquePtr<FDynamicMeshOperator> MakeNewOperator() override;
	
protected:
	UPROPERTY()
	USmoothMeshToolProperties* SmoothProperties = nullptr;

	UPROPERTY()
	UIterativeSmoothProperties* IterativeProperties = nullptr;

	UPROPERTY()
	UDiffusionSmoothProperties* DiffusionProperties = nullptr;

	UPROPERTY()
	UImplicitSmoothProperties* ImplicitProperties = nullptr;

	void UpdateVisiblePropertySets();

protected:
	void UpdateResult();

	bool bResultValid = false;
	void InvalidateResult();

	UPROPERTY()
	UMeshOpPreviewWithBackgroundCompute* Preview = nullptr;
	FDynamicMesh3 SrcDynamicMesh;

	UWorld* TargetWorld = nullptr;
	IToolsContextAssetAPI* AssetAPI = nullptr;
};
