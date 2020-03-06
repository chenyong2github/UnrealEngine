// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderResource.h"
#include "VertexFactory.h"
#include "LidarPointCloudShared.h"
#include "HAL/ThreadSafeBool.h"
#include "MeshMaterialShader.h"

/**
 * Base class for the buffers
 */
class FLidarPointCloudBuffer
{
public:
	/** Resizes the buffer to the specified capacity, if necessary. Must be called from Rendering thread. */
	void Resize(const uint32& RequestedCapacity);

	FLidarPointCloudBuffer(const uint32& ElementSize) : Capacity(100000), ElementSize(ElementSize) {}

	virtual FString GetFriendlyName() const { return TEXT("FPointCloudBuffer"); }

	/** Returns the total size of this buffer */
	virtual uint32 GetSize() const { return Capacity * ElementSize; }

protected:
	uint32 Capacity;
	uint32 ElementSize;

	FLidarPointCloudBuffer() : FLidarPointCloudBuffer(0) {}
	virtual ~FLidarPointCloudBuffer();

	virtual void Initialize() = 0;
	virtual void Release() = 0;
};

/**
 * This class creates an IndexBuffer shared between all assets and all instances.
 */
class FLidarPointCloudIndexBuffer : public FLidarPointCloudBuffer, public FIndexBuffer
{
public:
	FLidarPointCloudIndexBuffer() : FLidarPointCloudBuffer(sizeof(uint32)) { }

	uint32 GetPointOffset() const { return PointOffset; }

private:
	virtual void InitRHI() override;
	virtual void Initialize() override { InitResource(); }
	virtual void Release() override { ReleaseResource(); }
	virtual FString GetFriendlyName() const override { return TEXT("FLidarPointCloudIndexBuffer"); }

private:
	uint32 PointOffset;
};

/**
 * Encapsulates a GPU read buffer with its SRV.
 */
class FLidarPointCloudRenderBuffer : public FLidarPointCloudBuffer, public FRenderResource
{
public:
	FVertexBufferRHIRef Buffer;
	FShaderResourceViewRHIRef SRV;

	int32 PointCount;

	FLidarPointCloudRenderBuffer() : FLidarPointCloudBuffer(4) {}
	virtual ~FLidarPointCloudRenderBuffer() { Release(); }

	virtual void InitRHI() override;
	virtual void ReleaseRHI() override;

	virtual void Initialize() override { InitResource(); }
	virtual void Release() override { ReleaseResource(); }
	virtual FString GetFriendlyName() const override { return TEXT("FLidarPointCloudRenderBuffer"); }

private:
	FLidarPointCloudRenderBuffer(const FLidarPointCloudRenderBuffer&) = delete;
	FLidarPointCloudRenderBuffer(FLidarPointCloudRenderBuffer&&) = delete;
	FLidarPointCloudRenderBuffer& operator=(const FLidarPointCloudRenderBuffer&) = delete;
	FLidarPointCloudRenderBuffer& operator=(FLidarPointCloudRenderBuffer&&) = delete;
};

/**
 * Holds all data to be passed to the FLidarPointCloudVertexFactoryShaderParameters as UserData
 */
struct FLidarPointCloudBatchElementUserData
{
	FRHIShaderResourceView* DataBuffer;
	int32 IndexDivisor;
	int32 FirstElementIndex;
	FVector LocationOffset;
	float VDMultiplier;
	int32 SizeOffset;
	float RootCellSize;
	int32 bUseLODColoration;
	float SpriteSizeMultiplier;
	FVector ViewRightVector;
	FVector ViewUpVector;
	FVector BoundsSize;
	FVector ElevationColorBottom;
	FVector ElevationColorTop;
	int32 bUseCircle;
	int32 bUseColorOverride;
	int32 bUseElevationColor;
	FVector4 Offset;
	FVector4 Contrast;
	FVector4 Saturation;
	FVector4 Gamma;
	FVector Tint;
	float IntensityInfluence;
	int32 bUseClassification;
	FVector4 ClassificationColors[32];

	FLidarPointCloudBatchElementUserData(const float& VDMultiplier, const float& RootCellSize)
		: DataBuffer(nullptr)
		, IndexDivisor(4)
		, VDMultiplier(VDMultiplier)
		, RootCellSize(RootCellSize)
		, bUseLODColoration(false)
	{
	}

	void SetClassificationColors(const TMap<int32, FLinearColor>& InClassificationColors);
};

/**
 * Binds shader parameters necessary for rendering
 */
class FLidarPointCloudVertexFactoryShaderParameters : public FVertexFactoryShaderParameters
{
	DECLARE_INLINE_TYPE_LAYOUT(FLidarPointCloudVertexFactoryShaderParameters, NonVirtual);

public:
	void Bind(const FShaderParameterMap& ParameterMap);
	void GetElementShaderBindings(const class FSceneInterface* Scene, const FSceneView* View, const FMeshMaterialShader* Shader, const EVertexInputStreamType InputStreamType, ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory, const FMeshBatchElement& BatchElement, class FMeshDrawSingleShaderBindings& ShaderBindings, FVertexInputStreamArray& VertexStreams) const;

	LAYOUT_FIELD(FShaderResourceParameter, DataBuffer);
	LAYOUT_FIELD(FShaderParameter, IndexDivisor);
	LAYOUT_FIELD(FShaderParameter, FirstElementIndex);
	LAYOUT_FIELD(FShaderParameter, LocationOffset);
	LAYOUT_FIELD(FShaderParameter, VDMultiplier);
	LAYOUT_FIELD(FShaderParameter, SizeOffset);
	LAYOUT_FIELD(FShaderParameter, RootCellSize);
	LAYOUT_FIELD(FShaderParameter, bUseLODColoration);
	LAYOUT_FIELD(FShaderParameter, SpriteSizeMultiplier);
	LAYOUT_FIELD(FShaderParameter, ViewRightVector);
	LAYOUT_FIELD(FShaderParameter, ViewUpVector);
	LAYOUT_FIELD(FShaderParameter, BoundsSize);
	LAYOUT_FIELD(FShaderParameter, ElevationColorBottom);
	LAYOUT_FIELD(FShaderParameter, ElevationColorTop);
	LAYOUT_FIELD(FShaderParameter, bUseCircle);
	LAYOUT_FIELD(FShaderParameter, bUseColorOverride);
	LAYOUT_FIELD(FShaderParameter, bUseElevationColor);
	LAYOUT_FIELD(FShaderParameter, Offset);
	LAYOUT_FIELD(FShaderParameter, Contrast);
	LAYOUT_FIELD(FShaderParameter, Saturation);
	LAYOUT_FIELD(FShaderParameter, Gamma);
	LAYOUT_FIELD(FShaderParameter, Tint);
	LAYOUT_FIELD(FShaderParameter, IntensityInfluence);
	LAYOUT_FIELD(FShaderParameter, bUseClassification);
	LAYOUT_FIELD(FShaderParameter, ClassificationColors);
};

/**
 * Implementation of the custom Vertex Factory, containing only a ZeroStride position stream.
 */
class FLidarPointCloudVertexFactory : public FVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(FLidarPointCloudVertexFactory);

public:
	static bool ShouldCache(const FVertexFactoryShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
    static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters);

	FLidarPointCloudVertexFactory() : FVertexFactory(ERHIFeatureLevel::SM5) { }

private:
	/** Very simple implementation of a ZeroStride Vertex Buffer */
	class FPointCloudVertexBuffer : public FVertexBuffer
	{
	public:
		virtual void InitRHI() override
		{
			FRHIResourceCreateInfo CreateInfo;
			void* Buffer = nullptr;
			VertexBufferRHI = RHICreateAndLockVertexBuffer(sizeof(FVector), BUF_Static | BUF_ZeroStride, CreateInfo, Buffer);
			FMemory::Memzero(Buffer, sizeof(FVector));
			RHIUnlockVertexBuffer(VertexBufferRHI);
			Buffer = nullptr;
		}

		virtual FString GetFriendlyName() const override { return TEXT("FPointCloudVertexBuffer"); }
	} VertexBuffer;

	virtual void InitRHI() override;
	virtual void ReleaseRHI() override;
};

/** A set of global render resources shared between all Lidar Point Cloud proxies */
extern TGlobalResource<FLidarPointCloudRenderBuffer> GLidarPointCloudRenderBuffer;
extern TGlobalResource<FLidarPointCloudIndexBuffer> GLidarPointCloudIndexBuffer;
extern TGlobalResource<FLidarPointCloudVertexFactory> GLidarPointCloudVertexFactory;