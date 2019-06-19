// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraDataInterfaceMeshCommon.h"
#include "StaticMeshResources.h"
#include "Engine/StaticMesh.h"
#include "NiagaraDataInterfaceStaticMesh.generated.h"

struct FNDIStaticMesh_InstanceData;
struct FNDIStaticMeshSectionFilter;

/** Allows uniform random sampling of a number of mesh sections filtered by an FNDIStaticMeshSectionFilter */
struct FStaticMeshFilteredAreaWeightedSectionSampler : FStaticMeshAreaWeightedSectionSampler
{
	FStaticMeshFilteredAreaWeightedSectionSampler();
	void Init(FStaticMeshLODResources* InRes, FNDIStaticMesh_InstanceData* InOwner);
	virtual float GetWeights(TArray<float>& OutWeights)override;

protected:
	FStaticMeshLODResources* Res;
	FNDIStaticMesh_InstanceData* Owner;
};

USTRUCT()
struct FNDIStaticMeshSectionFilter
{
	GENERATED_USTRUCT_BODY();

	/** Only allow sections these material slots. */
	UPROPERTY(EditAnywhere, Category="Section Filter")
	TArray<int32> AllowedMaterialSlots;

	//Others?
	//Banned material slots
	
	void Init(class UNiagaraDataInterfaceStaticMesh* Owner, bool bAreaWeighted);
	FORCEINLINE bool CanEverReject()const { return AllowedMaterialSlots.Num() > 0; }
};

class UNiagaraDataInterfaceStaticMesh;

/** Used to stored GPU data needed for an interface/mesh tuple (e.g. uniform sampling of sections according to mesh surface area). */
class FStaticMeshGpuSpawnBuffer : public FRenderResource
{
public:

	virtual ~FStaticMeshGpuSpawnBuffer();

	void Initialise(const FStaticMeshLODResources& Res, const UNiagaraDataInterfaceStaticMesh& Interface,
		bool bIsGpuUniformlyDistributedSampling, const TArray<int32>& ValidSection, const FStaticMeshFilteredAreaWeightedSectionSampler& SectionSampler);

	virtual void InitRHI() override;
	virtual void ReleaseRHI() override;

	virtual FString GetFriendlyName() const override { return TEXT("FStaticMeshGpuSpawnBuffer"); }

	FRHIShaderResourceView* GetBufferSectionSRV() const { return BufferSectionSRV; }
	uint32 GetValidSectionCount() const { return ValidSections.Num(); };

	FRHIShaderResourceView* GetBufferPositionSRV() const { return MeshVertexBufferSrv; }
	FRHIShaderResourceView* GetBufferTangentSRV() const { return MeshTangentBufferSrv; }
	FRHIShaderResourceView* GetBufferTexCoordSRV() const { return MeshTexCoordBufferSrv; }
	FRHIShaderResourceView* GetBufferIndexSRV() const { return MeshIndexBufferSrv; }
	FRHIShaderResourceView* GetBufferColorSRV() const { return MeshColorBufferSRV; }

	FRHIShaderResourceView* GetBufferUniformTriangleSamplingSRV() const { return BufferUniformTriangleSamplingSRV; }

	uint32 GetNumTexCoord() const { return NumTexCoord; }

protected:

	// We could separate probabilities from the triangle information when UE supports R32G32 buffer. For pack it all in a uint RGBA32 format.
	struct SectionInfo
	{
		uint32 FirstIndex;
		uint32 NumTriangles;
		float  Prob;
		uint32 Alias;
	};

	// Cached pointer to Section render data used for initialization only.
	const FStaticMeshLODResources* SectionRenderData = nullptr;

	TArray<SectionInfo> ValidSections;					// Only the section we want to spawn from

	FVertexBufferRHIRef BufferSectionRHI = nullptr;
	FShaderResourceViewRHIRef BufferSectionSRV = nullptr;

	FShaderResourceViewRHIRef BufferUniformTriangleSamplingSRV = nullptr;

	// Cached SRV to gpu buffers of the mesh we spawn from 
	FShaderResourceViewRHIRef MeshIndexBufferSrv;
	FShaderResourceViewRHIRef MeshVertexBufferSrv;
	FShaderResourceViewRHIRef MeshTangentBufferSrv;
	FShaderResourceViewRHIRef MeshTexCoordBufferSrv;
	uint32 NumTexCoord;
	FShaderResourceViewRHIRef MeshColorBufferSRV;
};

struct FNDIStaticMesh_InstanceData
{
	 //Cached ptr to component we sample from. 
	TWeakObjectPtr<USceneComponent> Component;

	//Cached ptr to actual mesh we sample from. 
	UStaticMesh* Mesh;

	//Cached ComponentToWorld.
	FMatrix Transform;
	//InverseTranspose of above for transforming normals/tangents.
	FMatrix TransformInverseTransposed;

	//Cached ComponentToWorld from previous tick.
	FMatrix PrevTransform;
	//InverseTranspose of above for transforming normals/tangents.
	FMatrix PrevTransformInverseTransposed;

	/** Time separating Transform and PrevTransform. */
	float DeltaSeconds;

	/** True if the mesh allows CPU access. Use to reset the instance in the editor*/
	uint32 bMeshAllowsCpuAccess : 1;
	/** True if the mesh we're using allows area weighted sampling on CPU. */
	uint32 bIsCpuUniformlyDistributedSampling : 1;
	/** True if the mesh we're using allows area weighted sampling on GPU. */
	uint32 bIsGpuUniformlyDistributedSampling : 1;

	/** Cached results of this filter being applied to the owning mesh. */
	TArray<int32> ValidSections;
	/** Area weighted sampler for the valid sections. */
	FStaticMeshFilteredAreaWeightedSectionSampler Sampler;

	/** Allows sampling of the mesh's tris based on a dynamic color range. */
	TSharedPtr<struct FDynamicVertexColorFilterData> DynamicVertexColorSampler;

	/** Cached change id off of the data interface.*/
	uint32 ChangeId;

	FORCEINLINE UStaticMesh* GetActualMesh()const { return Mesh; }
	FORCEINLINE bool UsesCpuUniformlyDistributedSampling() const { return bIsCpuUniformlyDistributedSampling; }
	FORCEINLINE bool MeshHasPositions()const { return Mesh && Mesh->RenderData->LODResources[0].VertexBuffers.PositionVertexBuffer.GetNumVertices() > 0; }
	FORCEINLINE bool MeshHasVerts()const { return Mesh && Mesh->RenderData->LODResources[0].VertexBuffers.StaticMeshVertexBuffer.GetNumVertices() > 0; }
	FORCEINLINE bool MeshHasColors()const { return Mesh && Mesh->RenderData->LODResources[0].VertexBuffers.ColorVertexBuffer.GetNumVertices() > 0; }

	FORCEINLINE_DEBUGGABLE bool ResetRequired(UNiagaraDataInterfaceStaticMesh* Interface)const;

	FORCEINLINE const TArray<int32>& GetValidSections()const { return ValidSections; }
	FORCEINLINE const FStaticMeshAreaWeightedSectionSampler& GetAreaWeigtedSampler()const { return Sampler; }

	void InitVertexColorFiltering();

	FORCEINLINE_DEBUGGABLE bool Init(UNiagaraDataInterfaceStaticMesh* Interface, FNiagaraSystemInstance* SystemInstance);
	FORCEINLINE_DEBUGGABLE bool Tick(UNiagaraDataInterfaceStaticMesh* Interface, FNiagaraSystemInstance* SystemInstance, float InDeltaSeconds);
	FORCEINLINE_DEBUGGABLE void Release();
};

/** Data Interface allowing sampling of static meshes. */
UCLASS(EditInlineNew, Category = "Meshes", meta = (DisplayName = "Static Mesh"))
class NIAGARA_API UNiagaraDataInterfaceStaticMesh : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:
	
	/** Mesh used to sample from when not overridden by a source actor from the scene. Also useful for previewing in the editor. */
	UPROPERTY(EditAnywhere, Category = "Mesh")
	UStaticMesh* DefaultMesh;

	/** The source actor from which to sample. Takes precedence over the direct mesh. */
	UPROPERTY(EditAnywhere, Category = "Mesh")
	AActor* Source;
	
	/** The source component from which to sample. Takes precedence over the direct mesh. Not exposed to the user, only indirectly accessible from blueprints. */
	UPROPERTY(Transient)
	UStaticMeshComponent* SourceComponent;

	/** Array of filters the can be used to limit sampling to certain sections of the mesh. */
	UPROPERTY(EditAnywhere, Category = "Mesh")
	FNDIStaticMeshSectionFilter SectionFilter;

    /** Changed within the editor on PostEditChangeProperty. Should be changed whenever a refresh is desired.*/
	uint32 ChangeId;

public:

	//~ UObject interface

	virtual void PostInitProperties()override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

public:

	//~ UNiagaraDataInterface interface

	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)override;
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual int32 PerInstanceDataSize()const override { return sizeof(FNDIStaticMesh_InstanceData); }

	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target)const override { return true; }
#if WITH_EDITOR
	virtual TArray<FNiagaraDataInterfaceError> GetErrors() override;
#endif

	virtual bool GetFunctionHLSL(const FName&  DefinitionFunctionName, FString InstanceFunctionName, FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual void GetParameterDefinitionHLSL(FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual FNiagaraDataInterfaceParametersCS* ConstructComputeParameters()const override;

	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FGuid& SystemInstance) override;

	static const FString MeshIndexBufferName;
	static const FString MeshVertexBufferName;
	static const FString MeshTangentBufferName;
	static const FString MeshTexCoordBufferName;
	static const FString MeshColorBufferName;
	static const FString MeshSectionBufferName;
	static const FString MeshTriangleBufferName;
	static const FString SectionCountName;
	static const FString InstanceTransformName;
	static const FString InstanceTransformInverseTransposedName;
	static const FString InstancePrevTransformName;
	static const FString InstanceInvDeltaTimeName;
	static const FString InstanceWorldVelocityName;
	static const FString AreaWeightedSamplingName;
	static const FString NumTexCoordName;

public:
	void GetNumTriangles(FVectorVMContext& Context);

	template<typename TAreaWeighted>
	void RandomSection(FVectorVMContext& Context);

	template<typename TAreaWeighted>
	void RandomTriCoord(FVectorVMContext& Context);

	template<typename TAreaWeighted>
	void RandomTriCoordOnSection(FVectorVMContext& Context);

 	void RandomTriCoordVertexColorFiltered(FVectorVMContext& Context);
	
	template<typename TransformHandlerType>
	void GetTriCoordPosition(FVectorVMContext& Context);

	template<typename TransformHandlerType>
	void GetTriCoordNormal(FVectorVMContext& Context);

	template<typename VertexAccessorType, typename TransformHandlerType>
	void GetTriCoordTangents(FVectorVMContext& Context);

	void GetTriCoordColor(FVectorVMContext& Context);

	template<typename VertexAccessorType>
	void GetTriCoordUV(FVectorVMContext& Context);

	void GetTriCoordPositionAndVelocity(FVectorVMContext& Context);

	void GetLocalToWorld(FVectorVMContext& Context);
	void GetLocalToWorldInverseTransposed(FVectorVMContext& Context);
	void GetWorldVelocity(FVectorVMContext& Context);

	template<typename TransformHandlerType>
	void GetVertexPosition(FVectorVMContext& Context);

	void SetSourceComponentFromBlueprints(UStaticMeshComponent* ComponentToUse);
	void SetDefaultMeshFromBlueprints(UStaticMesh* MeshToUse);

	FORCEINLINE_DEBUGGABLE bool UsesSectionFilter()const { return SectionFilter.CanEverReject(); }

	//TODO: Vertex color filtering requires a bit more work.
	//FORCEINLINE bool UsesVertexColorFiltering()const { return bSupportingVertexColorSampling && bEnableVertexColorRangeSorting; }

protected:
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
	
private:
	
	template<typename TAreaWeighted, bool bFiltered>
	FORCEINLINE_DEBUGGABLE int32 RandomSection(FRandomStream& RandStream, FStaticMeshLODResources& Res, FNDIStaticMesh_InstanceData* InstData);

	template<typename TAreaWeighted, bool bFiltered>
	FORCEINLINE_DEBUGGABLE int32 RandomTriIndex(FRandomStream& RandStream, FStaticMeshLODResources& Res, FNDIStaticMesh_InstanceData* InstData);

	template<typename TAreaWeighted>
	FORCEINLINE_DEBUGGABLE int32 RandomTriIndexOnSection(FRandomStream& RandStream, FStaticMeshLODResources& Res, int32 SectionIdx, FNDIStaticMesh_InstanceData* InstData);

	void WriteTransform(const FMatrix& ToWrite, FVectorVMContext& Context);
};

//TODO: IMO this should be generalized fuhrer if possible and extended to a system allowing filtering based on texture color etc too.
struct FDynamicVertexColorFilterData
{
	//Cached ComponentToWorld.
	//Cached ComponentToWorld from previous tick.
	/** Container for the vertex colored triangles broken out by red channel values*/
	TArray<uint32> TrianglesSortedByVertexColor;
	/** Mapping from vertex color red value to starting entry in TrianglesSortedByVertexColor*/
	TArray<uint32> VertexColorToTriangleStart;

	bool Init(FNDIStaticMesh_InstanceData* Instance);
};

class FNDI_StaticMesh_GeneratedData
{
	static TMap<uint32, TSharedPtr<FDynamicVertexColorFilterData>> DynamicVertexColorFilters;

	static FCriticalSection CriticalSection;
public:
	
	/** Retrieves existing filter data for the passed mesh or generates a new one. */
	static TSharedPtr<FDynamicVertexColorFilterData> GetDynamicColorFilterData(FNDIStaticMesh_InstanceData* Instance);

	/** Todo: Find a place to call this on level change or somewhere. */
	static void CleanupDynamicColorFilterData();
};

struct FNiagaraStaticMeshData
{
	FNiagaraStaticMeshData()
		: MeshGpuSpawnBuffer(nullptr)
		, bIsGpuUniformlyDistributedSampling(false)
		, DeltaSeconds(0.03333f)
	{}

	~FNiagaraStaticMeshData()
	{
		check(IsInRenderingThread());
		if (MeshGpuSpawnBuffer)
		{
			MeshGpuSpawnBuffer->ReleaseResource();
			delete MeshGpuSpawnBuffer;
		}
	}

	/** Extra mesh data upload to GPU to do uniform sampling of sections and triangles.*/
	FStaticMeshGpuSpawnBuffer* MeshGpuSpawnBuffer;
	bool bIsGpuUniformlyDistributedSampling;
	FMatrix Transform;
	FMatrix PrevTransform;
	float DeltaSeconds;
};

struct FNiagaraPassedInstanceDataForRT
{
	bool bIsGpuUniformlyDistributedSampling;
	FMatrix Transform;
	FMatrix PrevTransform;
	float DeltaSeconds;
};

struct FNiagaraDataInterfaceProxyStaticMesh : public FNiagaraDataInterfaceProxy
{
	virtual void DeferredDestroy() override;

	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override
	{
		return sizeof(FNiagaraPassedInstanceDataForRT);
	}

	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FGuid& Instance) override;

	void InitializePerInstanceData(const FGuid& SystemInstance, FStaticMeshGpuSpawnBuffer* MeshGPUSpawnBuffer);
	void DestroyPerInstanceData(NiagaraEmitterInstanceBatcher* Batcher, const FGuid& SystemInstance);

	TMap<FGuid, FNiagaraStaticMeshData> SystemInstancesToMeshData;

	TSet<FGuid> DeferredDestroyList;
};
