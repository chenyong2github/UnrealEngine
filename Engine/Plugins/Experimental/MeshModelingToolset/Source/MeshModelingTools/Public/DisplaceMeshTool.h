// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Engine/Classes/Engine/Texture2D.h"
#include "SingleSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "DynamicMesh3.h"
#include "MeshNormals.h"
#include "Spatial/SampledScalarField2.h"
#include "DisplaceMeshTool.generated.h"


// predeclarations
struct FMeshDescription;
class USimpleDynamicMeshComponent;


UENUM()
enum class EDisplaceMeshToolDisplaceType : uint8
{
	/** Displace with N iterations */
	Constant UMETA(DisplayName = "Constant"),

	/** Displace with N iterations */
	RandomNoise UMETA(DisplayName = "Random Noise"),

	/** Displace with N iterations */
	DisplacementMap UMETA(DisplayName = "Displacement Map"),

};



/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UDisplaceMeshToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};





/**
 * Simple Mesh Displacement Tool
 */
UCLASS()
class MESHMODELINGTOOLS_API UDisplaceMeshTool : public USingleSelectionTool
{
	GENERATED_BODY()

public:
	UDisplaceMeshTool();

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override;
	virtual bool CanAccept() const override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
protected:
	// need to update bResultValid if these are modified, so we don't publicly expose them. 
	// @todo setters/getters for these

	/** primary brush mode */
	UPROPERTY(EditAnywhere, Category = Options)
	EDisplaceMeshToolDisplaceType DisplacementType;

	/** Displacement intensity */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "-100.0", UIMax = "100.0", ClampMin = "-10000.0", ClampMax = "100000.0"))
	float DisplaceIntensity;

	/** Seed for randomization */
	UPROPERTY(EditAnywhere, Category = Options, meta = (EditCondition = "DisplacementType == EDisplaceMeshToolDisplaceType::RandomNoise"))
	int RandomSeed;

	/** Subdivision iterations for mesh */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "0", UIMax = "10", ClampMin = "0", ClampMax = "100"))
	int Subdivisions;

	/** Displacement map */
	UPROPERTY(EditAnywhere, Category = Options)
	UTexture2D* DisplacementMap;


protected:
	USimpleDynamicMeshComponent* DynamicMeshComponent;
	
	FDynamicMesh3 OriginalMesh;		// don't actually need whole mesh, just positions...

	FDynamicMesh3 SubdividedMesh;
	int CachedSubdivisionsCount = -1;
	void UpdateSubdividedMesh();

	FSampledScalarField2f DisplaceField;
	UTexture2D* CachedMapSource = nullptr;
	void UpdateMap(bool bForceUpdate = false);

	TArray<FVector3d> PositionBuffer;
	FMeshNormals NormalsBuffer;
	TArray<FVector3d> DisplacedBuffer;
	bool bResultValid;
	void UpdateResult();

	void ComputeDisplacement_Constant();
	void ComputeDisplacement_RandomNoise();

	void ComputeDisplacement_Map();


private:
	//TUniquePtr<

};
