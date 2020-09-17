// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
LandscapeRenderMobile.h: Mobile landscape rendering
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "RenderResource.h"
#include "VertexFactory.h"
#include "LandscapeRender.h"
#include "Runtime/Landscape/Private/LandscapePrivate.h"

#define LANDSCAPE_MAX_ES_LOD_COMP	2
#define LANDSCAPE_MAX_ES_LOD		6

struct FLandscapeMobileVertex
{
	uint8 Position[4]; // Pos + LOD 0 Height
	uint8 LODHeights[LANDSCAPE_MAX_ES_LOD_COMP*4];
};

/** vertex factory for VTF-heightmap terrain  */
class FLandscapeVertexFactoryMobile : public FLandscapeVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(FLandscapeVertexFactoryMobile);

	typedef FLandscapeVertexFactory Super;
public:

	struct FDataType : FLandscapeVertexFactory::FDataType
	{
		/** stream which has heights of each LOD levels */
		TArray<FVertexStreamComponent,TFixedAllocator<LANDSCAPE_MAX_ES_LOD_COMP> > LODHeightsComponent;
	};

	FLandscapeVertexFactoryMobile(ERHIFeatureLevel::Type InFeatureLevel)
		: FLandscapeVertexFactory(InFeatureLevel)
	{
	}

	virtual ~FLandscapeVertexFactoryMobile()
	{
		ReleaseResource();
	}

	/**
	* Should we cache the material's shadertype on this platform with this vertex factory? 
	*/
	static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters);

	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment )
	{
		FVertexFactory::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("NUM_VF_PACKED_INTERPOLANTS"), TEXT("1"));
	}

	// FRenderResource interface.
	virtual void InitRHI() override;

	/**
	 * An implementation of the interface used by TSynchronizedResource to update the resource with new data from the game thread.
	 */
	void SetData(const FDataType& InData)
	{
		MobileData = InData;
		UpdateRHI();
	}

private:
	/** stream component data bound to this vertex factory */
	FDataType MobileData; 

	friend class FLandscapeComponentSceneProxyMobile;
};

//
// FLandscapeVertexBuffer
//
class FLandscapeVertexBufferMobile : public FVertexBuffer
{
	TArray<uint8> VertexData;
	int32 DataSize;
public:

	/** Constructor. */
	FLandscapeVertexBufferMobile(TArray<uint8> InVertexData)
	:	VertexData(InVertexData)
	,	DataSize(InVertexData.Num())
	{
		INC_DWORD_STAT_BY(STAT_LandscapeVertexMem, DataSize);
	}

	/** Destructor. */
	virtual ~FLandscapeVertexBufferMobile()
	{
		ReleaseResource();
		DEC_DWORD_STAT_BY(STAT_LandscapeVertexMem, DataSize);
	}

	/** 
	* Initialize the RHI for this rendering resource 
	*/
	virtual void InitRHI() override;

	static LANDSCAPE_API void UpdateMemoryStat(int32 Delta);
};

/**
 * Container for FLandscapeVertexBufferMobile that we can reference from a thread-safe shared pointer
 * while ensuring the vertex buffer is always destroyed on the render thread.
 **/
struct FLandscapeMobileRenderData
{
	FLandscapeVertexBufferMobile* VertexBuffer = nullptr;
	struct FLandscapeMobileHoleData* HoleData = nullptr;
	FOccluderVertexArraySP OccluderVerticesSP;
	uint8 CurrentFirstLODIdx;
	bool bReadyForStreaming = false;

	FLandscapeMobileRenderData(const TArray<uint8>& InPlatformData, uint8 InCurFirstLODIdx);
	~FLandscapeMobileRenderData();
};

//
// FLandscapeComponentSceneProxy
//
class FLandscapeComponentSceneProxyMobile final : public FLandscapeComponentSceneProxy
{
	TSharedPtr<FLandscapeMobileRenderData, ESPMode::ThreadSafe> MobileRenderData;

	virtual ~FLandscapeComponentSceneProxyMobile();

public:
	SIZE_T GetTypeHash() const override;

	FLandscapeComponentSceneProxyMobile(ULandscapeComponent* InComponent);

	virtual void CreateRenderThreadResources() override;
	virtual int32 CollectOccluderElements(FOccluderElementsCollector& Collector) const override;

	friend class FLandscapeVertexBufferMobile;

	virtual void ApplyMeshElementModifier(FMeshBatchElement& InOutMeshElement, int32 InLodIndex) const override;

#if PLATFORM_SUPPORTS_LANDSCAPE_VISUAL_MESH_LOD_STREAMING
	virtual uint8 GetCurrentFirstLODIdx_RenderThread() const override;
#endif
};


class FLandscapeFixedGridVertexFactoryMobile : public FLandscapeVertexFactoryMobile
{
	DECLARE_VERTEX_FACTORY_TYPE(FLandscapeFixedGridVertexFactoryMobile);

public:
	FLandscapeFixedGridVertexFactoryMobile(ERHIFeatureLevel::Type InFeatureLevel)
		: FLandscapeVertexFactoryMobile(InFeatureLevel)
	{
	}

	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);
	static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters);
};
