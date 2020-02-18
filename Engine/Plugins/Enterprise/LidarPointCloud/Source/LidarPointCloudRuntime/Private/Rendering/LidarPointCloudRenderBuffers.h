// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderResource.h"
#include "VertexFactory.h"
#include "LidarPointCloudShared.h"
#include "HAL/ThreadSafeBool.h"
#include "GlobalShader.h"

/**
 * Base class for the buffers
 */
class FLidarPointCloudBuffer
{
public:
	/** Resizes the buffer to the specified capacity, if necessary. Must be called from Rendering thread. */
	void Resize(const uint32& RequestedCapacity);

	FLidarPointCloudBuffer(const uint32& ElementSize, const float& Slack, const uint32 InitialCapacity = 0) : Capacity(InitialCapacity), ElementSize(ElementSize), Slack(Slack) {}

	virtual FString GetFriendlyName() const { return TEXT("FPointCloudBuffer"); }

	/** Returns the total size of this buffer */
	virtual uint32 GetSize() const { return Capacity * ElementSize; }

protected:
	uint32 Capacity;
	uint32 ElementSize;
	float Slack;

	FLidarPointCloudBuffer() : FLidarPointCloudBuffer(0, 0) {}

	virtual void Initialize() = 0;
	virtual void Release() = 0;
};

/**
 * This class creates an IndexBuffer shared between all assets and all instances.
 */
class FLidarPointCloudIndexBuffer : public FIndexBuffer, public FLidarPointCloudBuffer
{
public:
	FLidarPointCloudIndexBuffer() : FLidarPointCloudBuffer(sizeof(uint32), 0, 1000000) { }
	~FLidarPointCloudIndexBuffer();

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
 * Encapsulates a GPU read structured buffer with its SRV.
 */
class FLidarPointCloudStructuredBuffer : public FLidarPointCloudBuffer
{
public:
	FStructuredBufferRHIRef Buffer;
	FShaderResourceViewRHIRef SRV;

	FLidarPointCloudStructuredBuffer(const uint32& ElementSize, const float& Slack) : FLidarPointCloudBuffer(ElementSize, Slack) {}
	virtual ~FLidarPointCloudStructuredBuffer() { Release(); }

	virtual void Initialize() override;
	virtual void Release() override;
	virtual FString GetFriendlyName() const override { return TEXT("Structured Buffer"); }

private:
	FLidarPointCloudStructuredBuffer(const FLidarPointCloudStructuredBuffer&) = delete;
	FLidarPointCloudStructuredBuffer(FLidarPointCloudStructuredBuffer&&) = delete;
	FLidarPointCloudStructuredBuffer& operator=(const FLidarPointCloudStructuredBuffer&) = delete;
	FLidarPointCloudStructuredBuffer& operator=(FLidarPointCloudStructuredBuffer&&) = delete;
};

/**
 * Handles creation of the dynamic Instance Data
 */
 // #refactor: Can we avoid referencing FLidarPointCloudOctreeNode here?
struct FLidarPointCloudTraversalOctreeNode;
class FLidarPointCloudInstanceBuffer
{
public:
	FLidarPointCloudStructuredBuffer StructuredBuffer;
	TArray<const FLidarPointCloudTraversalOctreeNode*> Nodes;
	bool bOwnedByEditor;

private:
	uint32 NumInstances;
	uint32 NewNumInstances;

public:
	FLidarPointCloudInstanceBuffer()
		: StructuredBuffer(4, 1.0f)
		, bOwnedByEditor(false)
		, NumInstances(0)
		, NewNumInstances(0)
	{
	}

	FORCEINLINE uint32 GetNumInstances() const { return NumInstances; }

	/** Resets the nodes assigned for building this buffer */
	void Reset();

	/**
	 * Resizes and updates the buffer on the GPU if necessary.
	 * Returns the number of instances used.
	 */
	uint32 UpdateData(bool bUseClassification);

	void AddNode(const FLidarPointCloudTraversalOctreeNode* Node);

	/** Releases the memory held by StructuredBuffer */
	void Release();
};

/**
 * Holds all data to be passed to the FLidarPointCloudVertexFactoryShaderParameters as UserData
 */
struct FLidarPointCloudBatchElementUserData
{
	FRHIShaderResourceView* DataBuffer;
	int32 IndexDivisor;
	float VDMultiplier;
	int32 SizeOffset;
	float RootCellSize;
	int32 bUseLODColoration;
	float SpriteSizeMultiplier;
	FVector ViewRightVector;
	FVector ViewUpVector;
	FVector BoundsSize;
	FVector BoundsOffset;
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
public:
	void Bind(const FShaderParameterMap& ParameterMap);
	void GetElementShaderBindings(const class FSceneInterface* Scene, const FSceneView* View, const FMeshMaterialShader* Shader, const EVertexInputStreamType InputStreamType, ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory, const FMeshBatchElement& BatchElement, class FMeshDrawSingleShaderBindings& ShaderBindings, FVertexInputStreamArray& VertexStreams) const;

private:
	FShaderResourceParameter DataBuffer;
	FShaderParameter IndexDivisor;
	FShaderParameter VDMultiplier;
	FShaderParameter SizeOffset;
	FShaderParameter RootCellSize;
	FShaderParameter bUseLODColoration;
	FShaderParameter SpriteSizeMultiplier;
	FShaderParameter ViewRightVector;
	FShaderParameter ViewUpVector;
	FShaderParameter BoundsSize;
	FShaderParameter BoundsOffset;
	FShaderParameter ElevationColorBottom;
	FShaderParameter ElevationColorTop;
	FShaderParameter bUseCircle;
	FShaderParameter bUseColorOverride;
	FShaderParameter bUseElevationColor;
	FShaderParameter Offset;
	FShaderParameter Contrast;
	FShaderParameter Saturation;
	FShaderParameter Gamma;
	FShaderParameter Tint;
	FShaderParameter IntensityInfluence;
	FShaderParameter bUseClassification;
	FShaderParameter ClassificationColors;
};

/**
 * Implementation of the custom Vertex Factory, containing only a ZeroStride position stream.
 */
class FLidarPointCloudVertexFactory : public FVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(FLidarPointCloudVertexFactory);

public:
	static FVertexFactoryShaderParameters* ConstructShaderParameters(EShaderFrequency ShaderFrequency);

	static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters);
	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment) {}

	FLidarPointCloudVertexFactory() : FVertexFactory(ERHIFeatureLevel::SM5) { }
	~FLidarPointCloudVertexFactory();

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