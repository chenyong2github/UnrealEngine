// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	InstancedStaticMesh.h: Instanced static mesh header
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Containers/IndirectArray.h"
#include "Stats/Stats.h"
#include "HAL/IConsoleManager.h"
#include "RenderingThread.h"
#include "RenderResource.h"
#include "PrimitiveViewRelevance.h"
#include "ShaderParameters.h"
#include "SceneView.h"
#include "VertexFactory.h"
#include "LocalVertexFactory.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "StaticMeshResources.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"

#include "StaticMeshLight.h"

#if WITH_EDITOR
#include "LightMap.h"
#include "ShadowMap.h"
#endif

class ULightComponent;

extern TAutoConsoleVariable<float> CVarFoliageMinimumScreenSize;
extern TAutoConsoleVariable<float> CVarFoliageLODDistanceScale;
extern TAutoConsoleVariable<float> CVarRandomLODRange;
extern TAutoConsoleVariable<int32> CVarMinLOD;

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FInstancedStaticMeshVertexFactoryUniformShaderParameters, ENGINE_API)
	SHADER_PARAMETER_SRV(Buffer<float4>, VertexFetch_InstanceOriginBuffer)
	SHADER_PARAMETER_SRV(Buffer<float4>, VertexFetch_InstanceTransformBuffer)
	SHADER_PARAMETER_SRV(Buffer<float4>, VertexFetch_InstanceLightmapBuffer)
	SHADER_PARAMETER_SRV(Buffer<float>, InstanceCustomDataBuffer)
	SHADER_PARAMETER(int32, NumCustomDataFloats)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

// This must match the maximum a user could specify in the material (see 
// FHLSLMaterialTranslator::TextureCoordinate), otherwise the material will attempt 
// to look up a texture coordinate we didn't provide an element for.
extern const int32 InstancedStaticMeshMaxTexCoord;

/*-----------------------------------------------------------------------------
	FStaticMeshInstanceBuffer
-----------------------------------------------------------------------------*/

/** A vertex buffer of positions. */
class FStaticMeshInstanceBuffer : public FRenderResource
{
public:

	/** Default constructor. */
	FStaticMeshInstanceBuffer(ERHIFeatureLevel::Type InFeatureLevel, bool InRequireCPUAccess);

	/** Destructor. */
	~FStaticMeshInstanceBuffer();

	/**
	 * Initializes the buffer with the component's data.
	 * @param Other - instance data, this call assumes the memory, so this will be empty after the call
	 */
	ENGINE_API void InitFromPreallocatedData(FStaticMeshInstanceData& Other);
	ENGINE_API void UpdateFromCommandBuffer_Concurrent(FInstanceUpdateCmdBuffer& CmdBuffer);

	/**
	 * Specialized assignment operator, only used when importing LOD's. 
	 */
	void operator=(const FStaticMeshInstanceBuffer &Other);

	// Other accessors.
	FORCEINLINE uint32 GetNumInstances() const
	{
		return InstanceData->GetNumInstances();
	}

	FORCEINLINE  void GetInstanceTransform(int32 InstanceIndex, FMatrix& Transform) const
	{
		InstanceData->GetInstanceTransform(InstanceIndex, Transform);
	}

	FORCEINLINE  void GetInstanceShaderValues(int32 InstanceIndex, FVector4 (&InstanceTransform)[3], FVector4& InstanceLightmapAndShadowMapUVBias, FVector4& InstanceOrigin) const
	{
		InstanceData->GetInstanceShaderValues(InstanceIndex, InstanceTransform, InstanceLightmapAndShadowMapUVBias, InstanceOrigin);
	}
	
	FORCEINLINE  void GetInstanceCustomDataValues(int32 InstanceIndex, TArray<float>& InstanceCustomData) const
	{
		InstanceData->GetInstanceShaderCustomDataValues(InstanceIndex, InstanceCustomData);
	}
	
	FORCEINLINE FStaticMeshInstanceData* GetInstanceData() const
	{
		return InstanceData.Get();
	}

	// FRenderResource interface.
	virtual void InitRHI() override;
	virtual void ReleaseRHI() override;
	virtual void InitResource() override;
	virtual void ReleaseResource() override;
	virtual FString GetFriendlyName() const override { return TEXT("Static-mesh instances"); }
	SIZE_T GetResourceSize() const;

	void BindInstanceVertexBuffer(const class FVertexFactory* VertexFactory, struct FInstancedStaticMeshDataType& InstancedStaticMeshData) const;

public:
	/** The vertex data storage type */
	TSharedPtr<FStaticMeshInstanceData, ESPMode::ThreadSafe> InstanceData;

	/** Keep CPU copy of instance data*/
	bool RequireCPUAccess;

	FVertexBufferRHIRef GetInstanceOriginBuffer()
	{
		return InstanceOriginBuffer.VertexBufferRHI;
	}

	FVertexBufferRHIRef GetInstanceTransformBuffer()
	{
		return InstanceTransformBuffer.VertexBufferRHI;
	}

	FVertexBufferRHIRef GetInstanceLightmapBuffer()
	{
		return InstanceLightmapBuffer.VertexBufferRHI;
	}

private:
	class FInstanceOriginBuffer : public FVertexBuffer
	{
		virtual FString GetFriendlyName() const override { return TEXT("FInstanceOriginBuffer"); }
	} InstanceOriginBuffer;
	FShaderResourceViewRHIRef InstanceOriginSRV;

	class FInstanceTransformBuffer : public FVertexBuffer
	{
		virtual FString GetFriendlyName() const override { return TEXT("FInstanceTransformBuffer"); }
	} InstanceTransformBuffer;
	FShaderResourceViewRHIRef InstanceTransformSRV;

	class FInstanceLightmapBuffer : public FVertexBuffer
	{
		virtual FString GetFriendlyName() const override { return TEXT("FInstanceLightmapBuffer"); }
	} InstanceLightmapBuffer;
	FShaderResourceViewRHIRef InstanceLightmapSRV;

	class FInstanceCustomDataBuffer : public FVertexBuffer
	{
		virtual FString GetFriendlyName() const override { return TEXT("FInstanceCustomDataBuffer"); }
	} InstanceCustomDataBuffer;
	FShaderResourceViewRHIRef InstanceCustomDataSRV;	

	/** Delete existing resources */
	void CleanUp();

	void CreateVertexBuffer(FResourceArrayInterface* InResourceArray, uint32 InUsage, uint32 InStride, uint8 InFormat, FVertexBufferRHIRef& OutVertexBufferRHI, FShaderResourceViewRHIRef& OutInstanceSRV);
	
	/**  */
	void UpdateFromCommandBuffer_RenderThread(FInstanceUpdateCmdBuffer& CmdBuffer);
};

/*-----------------------------------------------------------------------------
	FInstancedStaticMeshVertexFactory
-----------------------------------------------------------------------------*/

struct FInstancingUserData
{
	class FInstancedStaticMeshRenderData* RenderData;
	class FStaticMeshRenderData* MeshRenderData;

	int32 StartCullDistance;
	int32 EndCullDistance;

	int32 MinLOD;

	bool bRenderSelected;
	bool bRenderUnselected;
	FVector AverageInstancesScale;
	FVector InstancingOffset;
};

struct FInstancedStaticMeshDataType
{
	/** The stream to read the mesh transform from. */
	FVertexStreamComponent InstanceOriginComponent;

	/** The stream to read the mesh transform from. */
	FVertexStreamComponent InstanceTransformComponent[3];

	/** The stream to read the Lightmap Bias and Random instance ID from. */
	FVertexStreamComponent InstanceLightmapAndShadowMapUVBiasComponent;

	FRHIShaderResourceView* InstanceOriginSRV = nullptr;
	FRHIShaderResourceView* InstanceTransformSRV = nullptr;
	FRHIShaderResourceView* InstanceLightmapSRV = nullptr;
	FRHIShaderResourceView* InstanceCustomDataSRV = nullptr;

	int32 NumCustomDataFloats = 0;
};

/**
 * A vertex factory for instanced static meshes
 */
struct FInstancedStaticMeshVertexFactory : public FLocalVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(FInstancedStaticMeshVertexFactory);
public:
	FInstancedStaticMeshVertexFactory(ERHIFeatureLevel::Type InFeatureLevel)
		: FLocalVertexFactory(InFeatureLevel, "FInstancedStaticMeshVertexFactory")
	{
	}

	struct FDataType : public FInstancedStaticMeshDataType, public FLocalVertexFactory::FDataType
	{
	};

	/**
	 * Should we cache the material's shadertype on this platform with this vertex factory? 
	 */
	static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters);

	/**
	 * Modify compile environment to enable instancing
	 * @param OutEnvironment - shader compile environment to modify
	 */
	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	/**
	 * An implementation of the interface used by TSynchronizedResource to update the resource with new data from the game thread.
	 */
	void SetData(const FDataType& InData)
	{
		FLocalVertexFactory::Data = InData;
		Data = InData;
		UpdateRHI();
	}

	/**
	 * Copy the data from another vertex factory
	 * @param Other - factory to copy from
	 */
	void Copy(const FInstancedStaticMeshVertexFactory& Other);

	// FRenderResource interface.
	virtual void InitRHI() override;

	/** Make sure we account for changes in the signature of GetStaticBatchElementVisibility() */
	static CONSTEXPR uint32 NumBitsForVisibilityMask()
	{		
		return 8 * sizeof(uint64);
	}

#if ALLOW_DITHERED_LOD_FOR_INSTANCED_STATIC_MESHES
	virtual bool SupportsNullPixelShader() const override { return false; }
#endif

	inline FRHIShaderResourceView* GetInstanceOriginSRV() const
	{
		return Data.InstanceOriginSRV;
	}

	inline FRHIShaderResourceView* GetInstanceTransformSRV() const
	{
		return Data.InstanceTransformSRV;
	}

	inline FRHIShaderResourceView* GetInstanceLightmapSRV() const
	{
		return Data.InstanceLightmapSRV;
	}

	inline FRHIShaderResourceView* GetInstanceCustomDataSRV() const
	{
		return Data.InstanceCustomDataSRV;
	}

	FRHIUniformBuffer* GetUniformBuffer() const
	{
		return UniformBuffer.GetReference();
	}

private:
	FDataType Data;

	TUniformBufferRef<FInstancedStaticMeshVertexFactoryUniformShaderParameters> UniformBuffer;
};

class FInstancedStaticMeshVertexFactoryShaderParameters : public FLocalVertexFactoryShaderParametersBase
{
	DECLARE_TYPE_LAYOUT(FInstancedStaticMeshVertexFactoryShaderParameters, NonVirtual);
public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		FLocalVertexFactoryShaderParametersBase::Bind(ParameterMap);

		InstancingFadeOutParamsParameter.Bind(ParameterMap, TEXT("InstancingFadeOutParams"));
		InstancingViewZCompareZeroParameter.Bind(ParameterMap, TEXT("InstancingViewZCompareZero"));
		InstancingViewZCompareOneParameter.Bind(ParameterMap, TEXT("InstancingViewZCompareOne"));
		InstancingViewZConstantParameter.Bind(ParameterMap, TEXT("InstancingViewZConstant"));
		InstancingOffsetParameter.Bind(ParameterMap, TEXT("InstancingOffset"));
		InstancingWorldViewOriginZeroParameter.Bind(ParameterMap, TEXT("InstancingWorldViewOriginZero"));
		InstancingWorldViewOriginOneParameter.Bind(ParameterMap, TEXT("InstancingWorldViewOriginOne"));
		VertexFetch_InstanceOriginBufferParameter.Bind(ParameterMap, TEXT("VertexFetch_InstanceOriginBuffer"));
		VertexFetch_InstanceTransformBufferParameter.Bind(ParameterMap, TEXT("VertexFetch_InstanceTransformBuffer"));
		VertexFetch_InstanceLightmapBufferParameter.Bind(ParameterMap, TEXT("VertexFetch_InstanceLightmapBuffer"));
		InstanceOffset.Bind(ParameterMap, TEXT("InstanceOffset"));
	}

	void GetElementShaderBindings(
		const class FSceneInterface* Scene,
		const FSceneView* View,
		const FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams
		) const;

private:
	
	LAYOUT_FIELD(FShaderParameter, InstancingFadeOutParamsParameter)
	LAYOUT_FIELD(FShaderParameter, InstancingViewZCompareZeroParameter)
	LAYOUT_FIELD(FShaderParameter, InstancingViewZCompareOneParameter)
	LAYOUT_FIELD(FShaderParameter, InstancingViewZConstantParameter)
	LAYOUT_FIELD(FShaderParameter, InstancingOffsetParameter);
	LAYOUT_FIELD(FShaderParameter, InstancingWorldViewOriginZeroParameter)
	LAYOUT_FIELD(FShaderParameter, InstancingWorldViewOriginOneParameter)

	LAYOUT_FIELD(FShaderResourceParameter, VertexFetch_InstanceOriginBufferParameter)
	LAYOUT_FIELD(FShaderResourceParameter, VertexFetch_InstanceTransformBufferParameter)
	LAYOUT_FIELD(FShaderResourceParameter, VertexFetch_InstanceLightmapBufferParameter)
	LAYOUT_FIELD(FShaderParameter, InstanceOffset)
};

struct FInstanceUpdateCmdBuffer;
/*-----------------------------------------------------------------------------
	FPerInstanceRenderData
	Holds render data that can persist between scene proxy reconstruction
-----------------------------------------------------------------------------*/
struct FPerInstanceRenderData
{
	// Should be always constructed on main thread
	FPerInstanceRenderData(FStaticMeshInstanceData& Other, ERHIFeatureLevel::Type InFeaureLevel, bool InRequireCPUAccess);
	~FPerInstanceRenderData();

	/**
	 * Call to update the Instance buffer with pre allocated data without recreating the FPerInstanceRenderData
	 * @param InComponent - The owning component
	 * @param InOther - The Instance data to copy into our instance buffer
	 */
	ENGINE_API void UpdateFromPreallocatedData(FStaticMeshInstanceData& InOther);
		
	/**
	*/
	ENGINE_API void UpdateFromCommandBuffer(FInstanceUpdateCmdBuffer& CmdBuffer);

	/** Hit proxies for the instances */
	TArray<TRefCountPtr<HHitProxy>>		HitProxies;

	/** cached per-instance resource size*/
	SIZE_T								ResourceSize;

	/** Instance buffer */
	FStaticMeshInstanceBuffer			InstanceBuffer;
	TSharedPtr<FStaticMeshInstanceData, ESPMode::ThreadSafe> InstanceBuffer_GameThread;
};


/*-----------------------------------------------------------------------------
	FInstancedStaticMeshRenderData
-----------------------------------------------------------------------------*/

class ENGINE_API FInstancedStaticMeshRenderData
{
public:

	FInstancedStaticMeshRenderData(UInstancedStaticMeshComponent* InComponent, ERHIFeatureLevel::Type InFeatureLevel)
	  : Component(InComponent)
	  , PerInstanceRenderData(InComponent->PerInstanceRenderData)
	  , LODModels(Component->GetStaticMesh()->RenderData->LODResources)
	  , FeatureLevel(InFeatureLevel)
	{
		check(PerInstanceRenderData.IsValid());
		// Allocate the vertex factories for each LOD
		InitVertexFactories();
		RegisterSpeedTreeWind();
	}

	void ReleaseResources(FSceneInterface* Scene, const UStaticMesh* StaticMesh)
	{
		// unregister SpeedTree wind with the scene
		if (Scene && StaticMesh && StaticMesh->SpeedTreeWind.IsValid())
		{
			for (int32 LODIndex = 0; LODIndex < VertexFactories.Num(); LODIndex++)
			{
				Scene->RemoveSpeedTreeWind_RenderThread(&VertexFactories[LODIndex], StaticMesh);
			}
		}

		for (int32 LODIndex = 0; LODIndex < VertexFactories.Num(); LODIndex++)
		{
			VertexFactories[LODIndex].ReleaseResource();
		}
	}

	/** Source component */
	UInstancedStaticMeshComponent* Component;

	/** Per instance render data, could be shared with component */
	TSharedPtr<FPerInstanceRenderData, ESPMode::ThreadSafe> PerInstanceRenderData;

	/** Vertex factory */
	TIndirectArray<FInstancedStaticMeshVertexFactory> VertexFactories;

	/** LOD render data from the static mesh. */
	FStaticMeshLODResourcesArray& LODModels;

	/** Feature level used when creating instance data */
	ERHIFeatureLevel::Type FeatureLevel;

private:
	void InitVertexFactories();

	void RegisterSpeedTreeWind()
	{
		// register SpeedTree wind with the scene
		if (Component->GetStaticMesh()->SpeedTreeWind.IsValid())
		{
			for (int32 LODIndex = 0; LODIndex < LODModels.Num(); LODIndex++)
			{
				if (Component->GetScene())
				{
					Component->GetScene()->AddSpeedTreeWind(&VertexFactories[LODIndex], Component->GetStaticMesh());
				}
			}
		}
	}
};


/*-----------------------------------------------------------------------------
	FInstancedStaticMeshSceneProxy
-----------------------------------------------------------------------------*/

class FInstancedStaticMeshSceneProxy : public FStaticMeshSceneProxy
{
public:
	SIZE_T GetTypeHash() const override;

	FInstancedStaticMeshSceneProxy(UInstancedStaticMeshComponent* InComponent, ERHIFeatureLevel::Type InFeatureLevel)
	:	FStaticMeshSceneProxy(InComponent, true)
	,	StaticMesh(InComponent->GetStaticMesh())
	,	InstancedRenderData(InComponent, InFeatureLevel)
#if WITH_EDITOR
	,	bHasSelectedInstances(InComponent->SelectedInstances.Num() > 0)
#endif
	{
		bVFRequiresPrimitiveUniformBuffer = true;
		SetupProxy(InComponent);

#if RHI_RAYTRACING
		SetupRayTracingCullClusters();
#endif
	}

	~FInstancedStaticMeshSceneProxy()
	{
	}

	// FPrimitiveSceneProxy interface.

	virtual void DestroyRenderThreadResources() override;

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		FPrimitiveViewRelevance Result;
		if(View->Family->EngineShowFlags.InstancedStaticMeshes)
		{
			Result = FStaticMeshSceneProxy::GetViewRelevance(View);
#if WITH_EDITOR
			// use dynamic path to render selected indices
			if( bHasSelectedInstances )
			{
				Result.bDynamicRelevance = true;
			}
#endif
		}
		return Result;
	}

#if RHI_RAYTRACING
	virtual bool IsRayTracingStaticRelevant() const override
	{
		return false;
	}

	virtual void GetDynamicRayTracingInstances(struct FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances) final override;
	void SetupRayTracingCullClusters();

#endif

	virtual void GetLightRelevance(const FLightSceneProxy* LightSceneProxy, bool& bDynamic, bool& bRelevant, bool& bLightMapped, bool& bShadowMapped) const override;
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;

	virtual int32 GetNumMeshBatches() const override
	{
		return 1;
	}

	/** Sets up a shadow FMeshBatch for a specific LOD. */
	virtual bool GetShadowMeshElement(int32 LODIndex, int32 BatchIndex, uint8 InDepthPriorityGroup, FMeshBatch& OutMeshBatch, bool bDitheredLODTransition) const override;

	/** Sets up a FMeshBatch for a specific LOD and element. */
	virtual bool GetMeshElement(int32 LODIndex, int32 BatchIndex, int32 ElementIndex, uint8 InDepthPriorityGroup, bool bUseSelectionOutline, bool bAllowPreCulledIndices, FMeshBatch& OutMeshBatch, bool bSecondaryMeshBatch = false) const override;

	/** Sets up a wireframe FMeshBatch for a specific LOD. */
	virtual bool GetWireframeMeshElement(int32 LODIndex, int32 BatchIndex, const FMaterialRenderProxy* WireframeRenderProxy, uint8 InDepthPriorityGroup, bool bAllowPreCulledIndices, FMeshBatch& OutMeshBatch) const override;

	virtual void GetDistancefieldAtlasData(FBox& LocalVolumeBounds, FVector2D& OutDistanceMinMax, FIntVector& OutBlockMin, FIntVector& OutBlockSize, bool& bOutBuiltAsIfTwoSided, bool& bMeshWasPlane, float& SelfShadowBias, TArray<FMatrix>& ObjectLocalToWorldTransforms, bool& bOutThrottled) const override;

	virtual void GetDistanceFieldInstanceInfo(int32& NumInstances, float& BoundsSurfaceArea) const override;

	virtual int32 CollectOccluderElements(FOccluderElementsCollector& Collector) const override;

	/**
	 * Creates the hit proxies are used when DrawDynamicElements is called.
	 * Called in the game thread.
	 * @param OutHitProxies - Hit proxes which are created should be added to this array.
	 * @return The hit proxy to use by default for elements drawn by DrawDynamicElements.
	 */
	virtual HHitProxy* CreateHitProxies(UPrimitiveComponent* Component,TArray<TRefCountPtr<HHitProxy> >& OutHitProxies) override;

	virtual bool IsDetailMesh() const override
	{
		return true;
	}

protected:
	/** Cache of the StaticMesh asset, needed to release SpeedTree resources*/
	UStaticMesh* StaticMesh;

	/** Per component render data */
	FInstancedStaticMeshRenderData InstancedRenderData;

#if WITH_EDITOR
	/* If we we have any selected instances */
	bool bHasSelectedInstances;
#else
	static const bool bHasSelectedInstances = false;
#endif

	/** LOD transition info. */
	FInstancingUserData UserData_AllInstances;
	FInstancingUserData UserData_SelectedInstances;
	FInstancingUserData UserData_DeselectedInstances;

#if RHI_RAYTRACING
	
	/* Precomputed bounding spheres for culling */
	struct FCullNode
	{
		FVector Center;
		float Radius;
		uint32 Instance;
	};

	struct FRayTracingCullCluster
	{
		FVector BoundsMin;
		FVector BoundsMax;
		TArray<FCullNode> Nodes;
	};

	TArray<FRayTracingCullCluster> RayTracingCullClusters;
#endif

	/** Common path for the Get*MeshElement functions */
	void SetupInstancedMeshBatch(int32 LODIndex, int32 BatchIndex, FMeshBatch& OutMeshBatch) const;

private:

	void SetupProxy(UInstancedStaticMeshComponent* InComponent);
};

#if WITH_EDITOR
/*-----------------------------------------------------------------------------
	FInstancedStaticMeshStaticLightingMesh
-----------------------------------------------------------------------------*/

/**
 * A static lighting mesh class that transforms the points by the per-instance transform of an 
 * InstancedStaticMeshComponent
 */
class FStaticLightingMesh_InstancedStaticMesh : public FStaticMeshStaticLightingMesh
{
public:

	/** Initialization constructor. */
	FStaticLightingMesh_InstancedStaticMesh(const UInstancedStaticMeshComponent* InPrimitive, int32 LODIndex, int32 InstanceIndex, const TArray<ULightComponent*>& InRelevantLights)
		: FStaticMeshStaticLightingMesh(InPrimitive, LODIndex, InRelevantLights)
	{
		// override the local to world to combine the per instance transform with the component's standard transform
		SetLocalToWorld(InPrimitive->PerInstanceSMData[InstanceIndex].Transform * InPrimitive->GetComponentTransform().ToMatrixWithScale());
	}
};

/*-----------------------------------------------------------------------------
	FInstancedStaticMeshStaticLightingTextureMapping
-----------------------------------------------------------------------------*/


/** Represents a static mesh primitive with texture mapped static lighting. */
class FStaticLightingTextureMapping_InstancedStaticMesh : public FStaticMeshStaticLightingTextureMapping
{
public:
	/** Initialization constructor. */
	FStaticLightingTextureMapping_InstancedStaticMesh(UInstancedStaticMeshComponent* InPrimitive, int32 LODIndex, int32 InInstanceIndex, FStaticLightingMesh* InMesh, int32 InSizeX, int32 InSizeY, int32 InTextureCoordinateIndex, bool bPerformFullQualityRebuild)
		: FStaticMeshStaticLightingTextureMapping(InPrimitive, LODIndex, InMesh, InSizeX, InSizeY, InTextureCoordinateIndex, bPerformFullQualityRebuild)
		, InstanceIndex(InInstanceIndex)
		, QuantizedData(nullptr)
		, ShadowMapData()
		, bComplete(false)
	{
	}

	// FStaticLightingTextureMapping interface
	virtual void Apply(FQuantizedLightmapData* InQuantizedData, const TMap<ULightComponent*, FShadowMapData2D*>& InShadowMapData, ULevel* LightingScenario) override
	{
		check(bComplete == false);

		UInstancedStaticMeshComponent* InstancedComponent = Cast<UInstancedStaticMeshComponent>(Primitive.Get());

		if (InstancedComponent)
		{
			// Save the static lighting until all of the component's static lighting has been built.
			QuantizedData = TUniquePtr<FQuantizedLightmapData>(InQuantizedData);
			ShadowMapData.Empty(InShadowMapData.Num());
			for (auto& ShadowDataPair : InShadowMapData)
			{
				ShadowMapData.Add(ShadowDataPair.Key, TUniquePtr<FShadowMapData2D>(ShadowDataPair.Value));
			}

			InstancedComponent->ApplyLightMapping(this, LightingScenario);
		}

		bComplete = true;
	}

	virtual bool DebugThisMapping() const override
	{
		return false;
	}

	virtual FString GetDescription() const override
	{
		return FString(TEXT("InstancedSMLightingMapping"));
	}

private:
	friend class UInstancedStaticMeshComponent;

	/** The instance of the primitive this mapping represents. */
	const int32 InstanceIndex;

	// Light/shadow map data stored until all instances for this component are processed
	// so we can apply them all into one light/shadowmap
	TUniquePtr<FQuantizedLightmapData> QuantizedData;
	TMap<ULightComponent*, TUniquePtr<FShadowMapData2D>> ShadowMapData;

	/** Has this mapping already been completed? */
	bool bComplete;
};

#endif

/**
 * Structure that maps a component to it's lighting/instancing specific data which must be the same
 * between all instances that are bound to that component.
 */
struct FComponentInstanceSharingData
{
	/** The component that is associated (owns) this data */
	UInstancedStaticMeshComponent* Component;

	/** Light map texture */
	UTexture* LightMapTexture;

	/** Shadow map texture (or NULL if no shadow map) */
	UTexture* ShadowMapTexture;


	FComponentInstanceSharingData()
		: Component( NULL ),
		  LightMapTexture( NULL ),
		  ShadowMapTexture( NULL )
	{
	}
};


/**
 * Helper struct to hold information about what components use what lightmap textures
 */
struct FComponentInstancedLightmapData
{
	/** List of all original components and their original instances containing */
	TMap<UInstancedStaticMeshComponent*, TArray<FInstancedStaticMeshInstanceData> > ComponentInstances;

	/** List of new components */
	TArray< FComponentInstanceSharingData > SharingData;
};
