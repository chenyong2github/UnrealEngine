// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Materials/MaterialInterface.h"
#include "GLTFMaterialAnalyzer.generated.h"

UCLASS()
class GLTFMATERIALANALYZER_API UGLTFMaterialAnalyzer : public UMaterialInterface
{
	GENERATED_BODY()

public:

	void AnalyzeMaterialProperty(const UMaterialInterface* InMaterial, EMaterialProperty InProperty);

	/** Whether the compiled material uses scene depth. */
	uint32 bUsesSceneDepth : 1;
	/** true if the material needs particle position. */
	uint32 bNeedsParticlePosition : 1;
	/** true if the material needs particle velocity. */
	uint32 bNeedsParticleVelocity : 1;
	/** true if the material needs particle relative time. */
	uint32 bNeedsParticleTime : 1;
	/** true if the material uses particle motion blur. */
	uint32 bUsesParticleMotionBlur : 1;
	/** true if the material needs particle random value. */
	uint32 bNeedsParticleRandom : 1;
	/** true if the material uses spherical particle opacity. */
	uint32 bUsesSphericalParticleOpacity : 1;
	/** true if the material uses particle sub uvs. */
	uint32 bUsesParticleSubUVs : 1;
	/** Boolean indicating using LightmapUvs */
	uint32 bUsesLightmapUVs : 1;
	/** Whether the material uses AO Material Mask */
	uint32 bUsesAOMaterialMask : 1;
	/** true if needs SpeedTree code */
	uint32 bUsesSpeedTree : 1;
	/** Boolean indicating the material uses worldspace position without shader offsets applied */
	uint32 bNeedsWorldPositionExcludingShaderOffsets : 1;
	/** true if the material needs particle size. */
	uint32 bNeedsParticleSize : 1;
	/** true if any scene texture expressions are reading from post process inputs */
	uint32 bNeedsSceneTexturePostProcessInputs : 1;
	/** true if any atmospheric fog expressions are used */
	uint32 bUsesAtmosphericFog : 1;
	/** true if any SkyAtmosphere expressions are used */
	uint32 bUsesSkyAtmosphere : 1;
	/** true if the material reads vertex color in the pixel shader. */
	uint32 bUsesVertexColor : 1;
	/** true if the material reads particle color in the pixel shader. */
	uint32 bUsesParticleColor : 1;
	/** true if the material reads mesh particle local to world in the pixel shader. */
	uint32 bUsesParticleLocalToWorld : 1;
	/** true if the material reads mesh particle world to local in the pixel shader. */
	uint32 bUsesParticleWorldToLocal : 1;

	/** true if the material uses any type of vertex position */
	uint32 bUsesVertexPosition : 1;

	uint32 bUsesTransformVector : 1;
	// True if the current property requires last frame's information
	uint32 bCompilingPreviousFrame : 1;
	/** True if material will output accurate velocities during base pass rendering. */
	uint32 bOutputsBasePassVelocities : 1;
	uint32 bUsesPixelDepthOffset : 1;
	uint32 bUsesWorldPositionOffset : 1;
	uint32 bUsesEmissiveColor : 1;
	uint32 bUsesDistanceCullFade : 1;
	/** true if the Roughness input evaluates to a constant 1.0 */
	uint32 bIsFullyRough : 1;
	/** true if allowed to generate code chunks. Translator operates in two phases; generate all code chunks & query meta data based on generated code chunks. */
	uint32 bAllowCodeChunkGeneration : 1;

	/** True if this material reads any per-instance custom data */
	uint32 bUsesPerInstanceCustomData : 1;

	/** Tracks the texture coordinates used by this material. */
	TBitArray<> AllocatedUserTexCoords;
	/** Tracks the texture coordinates used by the vertex shader in this material. */
	TBitArray<> AllocatedUserVertexTexCoords;

	uint32 DynamicParticleParameterMask;

	/** Will contain all the shading models picked up from the material expression graph */
	FMaterialShadingModelField ShadingModelsFromCompilation;

private:

	virtual FMaterialResource* GetMaterialResource(ERHIFeatureLevel::Type InFeatureLevel, EMaterialQualityLevel::Type QualityLevel) override;

	virtual int32 CompilePropertyEx(FMaterialCompiler* Compiler, const FGuid& AttributeID) override;

	virtual bool IsPropertyActive(EMaterialProperty InProperty) const override;

	// ReSharper disable once CppUE4ProbableMemoryIssuesWithUObject
	UMaterialInterface* Material;
};
