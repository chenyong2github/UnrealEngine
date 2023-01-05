// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Scene/SceneCapturePhotoSet.h"
#include "Sampling/MeshBakerCommon.h"
#include "Image/ImageInfilling.h"
#include "Baking/BakingTypes.h"
#include "DynamicMesh/MeshTangents.h"
#include "CoreMinimal.h"

class UTexture2D;
class AActor;

namespace UE
{
namespace Geometry
{

class FMeshMapBaker;






class MODELINGCOMPONENTS_API FRenderCaptureOcclusionHandler
{
public:
	FRenderCaptureOcclusionHandler(FImageDimensions Dimensions);

	void RegisterSample(const FVector2i& ImageCoords, bool bSampleValid);

	void PushInfillRequired(bool bInfillRequired);

	void ComputeAndApplyInfill(TArray<TUniquePtr<TImageBuilder<FVector4f>>>& Images);

private:

	struct FSampleStats {
		uint16 NumValid = 0;
		uint16 NumInvalid = 0;

		// These are required by the TMarchingPixelInfill implementation
		bool operator==(const FSampleStats& Other) const;
		bool operator!=(const FSampleStats& Other) const;
		FSampleStats& operator+=(const FSampleStats& Other);
		static FSampleStats Zero();
	};

	void ComputeInfill();

	void ApplyInfill(TImageBuilder<FVector4f>& Image) const;

	// Collect some sample stats per pixel, used to determine if a pixel requires infill or not
	TImageBuilder<FSampleStats> SampleStats;

	// InfillRequire[i] indicates if the i-th image passed to ComputeAndApplyInfill needs infill
	TArray<bool> InfillRequired;

	TMarchingPixelInfill<FSampleStats> Infill;
};





class MODELINGCOMPONENTS_API FSceneCapturePhotoSetSampler : public FMeshBakerDynamicMeshSampler
{
public:
	FSceneCapturePhotoSetSampler(
		FSceneCapturePhotoSet* SceneCapture,
		float ValidSampleDepthThreshold,
		const FDynamicMesh3* Mesh,
		const FDynamicMeshAABBTree3* Spatial,
		const FMeshTangentsd* Tangents); 

	virtual bool SupportsCustomCorrespondence() const override;

	// Warning: Expects that Sample.BaseSample.SurfacePoint and Sample.BaseNormal are set when the function is called
	virtual void* ComputeCustomCorrespondence(const FMeshUVSampleInfo& SampleInfo, FMeshMapEvaluator::FCorrespondenceSample& Sample) const override;

	virtual bool IsValidCorrespondence(const FMeshMapEvaluator::FCorrespondenceSample& Sample) const override;

private:
	FSceneCapturePhotoSet* SceneCapture = nullptr;
	float ValidSampleDepthThreshold = 0;
	TFunction<bool(const FVector3d&, const FVector3d&)> VisibilityFunction;
};






struct MODELINGCOMPONENTS_API FRenderCaptureOptions
{
	//
	// Material approximation settings
	//

	int32 RenderCaptureImageSize = 1024;
	bool bAntiAliasing = false;

	// render capture parameters
	double FieldOfViewDegrees = 45.0;
	double NearPlaneDist = 1.0;
	double ValidSampleDepthThreshold = 0;

	//
	// Material output settings
	//

	bool bBakeBaseColor = true;
	bool bBakeRoughness = true;
	bool bBakeMetallic = true;
	bool bBakeSpecular = true;
	bool bBakeEmissive = true;
	bool bBakeNormalMap = true;
	bool bBakeOpacity = true;
	bool bBakeSubsurfaceColor = true;
	bool bBakeDeviceDepth = true;
	
	bool bUsePackedMRS = true;

	//
	// Mesh settings
	//

	//  Which UV layer of the Target mesh (the one we're baking to) should be used
	int32 TargetUVLayer = 0;

	//
	// For internal use only
	//

	// A new MIC derived from this material will be created and assigned to the generated mesh
	// if null, will use /MeshModelingToolsetExp/Materials/FullMaterialBakePreviewMaterial_PackedMRS instead
	UMaterialInterface* BakeMaterial = nullptr;
};



MODELINGCOMPONENTS_API
TUniquePtr<FSceneCapturePhotoSet> CapturePhotoSet(
	const TArray<TObjectPtr<AActor>>& Actors,
	const FRenderCaptureOptions& Options,
	bool bAllowCancel);

MODELINGCOMPONENTS_API
void UpdatePhotoSet(
	TUniquePtr<FSceneCapturePhotoSet>& SceneCapture,
	const TArray<TObjectPtr<AActor>>& Actors,
	const FRenderCaptureOptions& Options,
	bool bAllowCancel);



// Return a render capture baker, note the lifetime of all arguments such match the lifetime of the returned baker
MODELINGCOMPONENTS_API
TUniquePtr<FMeshMapBaker> MakeRenderCaptureBaker(
	FDynamicMesh3* BaseMesh,
	TSharedPtr<FMeshTangentsd, ESPMode::ThreadSafe> BaseMeshTangents,
	FSceneCapturePhotoSet* SceneCapture,
	FSceneCapturePhotoSetSampler* Sampler,
	FRenderCaptureOptions Options,
	EBakeTextureResolution TextureImageSize,
	EBakeTextureSamplesPerPixel SamplesPerPixel,
	FRenderCaptureOcclusionHandler* OcclusionHandler);






struct MODELINGCOMPONENTS_API FRenderCaptureTextures
{
	UTexture2D* BaseColorMap = nullptr;
	UTexture2D* NormalMap = nullptr;
	UTexture2D* PackedMRSMap = nullptr;
	UTexture2D* MetallicMap = nullptr;
	UTexture2D* RoughnessMap = nullptr;
	UTexture2D* SpecularMap = nullptr;
	UTexture2D* EmissiveMap = nullptr;
	UTexture2D* OpacityMap = nullptr;
	UTexture2D* SubsurfaceColorMap = nullptr;
};

// Note: The source data in the textures is *not* updated by this function
MODELINGCOMPONENTS_API
void GetTexturesFromRenderCaptureBaker(
	const TUniquePtr<FMeshMapBaker>& Baker,
	FRenderCaptureTextures& TexturesOut);


} // end namespace Geometry
} // end namespace UE