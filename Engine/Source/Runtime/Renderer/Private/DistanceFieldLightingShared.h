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

class FLightSceneProxy;
class FMaterialRenderProxy;
class FPrimitiveSceneInfo;
class FSceneRenderer;
class FShaderParameterMap;
class FViewInfo;

DECLARE_LOG_CATEGORY_EXTERN(LogDistanceField, Warning, All);

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

	// In float4's
	static int32 ObjectDataStride;

	int32 MaxObjects;

	FRWBuffer Bounds;
	FRWBuffer Data;

	TDistanceFieldObjectBuffers()
	{
		MaxObjects = 0;
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
		DistanceFieldTexture.Bind(ParameterMap, TEXT("DistanceFieldTexture"));
		DistanceFieldSampler.Bind(ParameterMap, TEXT("DistanceFieldSampler"));
		DistanceFieldAtlasTexelSize.Bind(ParameterMap, TEXT("DistanceFieldAtlasTexelSize"));
	}

	template<typename TParamRef>
	void Set(
		FRHIComputeCommandList& RHICmdList,
		const TParamRef& ShaderRHI,
		const TDistanceFieldObjectBuffers<PrimitiveType>& ObjectBuffers,
		int32 NumObjectsValue,
		FRHITexture* TextureAtlas,
		int32 AtlasSizeX,
		int32 AtlasSizeY,
		int32 AtlasSizeZ,
		bool bBarrier = false)
	{
		if (bBarrier)
		{
			FRHITransitionInfo UAVTransitions[2];
			UAVTransitions[0] = FRHITransitionInfo(ObjectBuffers.Bounds.UAV, ERHIAccess::Unknown, ERHIAccess::ERWBarrier);
			UAVTransitions[1] = FRHITransitionInfo(ObjectBuffers.Data.UAV, ERHIAccess::Unknown, ERHIAccess::ERWBarrier);
			RHICmdList.Transition(MakeArrayView(UAVTransitions, UE_ARRAY_COUNT(UAVTransitions)));
		}

		SceneObjectBounds.SetBuffer(RHICmdList, ShaderRHI, ObjectBuffers.Bounds);
		SceneObjectData.SetBuffer(RHICmdList, ShaderRHI, ObjectBuffers.Data);
		SetShaderValue(RHICmdList, ShaderRHI, NumSceneObjects, NumObjectsValue);

		SetTextureParameter(
			RHICmdList,
			ShaderRHI,
			DistanceFieldTexture,
			DistanceFieldSampler,
			TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
			TextureAtlas);

		const int32 NumTexelsOneDimX = AtlasSizeX;
		const int32 NumTexelsOneDimY = AtlasSizeY;
		const int32 NumTexelsOneDimZ = AtlasSizeZ;
		const FVector InvTextureDim(1.0f / NumTexelsOneDimX, 1.0f / NumTexelsOneDimY, 1.0f / NumTexelsOneDimZ);
		SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldAtlasTexelSize, InvTextureDim);
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
		Ar << P.SceneObjectBounds << P.SceneObjectData << P.NumSceneObjects << P.DistanceFieldTexture << P.DistanceFieldSampler << P.DistanceFieldAtlasTexelSize;
		return Ar;
	}

	bool AnyBound() const
	{
		return SceneObjectBounds.IsBound() || SceneObjectData.IsBound() || NumSceneObjects.IsBound() || DistanceFieldTexture.IsBound() || DistanceFieldSampler.IsBound() || DistanceFieldAtlasTexelSize.IsBound();
	}

private:
	
	LAYOUT_FIELD(FRWShaderParameter, SceneObjectBounds)
	LAYOUT_FIELD(FRWShaderParameter, SceneObjectData)
	LAYOUT_FIELD(FShaderParameter, NumSceneObjects)
	LAYOUT_FIELD(FShaderResourceParameter, DistanceFieldTexture)
	LAYOUT_FIELD(FShaderResourceParameter, DistanceFieldSampler)
	LAYOUT_FIELD(FShaderParameter, DistanceFieldAtlasTexelSize)
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

			ObjectIndirectArguments.Initialize(sizeof(uint32), 5, PF_R32_UINT, BUF_Static | BUF_DrawIndirect, ObjectIndirectArgumentsDebugName);
			ObjectIndirectDispatch.Initialize(sizeof(uint32), 3, PF_R32_UINT, BUF_Static | BUF_DrawIndirect, ObjectIndirectDispatchDebugName);
			Bounds.Initialize(sizeof(FVector4), BoundsNumElements, BUF_Static | FastVRamFlag, BoundsDebugName);
			Data.Initialize(sizeof(FVector4), MaxObjects * ObjectDataStride, BUF_Static | FastVRamFlag, DataDebugName);

			if (bWantBoxBounds)
			{
				BoxBounds.Initialize(sizeof(FVector4), MaxObjects * ObjectBoxBoundsStride, BUF_Static | FastVRamFlag, BoxBoundsDebugName);
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
		DistanceFieldTexture.Bind(ParameterMap, TEXT("DistanceFieldTexture"));
		DistanceFieldSampler.Bind(ParameterMap, TEXT("DistanceFieldSampler"));
		DistanceFieldAtlasTexelSize.Bind(ParameterMap, TEXT("DistanceFieldAtlasTexelSize"));
	}

	template<typename TShaderRHI, typename TRHICommandList>
	void Set(
		TRHICommandList& RHICmdList,
		TShaderRHI* ShaderRHI,
		const TDistanceFieldCulledObjectBuffers<PrimitiveType>& ObjectBuffers,
		FRHITexture* TextureAtlas,
		const FIntVector& AtlasSizes,
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

		SetTextureParameter(
			RHICmdList,
			ShaderRHI,
			DistanceFieldTexture,
			DistanceFieldSampler,
			TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
			TextureAtlas
			);

		if (HFVisibilityTexture.IsBound())
		{
			check(HFVisibilityAtlas);
			SetTextureParameter(RHICmdList, ShaderRHI, HFVisibilityTexture, HFVisibilityAtlas);
		}

		const int32 NumTexelsOneDimX = AtlasSizes.X;
		const int32 NumTexelsOneDimY = AtlasSizes.Y;
		const int32 NumTexelsOneDimZ = AtlasSizes.Z;
		const FVector InvTextureDim(1.0f / NumTexelsOneDimX, 1.0f / NumTexelsOneDimY, 1.0f / NumTexelsOneDimZ);
		SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldAtlasTexelSize, InvTextureDim);
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
		Ar << P.DistanceFieldTexture;
		Ar << P.DistanceFieldSampler;
		Ar << P.DistanceFieldAtlasTexelSize;
		return Ar;
	}

private:
	
	LAYOUT_FIELD(FRWShaderParameter, ObjectIndirectArguments)
	LAYOUT_FIELD(FRWShaderParameter, CulledObjectBounds)
	LAYOUT_FIELD(FRWShaderParameter, CulledObjectData)
	LAYOUT_FIELD(FRWShaderParameter, CulledObjectBoxBounds)
	LAYOUT_FIELD(FShaderResourceParameter, HFVisibilityTexture);
	LAYOUT_FIELD(FShaderResourceParameter, DistanceFieldTexture)
	LAYOUT_FIELD(FShaderResourceParameter, DistanceFieldSampler)
	LAYOUT_FIELD(FShaderParameter, DistanceFieldAtlasTexelSize)
};

class FCPUUpdatedBuffer
{
public:

	EPixelFormat Format;
	int32 Stride;
	int32 MaxElements;

	// Volatile must be written every frame before use.  Supports multiple writes per frame on PS4, unlike Dynamic.
	bool bVolatile;

	FVertexBufferRHIRef Buffer;
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
			FRHIResourceCreateInfo CreateInfo;
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

static int32 LightTileDataStride = 1;

/**  */
class FLightTileIntersectionResources
{
public:

	FLightTileIntersectionResources() 
		: TileDimensions(FIntPoint::ZeroValue)
		, TileAlignedDimensions(FIntPoint::ZeroValue)
		, b16BitIndices(false)
	{}

	void Initialize(int32 MaxNumObjectsPerTile)
	{
		TileAlignedDimensions = FIntPoint(Align(TileDimensions.X, 64), Align(TileDimensions.Y, 64));
			
		TileNumCulledObjects.Initialize(sizeof(uint32), TileAlignedDimensions.X * TileAlignedDimensions.Y, PF_R32_UINT, BUF_Static, TEXT("FLightTileIntersectionResources::TileNumCulledObjects"));
		TileStartOffsets.Initialize(sizeof(uint32), TileAlignedDimensions.X * TileAlignedDimensions.Y, PF_R32_UINT, BUF_Static, TEXT("FLightTileIntersectionResources::TileStartOffsets"));
		NextStartOffset.Initialize(sizeof(uint32), 1, PF_R32_UINT, BUF_Static, TEXT("FLightTileIntersectionResources::NextStartOffset"));

		//@todo - handle max exceeded
		TileArrayData.Initialize(b16BitIndices ? sizeof(uint16) : sizeof(uint32), MaxNumObjectsPerTile * TileAlignedDimensions.X * TileAlignedDimensions.Y * LightTileDataStride, b16BitIndices ? PF_R16_UINT : PF_R32_UINT, BUF_Static, TEXT("FLightTileIntersectionResources::TileArrayData"));
	}

	void Release()
	{
		TileNumCulledObjects.Release();
		NextStartOffset.Release();
		TileStartOffsets.Release();
		TileArrayData.Release();
	}

	size_t GetSizeBytes() const
	{
		return TileNumCulledObjects.NumBytes + NextStartOffset.NumBytes + TileStartOffsets.NumBytes + TileArrayData.NumBytes;
	}

	FIntPoint GetTileAlignedDimensions() const 
	{
		return TileAlignedDimensions;
	}
	
	static FIntPoint GetAlignedDimensions(FIntPoint InTileDimensions)
	{
		return FIntPoint(Align(InTileDimensions.X, 64), Align(InTileDimensions.Y, 64));
	}

	FIntPoint TileDimensions;
	FIntPoint TileAlignedDimensions;

	FRWBuffer TileNumCulledObjects;
	FRWBuffer NextStartOffset;
	FRWBuffer TileStartOffsets;
	FRWBuffer TileArrayData;
	bool b16BitIndices;
};

class FLightTileIntersectionParameters
{
	DECLARE_TYPE_LAYOUT(FLightTileIntersectionParameters, NonVirtual);
public:

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("SHADOW_TILE_ARRAY_DATA_STRIDE"), LightTileDataStride);
	}

	void Bind(const FShaderParameterMap& ParameterMap)
	{
		ShadowTileNumCulledObjects.Bind(ParameterMap, TEXT("ShadowTileNumCulledObjects"));
		ShadowTileStartOffsets.Bind(ParameterMap, TEXT("ShadowTileStartOffsets"));
		NextStartOffset.Bind(ParameterMap, TEXT("NextStartOffset"));
		ShadowTileArrayData.Bind(ParameterMap, TEXT("ShadowTileArrayData"));
		ShadowTileListGroupSize.Bind(ParameterMap, TEXT("ShadowTileListGroupSize"));
		ShadowAverageObjectsPerTile.Bind(ParameterMap, TEXT("ShadowAverageObjectsPerTile"));
	}

	bool IsBound() const
	{
		return ShadowTileNumCulledObjects.IsBound() || ShadowTileStartOffsets.IsBound() || NextStartOffset.IsBound() || ShadowTileArrayData.IsBound() || ShadowTileListGroupSize.IsBound() || ShadowAverageObjectsPerTile.IsBound();
	}

	template<typename TParamRef, typename TRHICommandList>
	void Set(TRHICommandList& RHICmdList, const TParamRef& ShaderRHI, const FLightTileIntersectionResources& LightTileIntersectionResources)
	{
		ShadowTileNumCulledObjects.SetBuffer(RHICmdList, ShaderRHI, LightTileIntersectionResources.TileNumCulledObjects);
		ShadowTileStartOffsets.SetBuffer(RHICmdList, ShaderRHI, LightTileIntersectionResources.TileStartOffsets);

		NextStartOffset.SetBuffer(RHICmdList, ShaderRHI, LightTileIntersectionResources.NextStartOffset);

		// Bind sorted array data if we are after the sort pass
		ShadowTileArrayData.SetBuffer(RHICmdList, ShaderRHI, LightTileIntersectionResources.TileArrayData);

		SetShaderValue(RHICmdList, ShaderRHI, ShadowTileListGroupSize, LightTileIntersectionResources.TileDimensions);
		SetShaderValue(RHICmdList, ShaderRHI, ShadowAverageObjectsPerTile, GAverageObjectsPerShadowCullTile);
	}

	void GetUAVs(FLightTileIntersectionResources& TileIntersectionResources, TArray<FRHIUnorderedAccessView*>& UAVs)
	{
		int32 MaxIndex = FMath::Max(
			FMath::Max(ShadowTileNumCulledObjects.GetUAVIndex(), ShadowTileStartOffsets.GetUAVIndex()), 
			FMath::Max(NextStartOffset.GetUAVIndex(), ShadowTileArrayData.GetUAVIndex()));
		UAVs.AddZeroed(MaxIndex + 1);

		if (ShadowTileNumCulledObjects.IsUAVBound())
		{
			UAVs[ShadowTileNumCulledObjects.GetUAVIndex()] = TileIntersectionResources.TileNumCulledObjects.UAV;
		}

		if (ShadowTileStartOffsets.IsUAVBound())
		{
			UAVs[ShadowTileStartOffsets.GetUAVIndex()] = TileIntersectionResources.TileStartOffsets.UAV;
		}

		if (NextStartOffset.IsUAVBound())
		{
			UAVs[NextStartOffset.GetUAVIndex()] = TileIntersectionResources.NextStartOffset.UAV;
		}

		if (ShadowTileArrayData.IsUAVBound())
		{
			UAVs[ShadowTileArrayData.GetUAVIndex()] = TileIntersectionResources.TileArrayData.UAV;
		}

		check(UAVs.Num() > 0);
	}

	template<typename TParamRef>
	void UnsetParameters(FRHIComputeCommandList& RHICmdList, const TParamRef& ShaderRHI)
	{
		ShadowTileNumCulledObjects.UnsetUAV(RHICmdList, ShaderRHI);
		ShadowTileStartOffsets.UnsetUAV(RHICmdList, ShaderRHI);
		NextStartOffset.UnsetUAV(RHICmdList, ShaderRHI);
		ShadowTileArrayData.UnsetUAV(RHICmdList, ShaderRHI);
	}

	friend FArchive& operator<<(FArchive& Ar, FLightTileIntersectionParameters& P)
	{
		Ar << P.ShadowTileNumCulledObjects;
		Ar << P.ShadowTileStartOffsets;
		Ar << P.NextStartOffset;
		Ar << P.ShadowTileArrayData;
		Ar << P.ShadowTileListGroupSize;
		Ar << P.ShadowAverageObjectsPerTile;
		return Ar;
	}

private:
	
		LAYOUT_FIELD(FRWShaderParameter, ShadowTileNumCulledObjects)
		LAYOUT_FIELD(FRWShaderParameter, ShadowTileStartOffsets)
		LAYOUT_FIELD(FRWShaderParameter, NextStartOffset)
		LAYOUT_FIELD(FRWShaderParameter, ShadowTileArrayData)
		LAYOUT_FIELD(FShaderParameter, ShadowTileListGroupSize)
		LAYOUT_FIELD(FShaderParameter, ShadowAverageObjectsPerTile)
	
};

extern void CullDistanceFieldObjectsForLight(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FLightSceneProxy* LightSceneProxy, 
	const FMatrix& WorldToShadowValue, 
	int32 NumPlanes, 
	const FPlane* PlaneData, 
	const FVector4& ShadowBoundingSphereValue,
	float ShadowBoundingRadius,
	TUniquePtr<class FLightTileIntersectionResources>& TileIntersectionResources);

extern void CullHeightFieldObjectsForLight(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FLightSceneProxy* LightSceneProxy,
	const FMatrix& WorldToShadowValue,
	int32 NumPlanes,
	const FPlane* PlaneData,
	const FVector4& ShadowBoundingSphereValue,
	float ShadowBoundingRadius,
	TUniquePtr<class FLightTileIntersectionResources>& TileIntersectionResources);

class FUniformMeshBuffers
{
public:

	int32 MaxElements;

	FVertexBufferRHIRef TriangleData;
	FShaderResourceViewRHIRef TriangleDataSRV;

	FRWBuffer TriangleAreas;
	FRWBuffer TriangleCDFs;

	FUniformMeshBuffers()
	{
		MaxElements = 0;
	}

	void Initialize();

	void Release()
	{
		TriangleData.SafeRelease();
		TriangleDataSRV.SafeRelease(); 
		TriangleAreas.Release();
		TriangleCDFs.Release();
	}
};

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