// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraDataInterfaceMeshCommon.h"
#include "WeightedRandomSampler.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Array.h"
#include "NiagaraParameterStore.h"
#include "NiagaraDataInterfaceSkeletalMesh.generated.h"

class UNiagaraDataInterfaceSkeletalMesh;
class USkeletalMesh;
struct FSkeletalMeshSkinningData;
struct FNDISkeletalMesh_InstanceData;
class FSkinWeightVertexBuffer;
struct FSkeletalMeshSamplingRegion;
struct FSkeletalMeshSamplingRegionLODBuiltData;
struct FSkeletalMeshAccessorHelper;

//////////////////////////////////////////////////////////////////////////

struct FSkeletalMeshSkinningDataUsage
{
	FSkeletalMeshSkinningDataUsage()
		: LODIndex(INDEX_NONE)
		, bUsesBoneMatrices(false)
		, bUsesPreSkinnedVerts(false)
		, bNeedDataImmediately(false)
	{}

	FSkeletalMeshSkinningDataUsage(int32 InLODIndex, bool bInUsesBoneMatrices, bool bInUsesPreSkinnedVerts, bool bInNeedDataImmediately)
		: LODIndex(InLODIndex)
		, bUsesBoneMatrices(bInUsesBoneMatrices)
		, bUsesPreSkinnedVerts(bInUsesPreSkinnedVerts)
		, bNeedDataImmediately(bInNeedDataImmediately)
	{}

	FORCEINLINE bool NeedBoneMatrices()const { return bUsesBoneMatrices || bUsesPreSkinnedVerts; }
	FORCEINLINE bool NeedPreSkinnedVerts()const { return bUsesPreSkinnedVerts; }
	FORCEINLINE bool NeedsDataImmediately()const { return bNeedDataImmediately; }
	FORCEINLINE int32 GetLODIndex()const { return LODIndex; }
private:
	int32 LODIndex;
	uint32 bUsesBoneMatrices : 1;
	uint32 bUsesPreSkinnedVerts : 1;
	/** Some users need valid data immediately after the register call rather than being able to wait until the next tick. */
	uint32 bNeedDataImmediately : 1;
};

struct FSkeletalMeshSkinningDataHandle
{
	FSkeletalMeshSkinningDataHandle();
	FSkeletalMeshSkinningDataHandle(FSkeletalMeshSkinningDataUsage InUsage, TSharedPtr<struct FSkeletalMeshSkinningData> InSkinningData);
	FSkeletalMeshSkinningDataHandle(const FSkeletalMeshSkinningDataHandle& Other) = delete;
	FSkeletalMeshSkinningDataHandle(FSkeletalMeshSkinningDataHandle&& Other);
	~FSkeletalMeshSkinningDataHandle();

	FSkeletalMeshSkinningDataHandle& operator=(const FSkeletalMeshSkinningDataHandle& Other) = delete;
	FSkeletalMeshSkinningDataHandle& operator=(FSkeletalMeshSkinningDataHandle&& Other);

	FSkeletalMeshSkinningDataUsage Usage;
	TSharedPtr<FSkeletalMeshSkinningData> SkinningData;
};

struct FSkeletalMeshSkinningData
{
	FSkeletalMeshSkinningData(TWeakObjectPtr<USkeletalMeshComponent> InMeshComp)
		: MeshComp(InMeshComp)
		, DeltaSeconds(.0333f)
		, CurrIndex(0)
		, BoneMatrixUsers(0)
		, bForceDataRefresh(false)
	{}

	void RegisterUser(FSkeletalMeshSkinningDataUsage Usage);
	void UnregisterUser(FSkeletalMeshSkinningDataUsage Usage);
	bool IsUsed()const;
	void ForceDataRefresh();

	bool Tick(float InDeltaSeconds, bool bRequirePreskin = true);

	FORCEINLINE void EnterRead()
	{
		RWGuard.ReadLock();
	}

	FORCEINLINE void ExitRead()
	{
		RWGuard.ReadUnlock();
	}

	FORCEINLINE int32 GetBoneCount(bool RequiresPrevious) const
	{
		int32 BoneCount = CurrComponentTransforms().Num();
		if (RequiresPrevious)
		{
			BoneCount = FMath::Min(BoneCount, PrevComponentTransforms().Num());
		}

		return BoneCount;
	}

	FORCEINLINE FVector GetPosition(int32 LODIndex, int32 VertexIndex) const
	{
		return LODData[LODIndex].SkinnedCPUPositions[CurrIndex][VertexIndex];
	}

	FORCEINLINE FVector GetPreviousPosition(int32 LODIndex, int32 VertexIndex) const
	{
		return LODData[LODIndex].SkinnedCPUPositions[CurrIndex ^ 1][VertexIndex];
	}

	FORCEINLINE void GetTangentBasis(int32 LODIndex, int32 VertexIndex, FVector& OutTangentX, FVector& OutTangentZ)
	{
		OutTangentX = LODData[LODIndex].SkinnedTangentBasis[(VertexIndex * 2) + 0];
		OutTangentZ = LODData[LODIndex].SkinnedTangentBasis[(VertexIndex * 2) + 1];
	}

	FORCEINLINE TArray<FVector>& CurrSkinnedPositions(int32 LODIndex)
	{
		return LODData[LODIndex].SkinnedCPUPositions[CurrIndex];
	}

	FORCEINLINE TArray<FVector>& PrevSkinnedPositions(int32 LODIndex)
	{
		return LODData[LODIndex].SkinnedCPUPositions[CurrIndex ^ 1];
	}

	FORCEINLINE TArray<FVector>& CurrSkinnedTangentBasis(int32 LODIndex)
	{
		return LODData[LODIndex].SkinnedTangentBasis;
	}

	FORCEINLINE TArray<FMatrix>& CurrBoneRefToLocals()
	{
		return BoneRefToLocals[CurrIndex];
	}

	FORCEINLINE const TArray<FMatrix>& CurrBoneRefToLocals() const
	{
		return BoneRefToLocals[CurrIndex];
	}

	FORCEINLINE TArray<FMatrix>& PrevBoneRefToLocals()
	{
		return BoneRefToLocals[CurrIndex ^ 1];
	}

	FORCEINLINE const TArray<FMatrix>& PrevBoneRefToLocals() const
	{
		return BoneRefToLocals[CurrIndex ^ 1];
	}

	FORCEINLINE TArray<FTransform>& CurrComponentTransforms()
	{
		return ComponentTransforms[CurrIndex];
	}

	FORCEINLINE const TArray<FTransform>& CurrComponentTransforms() const
	{
		return ComponentTransforms[CurrIndex];
	}

	FORCEINLINE TArray<FTransform>& PrevComponentTransforms()
	{
		return ComponentTransforms[CurrIndex ^ 1];
	}

	FORCEINLINE const TArray<FTransform>& PrevComponentTransforms() const
	{
		return ComponentTransforms[CurrIndex ^ 1];
	}

private:

	void UpdateBoneTransforms();

	FRWLock RWGuard;
	
	TWeakObjectPtr<USkeletalMeshComponent> MeshComp;

	/** Delta seconds between calculations of the previous and current skinned positions. */
	float DeltaSeconds;

	/** Index of the current frames skinned positions and bone matrices. */
	int32 CurrIndex;

	/** Number of users for cached bone matrices. */
	volatile int32 BoneMatrixUsers;

	/** Cached bone matrices. */
	TArray<FMatrix> BoneRefToLocals[2];

	/** Component space transforms */
	TArray<FTransform> ComponentTransforms[2];

	struct FLODData
	{
		FLODData() : PreSkinnedVertsUsers(0) { }

		/** Number of users for pre skinned verts. */
		volatile int32 PreSkinnedVertsUsers;

		/** CPU Skinned vertex positions. Double buffered to allow accurate velocity calculation. */
		TArray<FVector> SkinnedCPUPositions[2];

		/** CPU Skinned tangent basis, where each vertex will map to TangentX + TangentZ */
		TArray<FVector> SkinnedTangentBasis;
	};
	TArray<FLODData> LODData;

	bool bForceDataRefresh;
};

class FNDI_SkeletalMesh_GeneratedData
{
	// Encapsulates skinning data and mesh usage information. 
	// Set by GetCachedSkinningData and used by TickGeneratedData to determine whether we need to pre-skin or not.
	struct CachedSkinningDataAndUsage
	{
		bool bHasTicked = false;
		TSharedPtr<FSkeletalMeshSkinningData> SkinningData;
		FSkeletalMeshSkinningDataUsage Usage;
	};

	FRWLock CachedSkinningDataGuard;
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, CachedSkinningDataAndUsage> CachedSkinningData;

public:
	FSkeletalMeshSkinningDataHandle GetCachedSkinningData(TWeakObjectPtr<USkeletalMeshComponent>& InComponent, FSkeletalMeshSkinningDataUsage Usage);
	void TickGeneratedData(ETickingGroup TickGroup, float DeltaSeconds);
};

//////////////////////////////////////////////////////////////////////////

UENUM()
enum class ENDISkeletalMesh_SkinningMode : uint8
{
	Invalid = (uint8)-1 UMETA(Hidden),

	/** No skinning. */
	None = 0,
	/** Skin vertex locations as you need them. Use if you have a high poly mesh or you are sampling the interface a small number of times. */
	SkinOnTheFly,
	/**
	Pre skins the whole mesh. Makes access to location data on the mesh much faster but incurs a significant initial cost in cpu time and memory to skin the mesh.
	Cost is proportional to vertex count in the mesh.
	Use if you are sampling skinned data from the mesh many times and are able to provide a low poly LOD to sample from.
	*/
	PreSkin,
};

enum class ENDISkeletalMesh_FilterMode : uint8
{
	/** No filtering, use all triangles. */
	None,
	/** Filtered to a single region. */
	SingleRegion,
	/** Filtered to multiple regions. */
	MultiRegion,
};

enum class ENDISkelMesh_AreaWeightingMode : uint8
{
	None,
	AreaWeighted,
};

/** Allows perfect area weighted sampling between different skeletal mesh Sampling regions. */
struct FSkeletalMeshSamplingRegionAreaWeightedSampler : FWeightedRandomSampler
{
	FSkeletalMeshSamplingRegionAreaWeightedSampler();
	void Init(FNDISkeletalMesh_InstanceData* InOwner);
	virtual float GetWeights(TArray<float>& OutWeights)override;

	FORCEINLINE bool IsValid() { return TotalWeight > 0.0f; }

	int32 GetEntries() const { return Alias.Num(); }
protected:
	FNDISkeletalMesh_InstanceData* Owner;
};

/** 
 * This contains static data created once from the DI.
 * This should be in a proxy create by GT and accessible on RT. 
 * Right now we cannot follow a real Proxy pattern since Niagara does not prevent unloading of UI while RT data is still in use. 
 * See https://jira.it.epicgames.net/browse/UE-69336
 */
class FSkeletalMeshGpuSpawnStaticBuffers : public FRenderResource
{
public:

	virtual ~FSkeletalMeshGpuSpawnStaticBuffers();

	FORCEINLINE_DEBUGGABLE void Initialise(struct FNDISkeletalMesh_InstanceData* InstData, const FSkeletalMeshLODRenderData& SkeletalMeshLODRenderData,const FSkeletalMeshSamplingLODBuiltData& SkeletalMeshSamplingLODBuiltData);

	virtual void InitRHI() override;
	virtual void ReleaseRHI() override;

	virtual FString GetFriendlyName() const override { return TEXT("FSkeletalMeshGpuSpawnStaticBuffers"); }

	FShaderResourceViewRHIRef GetBufferTriangleUniformSamplerProbaSRV() const { return BufferTriangleUniformSamplerProbaSRV; }
	FShaderResourceViewRHIRef GetBufferTriangleUniformSamplerAliasSRV() const { return BufferTriangleUniformSamplerAliasSRV; }
	FShaderResourceViewRHIRef GetBufferTriangleMatricesOffsetSRV() const { return BufferTriangleMatricesOffsetSRV; }
	uint32 GetTriangleCount() const { return TriangleCount; }
	uint32 GetVertexCount() const { return VertexCount; }

	bool IsSamplingRegionsAllAreaWeighted() const { return bSamplingRegionsAllAreaWeighted; }
	int32 GetNumSamplingRegionTriangles() const { return NumSamplingRegionTriangles; }
	int32 GetNumSamplingRegionVertices() const { return NumSamplingRegionVertices; }
	FShaderResourceViewRHIRef GetSampleRegionsProbSRV() const { return SampleRegionsProbSRV; }
	FShaderResourceViewRHIRef GetSampleRegionsAliasSRV() const { return SampleRegionsAliasSRV; }
	FShaderResourceViewRHIRef GetSampleRegionsTriangleIndicesSRV() const { return SampleRegionsTriangleIndicesSRV; }
	FShaderResourceViewRHIRef GetSampleRegionsVerticesSRV() const { return SampleRegionsVerticesSRV; }

	FRHIShaderResourceView* GetBufferPositionSRV() const { return MeshVertexBufferSrv; }
	FRHIShaderResourceView* GetBufferIndexSRV() const { return MeshIndexBufferSrv; }
	FRHIShaderResourceView* GetBufferTangentSRV() const { return MeshTangentBufferSRV; }
	FRHIShaderResourceView* GetBufferTexCoordSRV() const { return MeshTexCoordBufferSrv; }
	FRHIShaderResourceView* GetBufferColorSRV() const { return MeshColorBufferSrv; }

	uint32 GetNumTexCoord() const { return NumTexCoord; }
	uint32 GetNumWeights() const { return NumWeights; }

	int32 GetNumFilteredBones() const { return NumFilteredBones; }
	int32 GetNumUnfilteredBones() const { return NumUnfilteredBones;  }
	int32 GetExcludedBoneIndex() const { return ExcludedBoneIndex; }
	FRHIShaderResourceView* GetFilteredAndUnfilteredBonesSRV() const { return FilteredAndUnfilteredBonesSRV; }

	int32 GetNumFilteredSockets() const { return NumFilteredSockets; }
	int32 GetFilteredSocketBoneOffset() const { return FilteredSocketBoneOffset; }

protected:

	FVertexBufferRHIRef BufferTriangleUniformSamplerProbaRHI = nullptr;
	FShaderResourceViewRHIRef BufferTriangleUniformSamplerProbaSRV = nullptr;
	FVertexBufferRHIRef BufferTriangleUniformSamplerAliasRHI = nullptr;
	FShaderResourceViewRHIRef BufferTriangleUniformSamplerAliasSRV = nullptr;
	FVertexBufferRHIRef BufferTriangleMatricesOffsetRHI = nullptr;
	FShaderResourceViewRHIRef BufferTriangleMatricesOffsetSRV = nullptr;

	bool bSamplingRegionsAllAreaWeighted = false;
	int32 NumSamplingRegionTriangles = 0;
	int32 NumSamplingRegionVertices = 0;
	TResourceArray<float> SampleRegionsProb;
	TResourceArray<int32> SampleRegionsAlias;
	TResourceArray<int32> SampleRegionsTriangleIndicies;
	TResourceArray<int32> SampleRegionsVertices;

	FVertexBufferRHIRef SampleRegionsProbBuffer;
	FShaderResourceViewRHIRef SampleRegionsProbSRV;
	FVertexBufferRHIRef SampleRegionsAliasBuffer;
	FShaderResourceViewRHIRef SampleRegionsAliasSRV;
	FVertexBufferRHIRef SampleRegionsTriangleIndicesBuffer;
	FShaderResourceViewRHIRef SampleRegionsTriangleIndicesSRV;
	FVertexBufferRHIRef SampleRegionsVerticesBuffer;
	FShaderResourceViewRHIRef SampleRegionsVerticesSRV;

	int32 NumFilteredBones = 0;
	int32 NumUnfilteredBones = 0;
	int32 ExcludedBoneIndex = INDEX_NONE;
	TResourceArray<uint16> FilteredAndUnfilteredBonesArray;
	FVertexBufferRHIRef FilteredAndUnfilteredBonesBuffer;
	FShaderResourceViewRHIRef FilteredAndUnfilteredBonesSRV;

	int32 NumFilteredSockets = 0;
	int32 FilteredSocketBoneOffset = 0;

	/** Cached SRV to gpu buffers of the mesh we spawn from */
	FRHIShaderResourceView* MeshVertexBufferSrv;
	FRHIShaderResourceView* MeshIndexBufferSrv;
	FRHIShaderResourceView* MeshTangentBufferSRV;
	FRHIShaderResourceView* MeshTexCoordBufferSrv;
	FRHIShaderResourceView* MeshColorBufferSrv;

	uint32 NumTexCoord = 0;
	uint32 NumWeights = 0;

	// Cached data for resource creation on RenderThread
	const FSkeletalMeshLODRenderData* LODRenderData = nullptr;
	const FSkeletalMeshSamplingLODBuiltData* SkeletalMeshSamplingLODBuiltData = nullptr;
	uint32 TriangleCount = 0;
	uint32 VertexCount = 0;
	uint32 InputWeightStride = 0;
	bool bUseGpuUniformlyDistributedSampling = false;
};

/**
 * This contains dynamic data created per frame from the DI.
 * This should be in a proxy create by GT and accessible on RT. Right now we cannot follow a real Proxy pattern since Niagara does not prevent unloading of UI while RT data is still in use.
 * See https://jira.it.epicgames.net/browse/UE-69336
 */
class FSkeletalMeshGpuDynamicBufferProxy : public FRenderResource
{
public:

	FSkeletalMeshGpuDynamicBufferProxy();
	virtual ~FSkeletalMeshGpuDynamicBufferProxy();

	void Initialise(const FReferenceSkeleton& RefSkel, const FSkeletalMeshLODRenderData& SkeletalMeshLODRenderData, uint32 InSamplingSocketCount);

	virtual void InitRHI() override;
	virtual void ReleaseRHI() override;

	void NewFrame(const FNDISkeletalMesh_InstanceData* InstanceData, int32 LODIndex);

	bool DoesBoneDataExist() const { return bBoneGpuBufferValid;}

	int32 GetNumBones() const { return (int32)SamplingBoneCount;  }

	/** Encapsulates a GPU read / CPU write buffer for bone data */
	struct FSkeletalBuffer
	{
		FVertexBufferRHIRef SectionBuffer;
		FShaderResourceViewRHIRef SectionSRV;

		FVertexBufferRHIRef SamplingBuffer;
		FShaderResourceViewRHIRef SamplingSRV;
	};

	FSkeletalBuffer& GetRWBufferBone() { return RWBufferBones[CurrentBoneBufferId % 2]; }
	FSkeletalBuffer& GetRWBufferPrevBone() { return bPrevBoneGpuBufferValid ? RWBufferBones[(CurrentBoneBufferId + 1) % 2] : GetRWBufferBone(); }

private:
	uint32 SamplingBoneCount = 0;
	uint32 SamplingSocketCount = 0;
	uint32 SectionBoneCount = 0;

	enum { BufferBoneCount = 2 };
	FSkeletalBuffer RWBufferBones[BufferBoneCount];
	uint8 CurrentBoneBufferId = 0;

	bool bBoneGpuBufferValid = false;
	bool bPrevBoneGpuBufferValid = false;
};

struct FNDISkeletalMesh_InstanceData
{
	//Cached ptr to component we sample from. TODO: This should not need to be a weak ptr. We should always be clearing out DIs when the component is destroyed.
	TWeakObjectPtr<USceneComponent> Component;

	/** A binding to the user ptr we're reading the mesh from (if we are). */
	FNiagaraParameterDirectBinding<UObject*> UserParamBinding;

	//Always reset the DI when the attach parent changes.
	TWeakObjectPtr<USceneComponent> CachedAttachParent;

	UObject* CachedUserParam;

	USkeletalMesh* Mesh;

	TWeakObjectPtr<USkeletalMesh> MeshSafe;

	/** Handle to our skinning data. */
	FSkeletalMeshSkinningDataHandle SkinningData;
	
	/** Indices of all valid Sampling regions on the mesh to sample from. */
	TArray<int32> SamplingRegionIndices;
			
	/** Additional sampler for if we need to do area weighting sampling across multiple area weighted regions. */
	FSkeletalMeshSamplingRegionAreaWeightedSampler SamplingRegionAreaWeightedSampler;

	//Cached ComponentToWorld.
	FMatrix Transform;
	//InverseTranspose of above for transforming normals/tangents.
	FMatrix TransformInverseTransposed;

	//Cached ComponentToWorld from previous tick.
	FMatrix PrevTransform;

	/** Time separating Transform and PrevTransform. */
	float DeltaSeconds;

	/* Excluded bone for some specific functions, generally the root bone which you don't want to include when picking a random bone. */
	int32 ExcludedBoneIndex = INDEX_NONE;

	/* Number of filtered bones in the array. */
	int32 NumFilteredBones = 0;
	/* Number of unfiltered bones in the array. */
	int32 NumUnfilteredBones = 0;
	/** Indices of the bones filtered by the user followed by the unfiltered bones, if this array is empty no filtering is in effect. */
	TArray<uint16> FilteredAndUnfilteredBones;

	/** Name of all the sockets we use. */
	struct FCachedSocketInfo
	{
		FCachedSocketInfo() : BoneIdx(INDEX_NONE){}
		FTransform Transform;
		int32 BoneIdx;
	};
	TArray<FCachedSocketInfo> FilteredSocketInfo;

	/** Bone index of the first socket, sockets are appended to the end of the bone array */
	int32 FilteredSocketBoneOffset = 0;

	/** Index into which socket transforms to use.  */
	uint32 FilteredSocketTransformsIndex = 0;
	/** Transforms for sockets. */
	TStaticArray<TArray<FTransform>, 2> FilteredSocketTransforms;

	uint32 ChangeId;

	/** True if the mesh we're using allows area weighted sampling on GPU. */
	uint32 bIsGpuUniformlyDistributedSampling : 1;

	/** True if the mesh we're using is to be rendered in unlimited bone influences mode. */
	uint32 bUnlimitedBoneInfluences : 1;
	const FSkinWeightDataVertexBuffer* MeshSkinWeightBuffer;
	const FSkinWeightLookupVertexBuffer* MeshSkinWeightLookupBuffer;
	uint32 MeshWeightStrideByte;
	uint32 MeshSkinWeightIndexSizeByte;

	/** Extra mesh data upload to GPU.*/
	FSkeletalMeshGpuSpawnStaticBuffers* MeshGpuSpawnStaticBuffers;
	FSkeletalMeshGpuDynamicBufferProxy* MeshGpuSpawnDynamicBuffers;

	/** Flag to stub VM functions that rely on mesh data being accessible on the CPU */
	bool bAllowCPUMeshDataAccess;

	FORCEINLINE_DEBUGGABLE bool ResetRequired(UNiagaraDataInterfaceSkeletalMesh* Interface)const;

	bool Init(UNiagaraDataInterfaceSkeletalMesh* Interface, FNiagaraSystemInstance* SystemInstance);
	FORCEINLINE_DEBUGGABLE bool Tick(UNiagaraDataInterfaceSkeletalMesh* Interface, FNiagaraSystemInstance* SystemInstance, float InDeltaSeconds);
	FORCEINLINE_DEBUGGABLE void Release();
	FORCEINLINE int32 GetLODIndex()const { return SkinningData.Usage.GetLODIndex(); }

	FORCEINLINE_DEBUGGABLE FSkeletalMeshLODRenderData* GetLODRenderDataAndSkinWeights(FSkinWeightVertexBuffer*& OutSkinWeightBuffer)
	{
		FSkeletalMeshLODRenderData* Ret = nullptr;
		OutSkinWeightBuffer = nullptr;
		if (Mesh)
		{
			Ret = &Mesh->GetResourceForRendering()->LODRenderData[GetLODIndex()];
			if (bAllowCPUMeshDataAccess)
			{
				if (USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(Component.Get()))
				{
					OutSkinWeightBuffer = SkelComp->GetSkinWeightBuffer(GetLODIndex());
				}
				else
				{
					OutSkinWeightBuffer = &Ret->SkinWeightVertexBuffer; // Todo @sckime fix this when main comes in for real
				}
			}
		}
		return Ret;
	}

	FORCEINLINE_DEBUGGABLE FSkinWeightVertexBuffer* GetSkinWeights()
	{
		if (USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(Component.Get()))
		{
			return SkelComp->GetSkinWeightBuffer(GetLODIndex());
		}
		return &Mesh->GetResourceForRendering()->LODRenderData[GetLODIndex()].SkinWeightVertexBuffer;
	}

	void UpdateFilteredSocketTransforms();
	TArray<FTransform>& GetFilteredSocketsWriteBuffer() { return FilteredSocketTransforms[FilteredSocketTransformsIndex]; }
	const TArray<FTransform>& GetFilteredSocketsCurrBuffer() const { return FilteredSocketTransforms[FilteredSocketTransformsIndex]; }
	const TArray<FTransform>& GetFilteredSocketsPrevBuffer() const { return FilteredSocketTransforms[(FilteredSocketTransformsIndex + 1) % FilteredSocketTransforms.Num()]; }

	bool HasColorData();
};

/** Data Interface allowing sampling of skeletal meshes. */
UCLASS(EditInlineNew, Category = "Meshes", meta = (DisplayName = "Skeletal Mesh"))
class NIAGARA_API UNiagaraDataInterfaceSkeletalMesh : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:

	DECLARE_NIAGARA_DI_PARAMETER();
	
#if WITH_EDITORONLY_DATA
	/** Mesh used to sample from when not overridden by a source actor from the scene. Only available in editor for previewing. This is removed in cooked builds. */
	UPROPERTY(EditAnywhere, Category = "Mesh")
	USkeletalMesh* PreviewMesh;
#endif
	
	/** The source actor from which to sample. Takes precedence over the direct mesh. Note that this can only be set when used as a user variable on a component in the world.*/
	UPROPERTY(EditAnywhere, Category = "Mesh")
	AActor* Source;

	/** Reference to a user parameter if we're reading one. */
	UPROPERTY(EditAnywhere, Category = "Mesh")
	FNiagaraUserParameterBinding MeshUserParameter;
	
	/** The source component from which to sample. Takes precedence over the direct mesh. Not exposed to the user, only indirectly accessible from blueprints. */
	UPROPERTY(Transient)
	USkeletalMeshComponent* SourceComponent;

	UPROPERTY(EditAnywhere, Category="Mesh")
	ENDISkeletalMesh_SkinningMode SkinningMode;

	/** Sampling regions on the mesh from which to sample. Leave this empty to sample from the whole mesh. */
	UPROPERTY(EditAnywhere, Category="Mesh")
	TArray<FName> SamplingRegions;

	/** If no regions are specified, we'll sample the whole mesh at this LODIndex. -1 indicates to use the last LOD.*/
	UPROPERTY(EditAnywhere, Category="Mesh")
	int32 WholeMeshLOD;

	/** Set of filtered bones that can be used for sampling. Select from these with GetFilteredBoneAt and RandomFilteredBone. */
	UPROPERTY(EditAnywhere, Category = "Skeleton")
	TArray<FName> FilteredBones;

	/** Set of filtered sockets that can be used for sampling. Select from these with GetFilteredSocketAt and RandomFilteredSocket. */
	UPROPERTY(EditAnywhere, Category = "Skeleton")
	TArray<FName> FilteredSockets;

	/**
	Optionally remove a single bone from Random / Random Unfiltered access.
	You can still include this bone in filtered list and access using the direct index functionality.
	*/
	UPROPERTY(EditAnywhere, Category = "Skeleton", meta=(EditCondition="bExcludeBone"))
	FName ExcludeBoneName;

	UPROPERTY(EditAnywhere, Category = "Skeleton", meta = (InlineEditConditionToggle))
	uint8 bExcludeBone : 1;

	/** Cached change id off of the data interface.*/
	uint32 ChangeId;

	//~ UObject interface
	virtual void PostInitProperties()override; 
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ UObject interface END


	//~ UNiagaraDataInterface interface
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)override;
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual int32 PerInstanceDataSize()const override { return sizeof(FNDISkeletalMesh_InstanceData); }

	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)override;
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target)const override { return true; }
#if WITH_EDITOR	
	virtual void GetFeedback(UNiagaraSystem* Asset, UNiagaraComponent* Component, TArray<FNiagaraDataInterfaceError>& OutErrors,
		TArray<FNiagaraDataInterfaceFeedback>& Warnings, TArray<FNiagaraDataInterfaceFeedback>& Info) override;
	virtual void ValidateFunction(const FNiagaraFunctionSignature& Function, TArray<FText>& OutValidationErrors) override;
#endif
	virtual bool HasTickGroupPrereqs() const override { return true; }
	virtual ETickingGroup CalculateTickGroup(void* PerInstanceData) const override;
	virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const override;
	//~ UNiagaraDataInterface interface END

	USkeletalMesh* GetSkeletalMesh(class UNiagaraComponent* OwningComponent, TWeakObjectPtr<USceneComponent>& SceneComponent, USkeletalMeshComponent*& FoundSkelComp, FNDISkeletalMesh_InstanceData* InstData = nullptr);

	virtual void GetCommonHLSL(FString& OutHLSL) override;
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
#if WITH_EDITORONLY_DATA
	virtual bool UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature) override;
#endif

	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override;

	static const FString MeshIndexBufferName;
	static const FString MeshVertexBufferName;
	static const FString MeshSkinWeightBufferName;
	static const FString MeshSkinWeightLookupBufferName;
	static const FString MeshCurrBonesBufferName;
	static const FString MeshPrevBonesBufferName;
	static const FString MeshCurrSamplingBonesBufferName;
	static const FString MeshPrevSamplingBonesBufferName;
	static const FString MeshTangentBufferName;
	static const FString MeshTexCoordBufferName;
	static const FString MeshColorBufferName;
	static const FString MeshTriangleSamplerProbaBufferName;
	static const FString MeshTriangleSamplerAliasBufferName;
	static const FString MeshNumSamplingRegionTrianglesName;
	static const FString MeshNumSamplingRegionVerticesName;
	static const FString MeshSamplingRegionsProbaBufferName;
	static const FString MeshSamplingRegionsAliasBufferName;
	static const FString MeshSampleRegionsTriangleIndicesName;
	static const FString MeshSampleRegionsVerticesName;
	static const FString MeshTriangleMatricesOffsetBufferName;
	static const FString MeshTriangleCountName;
	static const FString MeshVertexCountName;
	static const FString MeshWeightStrideName;
	static const FString MeshSkinWeightIndexSizeName;
	static const FString MeshNumTexCoordName;
	static const FString MeshNumWeightsName;
	static const FString NumBonesName;
	static const FString NumFilteredBonesName;
	static const FString NumUnfilteredBonesName;
	static const FString RandomMaxBoneName;
	static const FString ExcludeBoneIndexName;
	static const FString FilteredAndUnfilteredBonesName;
	static const FString NumFilteredSocketsName;
	static const FString FilteredSocketBoneOffsetName;
	static const FString InstanceTransformName;
	static const FString InstancePrevTransformName;
	static const FString InstanceRotationName;
	static const FString InstancePrevRotationName;
	static const FString InstanceInvDeltaTimeName;
	static const FString EnabledFeaturesName;

protected:
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;

	//////////////////////////////////////////////////////////////////////////
	//Triangle sampling
	//Triangles are sampled a using MeshTriangleCoordinates which are composed of Triangle index and a bary centric coordinate on that triangle.
public:

	void GetTriangleSamplingFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions);
	void BindTriangleSamplingFunction(const FVMExternalFunctionBindingInfo& BindingInfo, FNDISkeletalMesh_InstanceData* InstData, FVMExternalFunction &OutFunc);

	template<typename FilterMode, typename AreaWeightingMode>
	void GetFilteredTriangleCount(FVectorVMContext& Context);

	template<typename FilterMode, typename AreaWeightingMode>
	void GetFilteredTriangleAt(FVectorVMContext& Context);

	template<typename FilterMode, typename AreaWeightingMode>
	void RandomTriCoord(FVectorVMContext& Context);

	template<typename FilterMode, typename AreaWeightingMode>
	void IsValidTriCoord(FVectorVMContext& Context);

	template<typename SkinningHandlerType, typename TransformHandlerType, typename VertexAccessorType, typename bInterpolated>
	void GetTriCoordSkinnedData(FVectorVMContext& Context);

	template<typename TransformHandlerType, typename bInterpolated>
	void GetTriCoordSkinnedDataFallback(FVectorVMContext& Context);

	void GetTriCoordColor(FVectorVMContext& Context);

	void GetTriCoordColorFallback(FVectorVMContext& Context);

	template<typename VertexAccessorType>
	void GetTriCoordUV(FVectorVMContext& Context);

	template<typename SkinningHandlerType>
	void GetTriCoordVertices(FVectorVMContext& Context);

private:
	template<typename FilterMode, typename AreaWeightingMode>
	FORCEINLINE int32 RandomTriIndex(FNDIRandomHelper& RandHelper, FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData, int32 InstanceIndex);

	void RandomTriangle(FVectorVMContext& Context);
	void GetTriangleCount(FVectorVMContext& Context);

	template<typename FilterMode, typename AreaWeightingMode>
	FORCEINLINE int32 GetFilteredTriangleCount(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData);

	template<typename FilterMode, typename AreaWeightingMode>
	FORCEINLINE int32 GetFilteredTriangleAt(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData, int32 FilteredIdx);
	//End of Mesh Sampling
	//////////////////////////////////////////////////////////////////////////

	//////////////////////////////////////////////////////////////////////////
	//Vertex Sampling
	//Vertex sampling done with direct vertex indices.
public:
	
	void GetVertexSamplingFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions);
	void BindVertexSamplingFunction(const FVMExternalFunctionBindingInfo& BindingInfo, FNDISkeletalMesh_InstanceData* InstData, FVMExternalFunction &OutFunc);

	void IsValidVertex(FVectorVMContext& Context);
	void RandomVertex(FVectorVMContext& Context);
	void GetVertexCount(FVectorVMContext& Context);

	template<typename FilterMode>
	void IsValidFilteredVertex(FVectorVMContext& Context);
	template<typename FilterMode>
	void RandomFilteredVertex(FVectorVMContext& Context);
	template<typename FilterMode>
	void GetFilteredVertexCount(FVectorVMContext& Context);
	template<typename FilterMode>
	void GetFilteredVertexAt(FVectorVMContext& Context);

	template<typename SkinningHandlerType, typename TransformHandlerType, typename VertexAccessorType>
	void GetVertexSkinnedData(FVectorVMContext& Context);

	void GetVertexColor(FVectorVMContext& Context);

	void GetVertexColorFallback(FVectorVMContext& Context);

	template<typename VertexAccessorType>
	void GetVertexUV(FVectorVMContext& Context);

private:
	template<typename FilterMode>
	FORCEINLINE int32 RandomFilteredVertIndex(FNDIRandomHelper& RandHelper, int32 Instance, FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData);

	template<typename FilterMode>
	FORCEINLINE int32 GetFilteredVertexCount(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData);

	template<typename FilterMode>
	FORCEINLINE int32 GetFilteredVertexAt(FSkeletalMeshAccessorHelper& Accessor, FNDISkeletalMesh_InstanceData* InstData, int32 FilteredIdx);

	//End of Vertex Sampling
	//////////////////////////////////////////////////////////////////////////

	//////////////////////////////////////////////////////////////////////////
	// Direct Bone + Socket Sampling

public:
	void GetSkeletonSamplingFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions);
	void BindSkeletonSamplingFunction(const FVMExternalFunctionBindingInfo& BindingInfo, FNDISkeletalMesh_InstanceData* InstData, FVMExternalFunction &OutFunc);

	template<typename SkinningHandlerType, typename TransformHandlerType, typename bInterpolated>
	void GetSkinnedBoneData(FVectorVMContext& Context);	
	template<typename TransformHandlerType, typename bInterpolated>
	void GetSkinnedBoneDataFallback(FVectorVMContext& Context);
	
	void IsValidBone(FVectorVMContext& Context);
	void RandomBone(FVectorVMContext& Context);
	void GetBoneCount(FVectorVMContext& Context);

	void GetFilteredBoneCount(FVectorVMContext& Context);
	void GetFilteredBoneAt(FVectorVMContext& Context);
	void RandomFilteredBone(FVectorVMContext& Context);

	void GetUnfilteredBoneCount(FVectorVMContext& Context);
	void GetUnfilteredBoneAt(FVectorVMContext& Context);
	void RandomUnfilteredBone(FVectorVMContext& Context);

	void GetFilteredSocketCount(FVectorVMContext& Context);
	void GetFilteredSocketBoneAt(FVectorVMContext& Context);
	void GetFilteredSocketTransform(FVectorVMContext& Context);
	void RandomFilteredSocket(FVectorVMContext& Context);
		
	void RandomFilteredSocketOrBone(FVectorVMContext& Context);
	void GetFilteredSocketOrBoneCount(FVectorVMContext& Context);
	void GetFilteredSocketOrBoneBoneAt(FVectorVMContext& Context);
	// End of Direct Bone + Socket Sampling
	//////////////////////////////////////////////////////////////////////////

	void SetSourceComponentFromBlueprints(USkeletalMeshComponent* ComponentToUse);
};


class FSkeletalMeshInterfaceHelper
{
public:
	// Triangle Sampling
	static const FName RandomTriCoordName;
	static const FName IsValidTriCoordName;
	static const FName GetSkinnedTriangleDataName;
	static const FName GetSkinnedTriangleDataWSName;
	static const FName GetSkinnedTriangleDataInterpName;
	static const FName GetSkinnedTriangleDataWSInterpName;
	static const FName GetTriColorName;
	static const FName GetTriUVName;
	static const FName GetTriCoordVerticesName;
	static const FName RandomTriangleName;
	static const FName GetTriangleCountName;
	static const FName RandomFilteredTriangleName;
	static const FName GetFilteredTriangleCountName;
	static const FName GetFilteredTriangleAtName;

	// Bone Sampling
	static const FName GetSkinnedBoneDataName;
	static const FName GetSkinnedBoneDataWSName;
	static const FName GetSkinnedBoneDataInterpolatedName;
	static const FName GetSkinnedBoneDataWSInterpolatedName;
	static const FName IsValidBoneName;
	static const FName RandomBoneName;
	static const FName GetBoneCountName;

	static const FName RandomFilteredBoneName;
	static const FName GetFilteredBoneCountName;
	static const FName GetFilteredBoneAtName;

	static const FName RandomUnfilteredBoneName;
	static const FName GetUnfilteredBoneCountName;
	static const FName GetUnfilteredBoneAtName;

	static const FName RandomFilteredSocketName;
	static const FName GetFilteredSocketCountName;
	static const FName GetFilteredSocketBoneAtName;
	static const FName GetFilteredSocketTransformName;

	static const FName RandomFilteredSocketOrBoneName;
	static const FName GetFilteredSocketOrBoneCountName;
	static const FName GetFilteredSocketOrBoneAtName;

	// Vertex Sampling
	static const FName GetSkinnedVertexDataName;
	static const FName GetSkinnedVertexDataWSName;
	static const FName GetVertexColorName;
	static const FName GetVertexUVName;

	static const FName IsValidVertexName;
	static const FName RandomVertexName;
	static const FName GetVertexCountName;

	static const FName IsValidFilteredVertexName;
	static const FName RandomFilteredVertexName;
	static const FName GetFilteredVertexCountName;
	static const FName GetFilteredVertexAtName;
};

struct FNiagaraDISkeletalMeshPassedDataToRT
{
	FSkeletalMeshGpuSpawnStaticBuffers* StaticBuffers;
	FSkeletalMeshGpuDynamicBufferProxy* DynamicBuffer;
	const FSkinWeightDataVertexBuffer* MeshSkinWeightBuffer;
	const FSkinWeightLookupVertexBuffer* MeshSkinWeightLookupBuffer;

	bool bIsGpuUniformlyDistributedSampling;

	bool bUnlimitedBoneInfluences;
	uint32 MeshWeightStrideByte;
	uint32 MeshSkinWeightIndexSizeByte;
	FMatrix Transform;
	FMatrix PrevTransform;
	float DeltaSeconds;
};

typedef FNiagaraDISkeletalMeshPassedDataToRT FNiagaraDataInterfaceProxySkeletalMeshData;

struct FNiagaraDataInterfaceProxySkeletalMesh : public FNiagaraDataInterfaceProxy
{
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override 
	{
		return sizeof(FNiagaraDISkeletalMeshPassedDataToRT);
	}

	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override;

	TMap<FNiagaraSystemInstanceID, FNiagaraDataInterfaceProxySkeletalMeshData> SystemInstancesToData;
};
