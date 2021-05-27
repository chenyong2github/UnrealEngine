// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "SceneTypes.h"
#include "RenderResource.h"
#include "ShaderParameters.h"
#include "UniformBuffer.h"
#include "InstanceUniformShaderParameters.h"
#include "LightmapUniformShaderParameters.h"
#include "UnifiedBuffer.h"
#include "Containers/StaticArray.h"

/** 
 * The uniform shader parameters associated with a primitive. 
 * Note: Must match FPrimitiveSceneData in shaders.
 * Note 2: Try to keep this 16 byte aligned. i.e |Matrix4x4|Vector3,float|Vector3,float|Vector4|  _NOT_  |Vector3,(waste padding)|Vector3,(waste padding)|Vector3. Or at least mark out padding if it can't be avoided.
 */
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FPrimitiveUniformShaderParameters,ENGINE_API)
	SHADER_PARAMETER(uint32,		Flags)
	SHADER_PARAMETER(uint32,		InstanceDataOffset)
	SHADER_PARAMETER(uint32,		NumInstanceDataEntries)
	SHADER_PARAMETER(int32,			SingleCaptureIndex)										// Should default to 0 if no reflection captures are provided, as there will be a default black (0,0,0,0) cubemap in that slot
	SHADER_PARAMETER(FMatrix44f,	LocalToWorld)											// Always needed
	SHADER_PARAMETER(FMatrix44f,	WorldToLocal)											// Rarely needed
	SHADER_PARAMETER(FMatrix44f,	PreviousLocalToWorld)									// Used to calculate velocity
	SHADER_PARAMETER(FMatrix44f,	PreviousWorldToLocal)									// Rarely used when calculating velocity, if material uses vertex offset along with world->local transform
	SHADER_PARAMETER_EX(FVector3f,	InvNonUniformScale,  EShaderPrecisionModifier::Half)	// Often needed
	SHADER_PARAMETER(float,			ObjectBoundsX)											// Only needed for editor/development
	SHADER_PARAMETER(FVector4,		ObjectWorldPositionAndRadius)							// Needed by some materials
	SHADER_PARAMETER(FVector3f,		ActorWorldPosition)
	SHADER_PARAMETER(uint32,		LightmapUVIndex)										// Only needed if static lighting is enabled
	SHADER_PARAMETER_EX(FVector3f,	ObjectOrientation,   EShaderPrecisionModifier::Half)
	SHADER_PARAMETER(uint32,		LightmapDataIndex)										// Only needed if static lighting is enabled
	SHADER_PARAMETER_EX(FVector4,	NonUniformScale,     EShaderPrecisionModifier::Half)
	SHADER_PARAMETER(FVector3f,		PreSkinnedLocalBoundsMin)								// Local space min bounds, pre-skinning
	SHADER_PARAMETER(uint32,		NaniteResourceID)
	SHADER_PARAMETER(FVector3f,		PreSkinnedLocalBoundsMax)								// Local space bounds, pre-skinning
	SHADER_PARAMETER(uint32,		NaniteHierarchyOffset)
	SHADER_PARAMETER(FVector3f,		LocalObjectBoundsMin)									// This is used in a custom material function (ObjectLocalBounds.uasset)
	SHADER_PARAMETER(float,			ObjectBoundsY)											// Only needed for editor/development
	SHADER_PARAMETER(FVector3f,		LocalObjectBoundsMax)									// This is used in a custom material function (ObjectLocalBounds.uasset)
	SHADER_PARAMETER(float,			ObjectBoundsZ)											// Only needed for editor/development
	SHADER_PARAMETER_ARRAY(FVector4, CustomPrimitiveData, [FCustomPrimitiveData::NumCustomPrimitiveDataFloat4s]) // Custom data per primitive that can be accessed through material expression parameters and modified through UStaticMeshComponent
END_GLOBAL_SHADER_PARAMETER_STRUCT()

// Must match SceneData.ush
#define PRIMITIVE_SCENE_DATA_FLAG_CAST_SHADOWS						0x1
#define PRIMITIVE_SCENE_DATA_FLAG_USE_SINGLE_SAMPLE_SHADOW_SL		0x2
#define PRIMITIVE_SCENE_DATA_FLAG_USE_VOLUMETRIC_LM_SHADOW_SL		0x4
#define PRIMITIVE_SCENE_DATA_FLAG_DECAL_RECEIVER					0x8
#define PRIMITIVE_SCENE_DATA_FLAG_DRAWS_VELOCITY					0x10
#define PRIMITIVE_SCENE_DATA_FLAG_OUTPUT_VELOCITY					0x20
#define PRIMITIVE_SCENE_DATA_FLAG_DETERMINANT_SIGN					0x40
#define PRIMITIVE_SCENE_DATA_FLAG_HAS_CAPSULE_REPRESENTATION		0x80
#define PRIMITIVE_SCENE_DATA_FLAG_HAS_CAST_CONTACT_SHADOW			0x100
#define PRIMITIVE_SCENE_DATA_FLAG_HAS_PRIMITIVE_CUSTOM_DATA			0x200
#define PRIMITIVE_SCENE_DATA_FLAG_LIGHTING_CHANNEL_0				0x400
#define PRIMITIVE_SCENE_DATA_FLAG_LIGHTING_CHANNEL_1				0x800
#define PRIMITIVE_SCENE_DATA_FLAG_LIGHTING_CHANNEL_2				0x1000

struct FPrimitiveUniformShaderParametersBuilder
{
public:
	inline FPrimitiveUniformShaderParametersBuilder& Defaults()
	{
		// Flags defaulted on
		bCastShadow									= true;
		bCastContactShadow							= true;

		// Flags defaulted off
		bReceivesDecals								= false;
		bUseSingleSampleShadowFromStationaryLights	= false;
		bUseVolumetricLightmap						= false;
		bDrawsVelocity								= false;
		bOutputVelocity								= false;
		bHasCapsuleRepresentation					= false;
		bHasPreSkinnedLocalBounds					= false;
		bHasPreviousLocalToWorld					= false;

		// Invalid indices
		Parameters.LightmapDataIndex				= INDEX_NONE;
		Parameters.LightmapUVIndex					= INDEX_NONE;
		Parameters.SingleCaptureIndex				= INDEX_NONE;

		// Instance culling
		Parameters.InstanceDataOffset				= INDEX_NONE;
		Parameters.NumInstanceDataEntries			= 0;

		LightingChannels = GetDefaultLightingChannelMask();

		return CustomPrimitiveData(nullptr);
	}

#define PRIMITIVE_UNIFORM_BUILDER_METHOD(INPUT_TYPE, VARIABLE_NAME) \
	inline FPrimitiveUniformShaderParametersBuilder& VARIABLE_NAME(INPUT_TYPE In##VARIABLE_NAME) { Parameters.VARIABLE_NAME = In##VARIABLE_NAME; return *this; }

#define PRIMITIVE_UNIFORM_BUILDER_FLAG_METHOD(INPUT_TYPE, VARIABLE_NAME) \
	inline FPrimitiveUniformShaderParametersBuilder& VARIABLE_NAME(INPUT_TYPE In##VARIABLE_NAME) { b##VARIABLE_NAME = In##VARIABLE_NAME; return *this; }

	PRIMITIVE_UNIFORM_BUILDER_FLAG_METHOD(bool,			ReceivesDecals);
	PRIMITIVE_UNIFORM_BUILDER_FLAG_METHOD(bool,			HasCapsuleRepresentation);
	PRIMITIVE_UNIFORM_BUILDER_FLAG_METHOD(bool,			CastContactShadow);
	PRIMITIVE_UNIFORM_BUILDER_FLAG_METHOD(bool,			UseSingleSampleShadowFromStationaryLights);
	PRIMITIVE_UNIFORM_BUILDER_FLAG_METHOD(bool,			UseVolumetricLightmap);
	PRIMITIVE_UNIFORM_BUILDER_FLAG_METHOD(bool,			DrawsVelocity);
	PRIMITIVE_UNIFORM_BUILDER_FLAG_METHOD(bool,			OutputVelocity);
	PRIMITIVE_UNIFORM_BUILDER_FLAG_METHOD(bool,			CastShadow);

	PRIMITIVE_UNIFORM_BUILDER_METHOD(uint32,			InstanceDataOffset);
	PRIMITIVE_UNIFORM_BUILDER_METHOD(uint32,			NumInstanceDataEntries);
	PRIMITIVE_UNIFORM_BUILDER_METHOD(int32,				SingleCaptureIndex);
	PRIMITIVE_UNIFORM_BUILDER_METHOD(uint32,			NaniteResourceID);
	PRIMITIVE_UNIFORM_BUILDER_METHOD(uint32,			NaniteHierarchyOffset);
	PRIMITIVE_UNIFORM_BUILDER_METHOD(uint32,			LightmapUVIndex);
	PRIMITIVE_UNIFORM_BUILDER_METHOD(uint32,			LightmapDataIndex);

	PRIMITIVE_UNIFORM_BUILDER_METHOD(const FVector3f&,	ActorWorldPosition);
	PRIMITIVE_UNIFORM_BUILDER_METHOD(const FMatrix44f&,	LocalToWorld);
	PRIMITIVE_UNIFORM_BUILDER_METHOD(const FMatrix44f&,	PreviousLocalToWorld);

#undef PRIMITIVE_UNIFORM_BUILDER_FLAG_METHOD
#undef PRIMITIVE_UNIFORM_BUILDER_METHOD

	inline FPrimitiveUniformShaderParametersBuilder& LightingChannelMask(uint32 InLightingChannelMask)
	{
		LightingChannels = InLightingChannelMask;
		return *this;
	}

	inline FPrimitiveUniformShaderParametersBuilder& LightingChannelMask(const FVector3f& InObjectBounds)
	{
		Parameters.ObjectBoundsX = InObjectBounds.X;
		Parameters.ObjectBoundsY = InObjectBounds.Y;
		Parameters.ObjectBoundsZ = InObjectBounds.Z;
		return *this;
	}

	inline FPrimitiveUniformShaderParametersBuilder& WorldBounds(const FBoxSphereBounds& InWorldBounds)
	{
		Parameters.ObjectWorldPositionAndRadius = FVector4(InWorldBounds.Origin, InWorldBounds.SphereRadius);
		Parameters.ObjectBoundsX = InWorldBounds.BoxExtent.X;
		Parameters.ObjectBoundsY = InWorldBounds.BoxExtent.Y;
		Parameters.ObjectBoundsZ = InWorldBounds.BoxExtent.Z;
		return *this;
	}

	inline FPrimitiveUniformShaderParametersBuilder& LocalBounds(const FBoxSphereBounds& InLocalBounds)
	{
		Parameters.LocalObjectBoundsMin = InLocalBounds.GetBoxExtrema(0); // 0 == minimum
		Parameters.LocalObjectBoundsMax = InLocalBounds.GetBoxExtrema(1); // 1 == maximum
		return *this;
	}

	inline FPrimitiveUniformShaderParametersBuilder& PreSkinnedLocalBounds(const FBoxSphereBounds& InPreSkinnedLocalBounds)
	{
		bHasPreSkinnedLocalBounds = true;
		Parameters.PreSkinnedLocalBoundsMin = InPreSkinnedLocalBounds.GetBoxExtrema(0); // 0 == minimum
		Parameters.PreSkinnedLocalBoundsMax = InPreSkinnedLocalBounds.GetBoxExtrema(1); // 1 == maximum
		return *this;
	}

	inline FPrimitiveUniformShaderParametersBuilder& CustomPrimitiveData(const FCustomPrimitiveData* InCustomPrimitiveData)
	{
		// If this primitive has custom primitive data, set it
		if (InCustomPrimitiveData)
		{
			// Copy at most up to the max supported number of dwords for safety
			FMemory::Memcpy(
				&Parameters.CustomPrimitiveData,
				InCustomPrimitiveData->Data.GetData(),
				InCustomPrimitiveData->Data.GetTypeSize() * FMath::Min(InCustomPrimitiveData->Data.Num(),
				FCustomPrimitiveData::NumCustomPrimitiveDataFloats)
			);

			bHasCustomData = true;
		}
		else
		{
			// Clear to 0
			FMemory::Memzero(Parameters.CustomPrimitiveData);
			bHasCustomData = false;
		}

		return *this;
	}

	inline const FPrimitiveUniformShaderParameters& Build()
	{
		Parameters.Flags = 0;
		Parameters.Flags |= bReceivesDecals ? PRIMITIVE_SCENE_DATA_FLAG_DECAL_RECEIVER : 0u;
		Parameters.Flags |= bHasCapsuleRepresentation ? PRIMITIVE_SCENE_DATA_FLAG_HAS_CAPSULE_REPRESENTATION : 0u;
		Parameters.Flags |= bCastContactShadow ? PRIMITIVE_SCENE_DATA_FLAG_HAS_CAST_CONTACT_SHADOW : 0u;
		Parameters.Flags |= bUseSingleSampleShadowFromStationaryLights ? PRIMITIVE_SCENE_DATA_FLAG_USE_SINGLE_SAMPLE_SHADOW_SL : 0u;
		Parameters.Flags |= (bUseVolumetricLightmap && bUseSingleSampleShadowFromStationaryLights) ? PRIMITIVE_SCENE_DATA_FLAG_USE_VOLUMETRIC_LM_SHADOW_SL : 0u;
		Parameters.Flags |= bDrawsVelocity ? PRIMITIVE_SCENE_DATA_FLAG_DRAWS_VELOCITY : 0u;
		Parameters.Flags |= bOutputVelocity ? PRIMITIVE_SCENE_DATA_FLAG_OUTPUT_VELOCITY : 0u;
		Parameters.Flags |= (Parameters.LocalToWorld.RotDeterminant() < 0.0f) ? PRIMITIVE_SCENE_DATA_FLAG_DETERMINANT_SIGN : 0u;
		Parameters.Flags |= bCastShadow ? PRIMITIVE_SCENE_DATA_FLAG_CAST_SHADOWS : 0u;
		Parameters.Flags |= bHasCustomData ? PRIMITIVE_SCENE_DATA_FLAG_HAS_PRIMITIVE_CUSTOM_DATA : 0u;
		Parameters.Flags |= ((LightingChannels & 0x1) != 0) ? PRIMITIVE_SCENE_DATA_FLAG_LIGHTING_CHANNEL_0 : 0u;
		Parameters.Flags |= ((LightingChannels & 0x2) != 0) ? PRIMITIVE_SCENE_DATA_FLAG_LIGHTING_CHANNEL_1 : 0u;
		Parameters.Flags |= ((LightingChannels & 0x4) != 0) ? PRIMITIVE_SCENE_DATA_FLAG_LIGHTING_CHANNEL_2 : 0u;

		Parameters.WorldToLocal = Parameters.LocalToWorld.Inverse();

		if (bHasPreviousLocalToWorld)
		{
			Parameters.PreviousWorldToLocal = Parameters.PreviousLocalToWorld.Inverse();
		}
		else
		{
			Parameters.PreviousLocalToWorld = Parameters.LocalToWorld;
			Parameters.PreviousWorldToLocal = Parameters.WorldToLocal;
		}

		if (!bHasPreSkinnedLocalBounds)
		{
			Parameters.PreSkinnedLocalBoundsMin = Parameters.LocalObjectBoundsMin;
			Parameters.PreSkinnedLocalBoundsMax = Parameters.LocalObjectBoundsMax;
		}

		Parameters.ObjectOrientation = Parameters.LocalToWorld.GetUnitAxis(EAxis::Z);

		{
			// Extract per axis scales from LocalToWorld transform
			FVector4 WorldX = FVector4(Parameters.LocalToWorld.M[0][0], Parameters.LocalToWorld.M[0][1], Parameters.LocalToWorld.M[0][2], 0);
			FVector4 WorldY = FVector4(Parameters.LocalToWorld.M[1][0], Parameters.LocalToWorld.M[1][1], Parameters.LocalToWorld.M[1][2], 0);
			FVector4 WorldZ = FVector4(Parameters.LocalToWorld.M[2][0], Parameters.LocalToWorld.M[2][1], Parameters.LocalToWorld.M[2][2], 0);
			float ScaleX = FVector(WorldX).Size();
			float ScaleY = FVector(WorldY).Size();
			float ScaleZ = FVector(WorldZ).Size();
			Parameters.NonUniformScale = FVector4(ScaleX, ScaleY, ScaleZ, FMath::Max3(FMath::Abs(ScaleX), FMath::Abs(ScaleY), FMath::Abs(ScaleZ)));
			Parameters.InvNonUniformScale = FVector3f(
				ScaleX > KINDA_SMALL_NUMBER ? 1.0f / ScaleX : 0.0f,
				ScaleY > KINDA_SMALL_NUMBER ? 1.0f / ScaleY : 0.0f,
				ScaleZ > KINDA_SMALL_NUMBER ? 1.0f / ScaleZ : 0.0f);
		}

		// If SingleCaptureIndex is invalid, set it to 0 since there will be a default cubemap at that slot
		Parameters.SingleCaptureIndex = FMath::Max(Parameters.SingleCaptureIndex, 0);

		return Parameters;
	}

private:
	FPrimitiveUniformShaderParameters Parameters;
	uint32 LightingChannels : 1;
	uint32 bReceivesDecals : 1;
	uint32 bUseSingleSampleShadowFromStationaryLights : 1;
	uint32 bUseVolumetricLightmap : 1;
	uint32 bDrawsVelocity : 1;
	uint32 bOutputVelocity : 1;
	uint32 bCastShadow : 1;
	uint32 bCastContactShadow : 1;
	uint32 bHasCapsuleRepresentation : 1;
	uint32 bHasPreSkinnedLocalBounds : 1;
	uint32 bHasCustomData : 1;
	uint32 bHasPreviousLocalToWorld : 1;
};

inline TUniformBufferRef<FPrimitiveUniformShaderParameters> CreatePrimitiveUniformBufferImmediate(
	const FMatrix44f& LocalToWorld,
	const FBoxSphereBounds& WorldBounds,
	const FBoxSphereBounds& LocalBounds,
	const FBoxSphereBounds& PreSkinnedLocalBounds,
	bool bReceivesDecals,
	bool bDrawsVelocity
)
{
	check(IsInRenderingThread());
	return TUniformBufferRef<FPrimitiveUniformShaderParameters>::CreateUniformBufferImmediate(
		FPrimitiveUniformShaderParametersBuilder{}
		.Defaults()
			.LocalToWorld(LocalToWorld)
			.ActorWorldPosition(WorldBounds.Origin)
			.WorldBounds(WorldBounds)
			.LocalBounds(LocalBounds)
			.PreSkinnedLocalBounds(PreSkinnedLocalBounds)
			.ReceivesDecals(bReceivesDecals)
			.DrawsVelocity(bDrawsVelocity)
		.Build(),
		UniformBuffer_MultiFrame
	);
}

inline FPrimitiveUniformShaderParameters GetIdentityPrimitiveParameters()
{
	// Don't use FMatrix44f::Identity here as GetIdentityPrimitiveParameters is used by TGlobalResource<FIdentityPrimitiveUniformBuffer> and because
	// static initialization order is undefined, FMatrix44f::Identity might be all 0's or random data the first time this is called.
	return FPrimitiveUniformShaderParametersBuilder{}
		.Defaults()
			.LocalToWorld(FMatrix44f(FPlane4f(1, 0, 0, 0), FPlane4f(0, 1, 0, 0), FPlane4f(0, 0, 1, 0), FPlane4f(0, 0, 0, 1)))
			.ActorWorldPosition(FVector3f(0.0f, 0.0f, 0.0f))
			.WorldBounds(FBoxSphereBounds(EForceInit::ForceInit))
			.LocalBounds(FBoxSphereBounds(EForceInit::ForceInit))
		.Build();
}

/**
 * Primitive uniform buffer containing only identity transforms.
 */
class FIdentityPrimitiveUniformBuffer : public TUniformBuffer<FPrimitiveUniformShaderParameters>
{
public:

	/** Default constructor. */
	FIdentityPrimitiveUniformBuffer()
	{
		SetContents(GetIdentityPrimitiveParameters());
	}
};

/** Global primitive uniform buffer resource containing identity transformations. */
extern ENGINE_API TGlobalResource<FIdentityPrimitiveUniformBuffer> GIdentityPrimitiveUniformBuffer;

struct FPrimitiveSceneShaderData
{
	// Must match usf
	enum { PrimitiveDataStrideInFloat4s = 35 };

	TStaticArray<FVector4, PrimitiveDataStrideInFloat4s> Data;

	FPrimitiveSceneShaderData()
		: Data(InPlace, NoInit)
	{
		static_assert(FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s == FScatterUploadBuffer::PrimitiveDataStrideInFloat4s,"");
		Setup(GetIdentityPrimitiveParameters());
	}

	explicit FPrimitiveSceneShaderData(const FPrimitiveUniformShaderParameters& PrimitiveUniformShaderParameters)
		: Data(InPlace, NoInit)
	{
		Setup(PrimitiveUniformShaderParameters);
	}

	ENGINE_API FPrimitiveSceneShaderData(const class FPrimitiveSceneProxy* RESTRICT Proxy);

	ENGINE_API void Setup(const FPrimitiveUniformShaderParameters& PrimitiveUniformShaderParameters);
};

class FSinglePrimitiveStructured : public FRenderResource
{
public:

	FSinglePrimitiveStructured()
		: ShaderPlatform(SP_NumPlatforms)
	{}

	ENGINE_API virtual void InitRHI() override;

	virtual void ReleaseRHI() override
	{
		PrimitiveSceneDataBufferRHI.SafeRelease();
		PrimitiveSceneDataBufferSRV.SafeRelease();
		SkyIrradianceEnvironmentMapRHI.SafeRelease();
		SkyIrradianceEnvironmentMapSRV.SafeRelease();
		InstanceSceneDataBufferRHI.SafeRelease();
		InstanceSceneDataBufferSRV.SafeRelease();
		PrimitiveSceneDataTextureRHI.SafeRelease();
		PrimitiveSceneDataTextureSRV.SafeRelease();
		LightmapSceneDataBufferRHI.SafeRelease();
		LightmapSceneDataBufferSRV.SafeRelease();
//#if WITH_EDITOR
		EditorVisualizeLevelInstanceDataBufferRHI.SafeRelease();
		EditorVisualizeLevelInstanceDataBufferSRV.SafeRelease();

		EditorSelectedDataBufferRHI.SafeRelease();
		EditorSelectedDataBufferSRV.SafeRelease();
//#endif
	}

	ENGINE_API void UploadToGPU();

	EShaderPlatform ShaderPlatform=SP_NumPlatforms;

	FPrimitiveSceneShaderData PrimitiveSceneData;
	FInstanceSceneShaderData InstanceSceneData;
	FLightmapSceneShaderData LightmapSceneData;

	FBufferRHIRef PrimitiveSceneDataBufferRHI;
	FShaderResourceViewRHIRef PrimitiveSceneDataBufferSRV;

	FBufferRHIRef SkyIrradianceEnvironmentMapRHI;
	FShaderResourceViewRHIRef SkyIrradianceEnvironmentMapSRV;

	FBufferRHIRef InstanceSceneDataBufferRHI;
	FShaderResourceViewRHIRef InstanceSceneDataBufferSRV;

	FTexture2DRHIRef PrimitiveSceneDataTextureRHI;
	FShaderResourceViewRHIRef PrimitiveSceneDataTextureSRV;

	FBufferRHIRef LightmapSceneDataBufferRHI;
	FShaderResourceViewRHIRef LightmapSceneDataBufferSRV;

//#if WITH_EDITOR
	FBufferRHIRef EditorVisualizeLevelInstanceDataBufferRHI;
	FShaderResourceViewRHIRef EditorVisualizeLevelInstanceDataBufferSRV;

	FBufferRHIRef EditorSelectedDataBufferRHI;
	FShaderResourceViewRHIRef EditorSelectedDataBufferSRV;
//#endif
};

/**
* Default Primitive data buffer.  
* This is used when the VF is used for rendering outside normal mesh passes, where there is no valid scene.
*/
extern ENGINE_API TGlobalResource<FSinglePrimitiveStructured> GIdentityPrimitiveBuffer;
extern ENGINE_API TGlobalResource<FSinglePrimitiveStructured> GTilePrimitiveBuffer;
