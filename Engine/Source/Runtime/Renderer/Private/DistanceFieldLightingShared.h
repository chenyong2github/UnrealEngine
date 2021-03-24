// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DistanceFieldLightingShared.h
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RenderResource.h"
#include "ShaderParameters.h"
#include "RenderUtils.h"
#include "RHIStaticStates.h"
#include "DistanceFieldAtlas.h"
#include "Templates/UniquePtr.h"
#include "SceneRendering.h"
#include "ScenePrivate.h"

class FLightSceneProxy;
class FMaterialRenderProxy;
class FPrimitiveSceneInfo;
class FSceneRenderer;
class FShaderParameterMap;
class FViewInfo;
class FDistanceFieldSceneData;

DECLARE_LOG_CATEGORY_EXTERN(LogDistanceField, Log, All);

/** Tile sized used for most AO compute shaders. */
extern int32 GDistanceFieldAOTileSizeX;
extern int32 GDistanceFieldAOTileSizeY;
extern int32 GAverageObjectsPerShadowCullTile;
extern int32 GAverageHeightFieldObjectsPerShadowCullTile;

extern bool UseDistanceFieldAO();
extern bool UseAOObjectDistanceField();

enum EDistanceFieldPrimitiveType
{
	DFPT_SignedDistanceField,
	DFPT_HeightField,
	DFPT_Num
};

template <EDistanceFieldPrimitiveType PrimitiveType>
class TDistanceFieldObjectBuffers
{
public:

	static int32 ObjectDataStride;
	static int32 ObjectBoundsStride;

	FRWBufferStructured Bounds;
	FRWBufferStructured Data;

	TDistanceFieldObjectBuffers()
	{
	}

	void Initialize();

	void Release()
	{
		Bounds.Release();
		Data.Release();
	}

	size_t GetSizeBytes() const
	{
		return Bounds.NumBytes + Data.NumBytes;
	}
};

class FDistanceFieldObjectBuffers : public TDistanceFieldObjectBuffers<DFPT_SignedDistanceField> {};
class FHeightFieldObjectBuffers : public TDistanceFieldObjectBuffers<DFPT_HeightField> {};

BEGIN_SHADER_PARAMETER_STRUCT(FDistanceFieldObjectBufferParameters, )
	SHADER_PARAMETER_SRV(StructuredBuffer<float4>, SceneObjectBounds)
	SHADER_PARAMETER_SRV(StructuredBuffer<float4>, SceneObjectData)
	SHADER_PARAMETER(uint32, NumSceneObjects)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FDistanceFieldAtlasParameters, )
	SHADER_PARAMETER_SRV(StructuredBuffer<float4>, SceneDistanceFieldAssetData)
	SHADER_PARAMETER_SRV(StructuredBuffer<uint>, DistanceFieldIndirectionTable)
	SHADER_PARAMETER_TEXTURE(Texture3D, DistanceFieldBrickTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, DistanceFieldSampler)
	SHADER_PARAMETER(FVector, DistanceFieldBrickSize)
	SHADER_PARAMETER(FVector, DistanceFieldUniqueDataBrickSize)
	SHADER_PARAMETER(FIntVector, DistanceFieldBrickAtlasSizeInBricks)
	SHADER_PARAMETER(FIntVector, DistanceFieldBrickAtlasMask)
	SHADER_PARAMETER(FIntVector, DistanceFieldBrickAtlasSizeLog2)
	SHADER_PARAMETER(FVector, DistanceFieldBrickAtlasTexelSize)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FHeightFieldAtlasParameters, )
	SHADER_PARAMETER_TEXTURE(Texture2D, HeightFieldTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, HFVisibilityTexture)
	SHADER_PARAMETER(FVector2D, HeightFieldAtlasTexelSize)
END_SHADER_PARAMETER_STRUCT()

namespace DistanceField
{
	FDistanceFieldObjectBufferParameters SetupObjectBufferParameters(const FDistanceFieldSceneData& DistanceFieldSceneData);
	FDistanceFieldAtlasParameters SetupAtlasParameters(const FDistanceFieldSceneData& DistanceFieldSceneData);
};

template <EDistanceFieldPrimitiveType PrimitiveType>
class TDistanceFieldObjectBufferParameters
{
	DECLARE_INLINE_TYPE_LAYOUT(TDistanceFieldObjectBufferParameters, NonVirtual);
public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		SceneObjectBounds.Bind(ParameterMap, TEXT("SceneObjectBounds"));
		SceneObjectData.Bind(ParameterMap, TEXT("SceneObjectData"));
		NumSceneObjects.Bind(ParameterMap, TEXT("NumSceneObjects"));
	}

	template<typename TParamRef>
	void Set(
		FRHIComputeCommandList& RHICmdList,
		const TParamRef& ShaderRHI,
		const TDistanceFieldObjectBuffers<PrimitiveType>& ObjectBuffers,
		int32 NumObjectsValue,
		bool bBarrier = false)
	{
		if (bBarrier)
		{
			FRHITransitionInfo UAVTransitions[2];
			UAVTransitions[0] = FRHITransitionInfo(ObjectBuffers.Bounds.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute);
			UAVTransitions[1] = FRHITransitionInfo(ObjectBuffers.Data.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute);
			RHICmdList.Transition(MakeArrayView(UAVTransitions, UE_ARRAY_COUNT(UAVTransitions)));
		}

		SceneObjectBounds.SetBuffer(RHICmdList, ShaderRHI, ObjectBuffers.Bounds);
		SceneObjectData.SetBuffer(RHICmdList, ShaderRHI, ObjectBuffers.Data);
		SetShaderValue(RHICmdList, ShaderRHI, NumSceneObjects, NumObjectsValue);
	}

	template<typename TParamRef>
	void UnsetParameters(FRHICommandList& RHICmdList, const TParamRef& ShaderRHI, const TDistanceFieldObjectBuffers<PrimitiveType>& ObjectBuffers, bool bBarrier = false)
	{
		SceneObjectBounds.UnsetUAV(RHICmdList, ShaderRHI);
		SceneObjectData.UnsetUAV(RHICmdList, ShaderRHI);

		if (bBarrier)
		{
			FRHITransitionInfo SRVTransitions[2];
			SRVTransitions[0] = FRHITransitionInfo(ObjectBuffers.Bounds.UAV, ERHIAccess::Unknown, ERHIAccess::SRVMask);
			SRVTransitions[1] = FRHITransitionInfo(ObjectBuffers.Data.UAV, ERHIAccess::Unknown, ERHIAccess::SRVMask);
			RHICmdList.Transition(MakeArrayView(SRVTransitions, UE_ARRAY_COUNT(SRVTransitions)));
		}
	}

	friend FArchive& operator<<(FArchive& Ar, TDistanceFieldObjectBufferParameters& P)
	{
		Ar << P.SceneObjectBounds << P.SceneObjectData << P.NumSceneObjects;
		return Ar;
	}

	bool AnyBound() const
	{
		return SceneObjectBounds.IsBound() || SceneObjectData.IsBound() || NumSceneObjects.IsBound();
	}

private:
	
	LAYOUT_FIELD(FRWShaderParameter, SceneObjectBounds)
	LAYOUT_FIELD(FRWShaderParameter, SceneObjectData)
	LAYOUT_FIELD(FShaderParameter, NumSceneObjects)
};

template <EDistanceFieldPrimitiveType PrimitiveType>
class TDistanceFieldCulledObjectBuffers
{
public:

	static int32 ObjectDataStride;
	static int32 ObjectBoxBoundsStride;

	bool bWantBoxBounds;
	int32 MaxObjects;

	FRWBuffer ObjectIndirectArguments;
	FRWBuffer ObjectIndirectDispatch;
	FRWBufferStructured Bounds;
	FRWBufferStructured Data;
	FRWBufferStructured BoxBounds;

	TDistanceFieldCulledObjectBuffers()
	{
		MaxObjects = 0;
		bWantBoxBounds = false;
	}

	void Initialize()
	{
		if (MaxObjects > 0)
		{
			const TCHAR* ObjectIndirectArgumentsDebugName;
			const TCHAR* ObjectIndirectDispatchDebugName;
			const TCHAR* BoundsDebugName;
			const TCHAR* DataDebugName;
			const TCHAR* BoxBoundsDebugName;
			uint32 BoundsNumElements;

			if (PrimitiveType == DFPT_HeightField)
			{
				ObjectIndirectArgumentsDebugName = TEXT("FHeightFieldCulledObjectBuffers_ObjectIndirectArguments");
				ObjectIndirectDispatchDebugName = TEXT("FHeightFieldCulledObjectBuffers_ObjectIndirectDispatch");
				BoundsDebugName = TEXT("FHeightFieldCulledObjectBuffers_Bounds");
				DataDebugName = TEXT("FHeightFieldCulledObjectBuffers_Data");
				BoxBoundsDebugName = TEXT("FHeightFieldCulledObjectBuffers_BoxBounds");
				BoundsNumElements = MaxObjects * 2;
			}
			else
			{
				check(PrimitiveType == DFPT_SignedDistanceField);
				ObjectIndirectArgumentsDebugName = TEXT("FDistanceFieldCulledObjectBuffers_ObjectIndirectArguments");
				ObjectIndirectDispatchDebugName = TEXT("FDistanceFieldCulledObjectBuffers_ObjectIndirectDispatch");
				BoundsDebugName = TEXT("FDistanceFieldCulledObjectBuffers_Bounds");
				DataDebugName = TEXT("FDistanceFieldCulledObjectBuffers_Data");
				BoxBoundsDebugName = TEXT("FDistanceFieldCulledObjectBuffers_BoxBounds");
				BoundsNumElements = MaxObjects;
			}

			const uint32 FastVRamFlag = GFastVRamConfig.DistanceFieldCulledObjectBuffers | ( IsTransientResourceBufferAliasingEnabled() ? BUF_Transient : BUF_None );

			ObjectIndirectArguments.Initialize(ObjectIndirectArgumentsDebugName, sizeof(uint32), 5, PF_R32_UINT, BUF_Static | BUF_DrawIndirect);
			ObjectIndirectDispatch.Initialize(ObjectIndirectDispatchDebugName, sizeof(uint32), 3, PF_R32_UINT, BUF_Static | BUF_DrawIndirect);
			Bounds.Initialize(BoundsDebugName, sizeof(FVector4), BoundsNumElements, BUF_Static | FastVRamFlag);
			Data.Initialize(DataDebugName, sizeof(FVector4), MaxObjects * ObjectDataStride, BUF_Static | FastVRamFlag);

			if (bWantBoxBounds)
			{
				BoxBounds.Initialize(BoxBoundsDebugName, sizeof(FVector4), MaxObjects * ObjectBoxBoundsStride, BUF_Static | FastVRamFlag);
			}
		}
	}

	void AcquireTransientResource()
	{
		Bounds.AcquireTransientResource();
		Data.AcquireTransientResource();
		if (bWantBoxBounds)
		{
			BoxBounds.AcquireTransientResource();
		}
	}

	void DiscardTransientResource()
	{
		Bounds.DiscardTransientResource();
		Data.DiscardTransientResource();
		if (bWantBoxBounds)
		{
			BoxBounds.DiscardTransientResource();
		}
	}

	void Release()
	{
		ObjectIndirectArguments.Release();
		ObjectIndirectDispatch.Release();
		Bounds.Release();
		Data.Release();
		BoxBounds.Release();
	}

	size_t GetSizeBytes() const
	{
		return ObjectIndirectArguments.NumBytes + ObjectIndirectDispatch.NumBytes + Bounds.NumBytes + Data.NumBytes + BoxBounds.NumBytes;
	}
};

class FDistanceFieldCulledObjectBuffers : public TDistanceFieldCulledObjectBuffers<DFPT_SignedDistanceField> {};
class FHeightFieldCulledObjectBuffers : public TDistanceFieldCulledObjectBuffers<DFPT_HeightField> {};

template <EDistanceFieldPrimitiveType PrimitiveType>
class TDistanceFieldObjectBufferResource : public FRenderResource
{
public:
	typename TChooseClass<PrimitiveType == DFPT_HeightField, FHeightFieldCulledObjectBuffers, FDistanceFieldCulledObjectBuffers>::Result Buffers;

	virtual void InitDynamicRHI()  override
	{
		Buffers.Initialize();
	}

	virtual void ReleaseDynamicRHI() override
	{
		Buffers.Release();
	}
};

class FDistanceFieldObjectBufferResource : public TDistanceFieldObjectBufferResource<DFPT_SignedDistanceField> {};
class FHeightFieldObjectBufferResource : public TDistanceFieldObjectBufferResource<DFPT_HeightField> {};

BEGIN_SHADER_PARAMETER_STRUCT(FDistanceFieldCulledObjectBufferParameters, )
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWObjectIndirectArguments)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCulledObjectBounds)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCulledObjectData)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCulledObjectBoxBounds)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ObjectIndirectArguments)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CulledObjectBounds)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CulledObjectData)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CulledObjectBoxBounds)
END_SHADER_PARAMETER_STRUCT()

extern void AllocateDistanceFieldCulledObjectBuffers(
	FRDGBuilder& GraphBuilder, 
	bool bWantBoxBounds, 
	uint32 MaxObjects, 
	uint32 NumBoundsElementsScale,
	FRDGBufferRef& OutObjectIndirectArguments,
	FDistanceFieldCulledObjectBufferParameters& OutParameters);

template <EDistanceFieldPrimitiveType PrimitiveType>
class TDistanceFieldCulledObjectBufferParameters
{
	DECLARE_INLINE_TYPE_LAYOUT(TDistanceFieldCulledObjectBufferParameters, NonVirtual);
public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		ObjectIndirectArguments.Bind(ParameterMap, TEXT("ObjectIndirectArguments"));
		CulledObjectBounds.Bind(ParameterMap, TEXT("CulledObjectBounds"));
		CulledObjectData.Bind(ParameterMap, TEXT("CulledObjectData"));
		CulledObjectBoxBounds.Bind(ParameterMap, TEXT("CulledObjectBoxBounds"));
		HFVisibilityTexture.Bind(ParameterMap, TEXT("HFVisibilityTexture"));
		SceneDistanceFieldAssetData.Bind(ParameterMap, TEXT("SceneDistanceFieldAssetData"));
		DistanceFieldIndirectionTable.Bind(ParameterMap, TEXT("DistanceFieldIndirectionTable"));
		DistanceFieldBrickTexture.Bind(ParameterMap, TEXT("DistanceFieldBrickTexture"));
		DistanceFieldSampler.Bind(ParameterMap, TEXT("DistanceFieldSampler"));
		DistanceFieldBrickSize.Bind(ParameterMap, TEXT("DistanceFieldBrickSize"));
		DistanceFieldUniqueDataBrickSize.Bind(ParameterMap, TEXT("DistanceFieldUniqueDataBrickSize"));
		DistanceFieldBrickAtlasSizeInBricks.Bind(ParameterMap, TEXT("DistanceFieldBrickAtlasSizeInBricks"));
		DistanceFieldBrickAtlasMask.Bind(ParameterMap, TEXT("DistanceFieldBrickAtlasMask"));
		DistanceFieldBrickAtlasSizeLog2.Bind(ParameterMap, TEXT("DistanceFieldBrickAtlasSizeLog2"));
		DistanceFieldBrickAtlasTexelSize.Bind(ParameterMap, TEXT("DistanceFieldBrickAtlasTexelSize"));
	}

	template<typename TShaderRHI, typename TRHICommandList>
	void Set(
		TRHICommandList& RHICmdList,
		TShaderRHI* ShaderRHI,
		const TDistanceFieldCulledObjectBuffers<PrimitiveType>& ObjectBuffers,
		const FDistanceFieldSceneData& DistanceFieldSceneData,
		FRHITexture* HFVisibilityAtlas = nullptr)
	{
		ObjectIndirectArguments.SetBuffer(RHICmdList, ShaderRHI, ObjectBuffers.ObjectIndirectArguments);
		CulledObjectBounds.SetBuffer(RHICmdList, ShaderRHI, ObjectBuffers.Bounds);
		CulledObjectData.SetBuffer(RHICmdList, ShaderRHI, ObjectBuffers.Data);

		if (CulledObjectBoxBounds.IsBound())
		{
			check(ObjectBuffers.bWantBoxBounds);
			CulledObjectBoxBounds.SetBuffer(RHICmdList, ShaderRHI, ObjectBuffers.BoxBounds);
		}

		FDistanceFieldAtlasParameters AtlasParameters = DistanceField::SetupAtlasParameters(DistanceFieldSceneData);

		SetSRVParameter(RHICmdList, ShaderRHI, SceneDistanceFieldAssetData, AtlasParameters.SceneDistanceFieldAssetData);
		SetSRVParameter(RHICmdList, ShaderRHI, DistanceFieldIndirectionTable, AtlasParameters.DistanceFieldIndirectionTable);

		SetTextureParameter(
			RHICmdList,
			ShaderRHI,
			DistanceFieldBrickTexture,
			DistanceFieldSampler,
			TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
			DistanceFieldSceneData.DistanceFieldBrickVolumeTexture->GetRenderTargetItem().ShaderResourceTexture
			);

		SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldBrickSize, AtlasParameters.DistanceFieldBrickSize);
		SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldUniqueDataBrickSize, AtlasParameters.DistanceFieldUniqueDataBrickSize);
		SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldBrickAtlasSizeInBricks, AtlasParameters.DistanceFieldBrickAtlasSizeInBricks);
		SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldBrickAtlasMask, AtlasParameters.DistanceFieldBrickAtlasMask);
		SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldBrickAtlasSizeLog2, AtlasParameters.DistanceFieldBrickAtlasSizeLog2);
		SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldBrickAtlasTexelSize, AtlasParameters.DistanceFieldBrickAtlasTexelSize);

		if (HFVisibilityTexture.IsBound())
		{
			check(HFVisibilityAtlas);
			SetTextureParameter(RHICmdList, ShaderRHI, HFVisibilityTexture, HFVisibilityAtlas);
		}
	}

	template<typename TRHIShader, typename TRHICommandList>
	void UnsetParameters(TRHICommandList& RHICmdList, TRHIShader* ShaderRHI)
	{
		ObjectIndirectArguments.UnsetUAV(RHICmdList, ShaderRHI);
		CulledObjectBounds.UnsetUAV(RHICmdList, ShaderRHI);
		CulledObjectData.UnsetUAV(RHICmdList, ShaderRHI);
		CulledObjectBoxBounds.UnsetUAV(RHICmdList, ShaderRHI);
	}

	void GetUAVs(const TDistanceFieldCulledObjectBuffers<PrimitiveType>& ObjectBuffers, TArray<FRHIUnorderedAccessView*>& UAVs)
	{
		uint32 MaxIndex = 0;
		MaxIndex = FMath::Max(MaxIndex, ObjectIndirectArguments.GetUAVIndex());
		MaxIndex = FMath::Max(MaxIndex, CulledObjectBounds.GetUAVIndex());
		MaxIndex = FMath::Max(MaxIndex, CulledObjectData.GetUAVIndex());
		MaxIndex = FMath::Max(MaxIndex, CulledObjectBoxBounds.GetUAVIndex());

		UAVs.AddZeroed(MaxIndex + 1);

		if (ObjectIndirectArguments.IsUAVBound())
		{
			UAVs[ObjectIndirectArguments.GetUAVIndex()] = ObjectBuffers.ObjectIndirectArguments.UAV;
		}

		if (CulledObjectBounds.IsUAVBound())
		{
			UAVs[CulledObjectBounds.GetUAVIndex()] = ObjectBuffers.Bounds.UAV;
		}

		if (CulledObjectData.IsUAVBound())
		{
			UAVs[CulledObjectData.GetUAVIndex()] = ObjectBuffers.Data.UAV;
		}

		if (CulledObjectBoxBounds.IsUAVBound())
		{
			UAVs[CulledObjectBoxBounds.GetUAVIndex()] = ObjectBuffers.BoxBounds.UAV;
		}

		check(UAVs.Num() > 0);
	}

	friend FArchive& operator<<(FArchive& Ar, TDistanceFieldCulledObjectBufferParameters<PrimitiveType>& P)
	{
		Ar << P.ObjectIndirectArguments;
		Ar << P.CulledObjectBounds;
		Ar << P.CulledObjectData;
		Ar << P.CulledObjectBoxBounds;
		Ar << P.HFVisibilityTexture;
		Ar << P.SceneDistanceFieldAssetData;
		Ar << P.DistanceFieldIndirectionTable;
		Ar << P.DistanceFieldBrickTexture;
		Ar << P.DistanceFieldSampler;
		Ar << P.DistanceFieldBrickSize;
		Ar << P.DistanceFieldUniqueDataBrickSize;
		Ar << P.DistanceFieldBrickAtlasSizeInBricks;
		Ar << P.DistanceFieldBrickAtlasMask;
		Ar << P.DistanceFieldBrickAtlasSizeLog2;
		Ar << P.DistanceFieldBrickAtlasTexelSize;
		return Ar;
	}

private:
	
	LAYOUT_FIELD(FRWShaderParameter, ObjectIndirectArguments)
	LAYOUT_FIELD(FRWShaderParameter, CulledObjectBounds)
	LAYOUT_FIELD(FRWShaderParameter, CulledObjectData)
	LAYOUT_FIELD(FRWShaderParameter, CulledObjectBoxBounds)
	LAYOUT_FIELD(FShaderResourceParameter, HFVisibilityTexture);
	LAYOUT_FIELD(FShaderResourceParameter, SceneDistanceFieldAssetData)
	LAYOUT_FIELD(FShaderResourceParameter, DistanceFieldIndirectionTable)
	LAYOUT_FIELD(FShaderResourceParameter, DistanceFieldBrickTexture)
	LAYOUT_FIELD(FShaderResourceParameter, DistanceFieldSampler)
	LAYOUT_FIELD(FShaderParameter, DistanceFieldBrickSize);
	LAYOUT_FIELD(FShaderParameter, DistanceFieldUniqueDataBrickSize);
	LAYOUT_FIELD(FShaderParameter, DistanceFieldBrickAtlasSizeInBricks);
	LAYOUT_FIELD(FShaderParameter, DistanceFieldBrickAtlasMask);
	LAYOUT_FIELD(FShaderParameter, DistanceFieldBrickAtlasSizeLog2);
	LAYOUT_FIELD(FShaderParameter, DistanceFieldBrickAtlasTexelSize);
};

class FCPUUpdatedBuffer
{
public:

	EPixelFormat Format;
	int32 Stride;
	int32 MaxElements;

	// Volatile must be written every frame before use.  Supports multiple writes per frame on PS4, unlike Dynamic.
	bool bVolatile;

	FBufferRHIRef Buffer;
	FShaderResourceViewRHIRef BufferSRV;

	FCPUUpdatedBuffer()
	{
		Format = PF_A32B32G32R32F;
		Stride = 1;
		MaxElements = 0;
		bVolatile = true;
	}

	void Initialize()
	{
		if (MaxElements > 0 && Stride > 0)
		{
			FRHIResourceCreateInfo CreateInfo(TEXT("FCPUUpdatedBuffer"));
			Buffer = RHICreateVertexBuffer(MaxElements * Stride * GPixelFormats[Format].BlockBytes, (bVolatile ? BUF_Volatile : BUF_Dynamic)  | BUF_ShaderResource, CreateInfo);
			BufferSRV = RHICreateShaderResourceView(Buffer, GPixelFormats[Format].BlockBytes, Format);
		}
	}

	void Release()
	{
		Buffer.SafeRelease();
		BufferSRV.SafeRelease(); 
	}

	size_t GetSizeBytes() const
	{
		return MaxElements * Stride * GPixelFormats[Format].BlockBytes;
	}
};

BEGIN_SHADER_PARAMETER_STRUCT(FLightTileIntersectionParameters, )
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWShadowTileNumCulledObjects)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWShadowTileStartOffsets)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWNextStartOffset)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWShadowTileArrayData)

	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ShadowTileNumCulledObjects)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ShadowTileStartOffsets)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, NextStartOffset)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ShadowTileArrayData)

	SHADER_PARAMETER(FIntPoint, ShadowTileListGroupSize)
END_SHADER_PARAMETER_STRUCT()

extern void CullDistanceFieldObjectsForLight(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FLightSceneProxy* LightSceneProxy, 
	EDistanceFieldPrimitiveType PrimitiveType,
	const FMatrix& WorldToShadowValue, 
	int32 NumPlanes, 
	const FPlane* PlaneData, 
	const FVector4& ShadowBoundingSphereValue,
	float ShadowBoundingRadius,
	const FDistanceFieldObjectBufferParameters& ObjectBufferParameters,
	FDistanceFieldCulledObjectBufferParameters& CulledObjectBufferParameters,
	FLightTileIntersectionParameters& LightTileIntersectionParameters);

extern TGlobalResource<FDistanceFieldObjectBufferResource> GAOCulledObjectBuffers;

extern bool SupportsDistanceFieldAO(ERHIFeatureLevel::Type FeatureLevel, EShaderPlatform ShaderPlatform);

template <bool bIsAType> struct TSelector {};

template <>
struct TSelector<true>
{
	template <typename AType, typename BType>
	AType& operator()(AType& A, BType& B)
	{
		return A;
	}
};

template <>
struct TSelector<false>
{
	template <typename AType, typename BType>
	BType& operator()(AType& A, BType& B)
	{
		return B;
	}
};