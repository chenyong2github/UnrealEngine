// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraDataInterfaceMeshCommon.h"
#include "StaticMeshResources.h"
#include "Engine/StaticMesh.h"
#include "NiagaraDataInterfaceStaticMesh.generated.h"

struct FNDIStaticMesh_InstanceData;
struct FNDIStaticMeshSectionFilter;

/** Allows uniform random sampling of a number of mesh sections filtered by an FNDIStaticMeshSectionFilter */
struct FStaticMeshFilteredAreaWeightedSectionSampler : FWeightedRandomSampler
{
	FStaticMeshFilteredAreaWeightedSectionSampler();
	void Init(const FStaticMeshLODResources* InRes, FNDIStaticMesh_InstanceData* InOwner);

protected:

	virtual float GetWeights(TArray<float>& OutWeights)override;

	TRefCountPtr<const FStaticMeshLODResources> Res;
	FNDIStaticMesh_InstanceData* Owner;
};

UENUM()
enum class ENDIStaticMesh_SourceMode : uint8
{
	/**
	Default behavior.
	- Use "Source" when specified (either set explicitly or via blueprint with Set Niagara Static Mesh Component).
	- When no source is specified, attempt to find a Static Mesh Component on an attached actor or component.
	- If no source actor/component specified and no attached component found, fall back to the "Default Mesh" specified.
	*/
	Default,

	/**
	Only use "Source" (either set explicitly or via blueprint with Set Niagara Static Mesh Component).
	*/
	Source,

	/**
	Only use the parent actor or component the system is attached to.
	*/
	AttachParent,

	/**
	Only use the "Default Mesh" specified.
	*/
	DefaultMeshOnly,
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

	void Initialise(const FStaticMeshLODResources* Res, const UNiagaraDataInterfaceStaticMesh& Interface, struct FNDIStaticMesh_InstanceData* InstanceData);

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

	FRHIShaderResourceView* GetSocketTransformsSRV() const { return SocketTransformsSRV; }
	FRHIShaderResourceView* GetFilteredAndUnfilteredSocketsSRV() const { return FilteredAndUnfilteredSocketsSRV; }
	uint32 GetNumSockets() const { return NumSockets; }
	uint32 GetNumFilteredSockets() const { return NumFilteredSockets; }

protected:

	// We could separate probabilities from the triangle information when UE supports R32G32 buffer. For pack it all in a uint RGBA32 format.
	struct SectionInfo
	{
		uint32 FirstIndex;
		uint32 NumTriangles;
		float  Prob;
		uint32 Alias;
	};

	// Cached pointer to Section render data used for initialization only. This doesn't need to be ref counted since it doesn't reference CPU data.
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

	TResourceArray<FVector4> SocketTransformsResourceArray;
	FVertexBufferRHIRef SocketTransformsBuffer;
	FShaderResourceViewRHIRef SocketTransformsSRV;

	TResourceArray<uint16> FilteredAndUnfilteredSocketsResourceArray;
	FVertexBufferRHIRef FilteredAndUnfilteredSocketsBuffer;
	FShaderResourceViewRHIRef FilteredAndUnfilteredSocketsSRV;

	uint32 NumSockets = 0;
	uint32 NumFilteredSockets = 0;

#if STATS
	int64 GPUMemoryUsage = 0;
#endif
};

struct FNDIStaticMesh_InstanceData
{
	/** Cached ptr to StaticMeshComponent we sample from, when found. Otherwise, the SceneComponent to use to transform the Default or Preview mesh. */
	TWeakObjectPtr<USceneComponent> SceneComponent;

	/** Cached ptr to the mesh so that we can make sure that we haven't been deleted. */
	TWeakObjectPtr<UStaticMesh> StaticMesh;

	/** Cached ComponentToWorld. (Falls back to WorldTransform of the system instance) */
	FMatrix Transform;
	/** InverseTranspose of above for transforming normals/tangents. */
	FMatrix TransformInverseTransposed;

	/** Cached ComponentToWorld from previous tick. */
	FMatrix PrevTransform;

	/** Cached Rotation. */
	FQuat Rotation;
	/** Cached Previous Rotation. */
	FQuat PrevRotation;

	/** Time separating Transform and PrevTransform. */
	float DeltaSeconds;

	/** Velocity set by the physics body of the mesh component */
	FVector PhysicsVelocity;
	/** True if velocity should not be calculated via the transforms, but rather read the physics data from the mesh component */
	uint32 bUsePhysicsVelocity : 1;

	/** True if SceneComponent was valid on initialization (used to track invalidation of the component on tick) */
	uint32 bComponentValid : 1;
	
	/** True if StaticMesh was valid on initialization (used to track invalidation of the mesh on tick) */
	uint32 bMeshValid : 1;

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

	/** The MinLOD, see UStaticMesh::MinLOD which is platform specific.*/
	int32 MinLOD = 0;
	/** The cached LODIdx used to initialize the FNDIStaticMesh_InstanceData.*/
	int32 CachedLODIdx = 0;

	/** Cached socket information, if available */
	TArray<FTransform> CachedSockets;

	/** Number of filtered sockets. */
	int32 NumFilteredSockets = 0;

	/** Filter sockets followed by unfiltered sockets */
	TArray<uint16> FilteredAndUnfilteredSockets;

	FORCEINLINE bool UsesCpuUniformlyDistributedSampling() const { return bIsCpuUniformlyDistributedSampling; }

	FORCEINLINE_DEBUGGABLE bool ResetRequired(UNiagaraDataInterfaceStaticMesh* Interface)const;

	FORCEINLINE const TArray<int32>& GetValidSections()const { return ValidSections; }
	FORCEINLINE const FWeightedRandomSampler& GetAreaWeightedSampler() const { return Sampler; }

	void InitVertexColorFiltering();

	FORCEINLINE_DEBUGGABLE bool Init(UNiagaraDataInterfaceStaticMesh* Interface, FNiagaraSystemInstance* SystemInstance);
	FORCEINLINE_DEBUGGABLE bool Tick(UNiagaraDataInterfaceStaticMesh* Interface, FNiagaraSystemInstance* SystemInstance, float InDeltaSeconds);
	FORCEINLINE_DEBUGGABLE void Release();

	FORCEINLINE const FStaticMeshLODResources* GetCurrentFirstLOD()
	{
		UStaticMesh* Mesh = StaticMesh.Get();
		check(Mesh); // sanity - should have been checked for GC earlier
		return Mesh->GetRenderData()->GetCurrentFirstLOD(MinLOD);
	}
};

/** Data Interface allowing sampling of static meshes. */
UCLASS(EditInlineNew, Category = "Meshes", meta = (DisplayName = "Static Mesh"))
class NIAGARA_API UNiagaraDataInterfaceStaticMesh : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:

	enum class ESampleMode : int32
	{
		Invalid = -1,
		Default,
		AreaWeighted
	};

	DECLARE_NIAGARA_DI_PARAMETER();

	/** Controls how to retrieve the Static Mesh Component to attach to. */
	UPROPERTY(EditAnywhere, Category = "Mesh")
	ENDIStaticMesh_SourceMode SourceMode;
	
#if WITH_EDITORONLY_DATA
	/** Mesh used to sample from when not overridden by a source actor from the scene. Only available in editor for previewing. This is removed in cooked builds. */
	UPROPERTY(EditAnywhere, Category = "Mesh")
	TSoftObjectPtr<UStaticMesh> PreviewMesh;
#endif

	/** Mesh used to sample from when not overridden by a source actor from the scene. This mesh is NOT removed from cooked builds. */
	UPROPERTY(EditAnywhere, Category = "Mesh")
	UStaticMesh* DefaultMesh;

	/** The source actor from which to sample. Takes precedence over the direct mesh. Note that this can only be set when used as a user variable on a component in the world. */
	UPROPERTY(EditAnywhere, Category = "Mesh")
	AActor* Source;
	
	/** The source component from which to sample. Takes precedence over the direct mesh. Not exposed to the user, only indirectly accessible from blueprints. */
	UPROPERTY(Transient)
	UStaticMeshComponent* SourceComponent;

	/** Array of filters the can be used to limit sampling to certain sections of the mesh. */
	UPROPERTY(EditAnywhere, Category = "Mesh")
	FNDIStaticMeshSectionFilter SectionFilter;

	/** If true then the mesh velocity is taken from the mesh component's physics data. Otherwise it will be calculated by diffing the component transforms between ticks, which is more reliable but won't work on the first frame. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Mesh")
    bool bUsePhysicsBodyVelocity;

	/** List of filtered sockets to use. */
	UPROPERTY(EditAnywhere, Category = "Mesh")
	TArray<FName> FilteredSockets;

    /** Changed within the editor on PostEditChangeProperty. Should be changed whenever a refresh is desired.*/
	uint32 ChangeId;

public:

	//~ UObject interface

	virtual void PostInitProperties()override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif

public:

	//~ UNiagaraDataInterface interface

	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual int32 PerInstanceDataSize() const override { return sizeof(FNDIStaticMesh_InstanceData); }

	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return true; }
	virtual bool HasPreSimulateTick() const override { return true; }

#if WITH_EDITOR
	virtual void GetFeedback(UNiagaraSystem* Asset, UNiagaraComponent* Component, TArray<FNiagaraDataInterfaceError>& OutErrors,
		TArray<FNiagaraDataInterfaceFeedback>& OutWarnings, TArray<FNiagaraDataInterfaceFeedback>& OutInfo) override;
#endif

#if WITH_EDITORONLY_DATA
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
#endif

	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override;

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
	static const FString InstanceRotationName;
	static const FString InstancePrevRotationName;
	static const FString InstanceInvDeltaTimeName;
	static const FString InstanceWorldVelocityName;
	static const FString AreaWeightedSamplingName;
	static const FString NumTexCoordName;
	static const FString UseColorBufferName;
	static const FString SocketTransformsName;
	static const FString FilteredAndUnfilteredSocketsName;
	static const FString NumSocketsAndFilteredName;

public:
	UStaticMesh* GetStaticMesh(TWeakObjectPtr<USceneComponent>& OutComponent, class FNiagaraSystemInstance* SystemInstance = nullptr);

	void IsValid(FVectorVMContext& Context);

	template<typename TSampleMode>
	void RandomSection(FVectorVMContext& Context);

	template<typename TSampleMode>
	void RandomTriCoord(FVectorVMContext& Context);

	template<typename TSampleMode>
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

	// Socket Functions
	void GetSocketCount(FVectorVMContext& Context);
	void GetFilteredSocketCount(FVectorVMContext& Context);
	void GetUnfilteredSocketCount(FVectorVMContext& Context);
	template<bool bWorldSpace>
	void GetSocketTransform(FVectorVMContext& Context);
	template<bool bWorldSpace>
	void GetFilteredSocketTransform(FVectorVMContext& Context);
	template<bool bWorldSpace>
	void GetUnfilteredSocketTransform(FVectorVMContext& Context);

	void SetSourceComponentFromBlueprints(UStaticMeshComponent* ComponentToUse);
	void SetDefaultMeshFromBlueprints(UStaticMesh* MeshToUse);

	FORCEINLINE_DEBUGGABLE bool UsesSectionFilter()const { return SectionFilter.CanEverReject(); }

	//TODO: Vertex color filtering requires a bit more work.
	//FORCEINLINE bool UsesVertexColorFiltering()const { return bSupportingVertexColorSampling && bEnableVertexColorRangeSorting; }

protected:
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
	
private:
	
	template<typename TSampleMode, bool bFiltered>
	FORCEINLINE_DEBUGGABLE int32 RandomSection(FRandomStream& RandStream, const FStaticMeshLODResources& Res, FNDIStaticMesh_InstanceData* InstData);

	template<typename TSampleMode, bool bFiltered>
	FORCEINLINE_DEBUGGABLE int32 RandomTriIndex(FRandomStream& RandStream, const FStaticMeshLODResources& Res, FNDIStaticMesh_InstanceData* InstData);

	template<typename TSampleMode>
	FORCEINLINE_DEBUGGABLE int32 RandomTriIndexOnSection(FRandomStream& RandStream, const FStaticMeshLODResources& Res, int32 SectionIdx, FNDIStaticMesh_InstanceData* InstData);

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
	FQuat Rotation;
	FQuat PrevRotation;
	float DeltaSeconds;
};

struct FNiagaraPassedInstanceDataForRT
{
	bool bIsGpuUniformlyDistributedSampling;
	FMatrix Transform;
	FMatrix PrevTransform;
	FQuat Rotation;
	FQuat PrevRotation;
	float DeltaSeconds;
};

struct FNiagaraDataInterfaceProxyStaticMesh : public FNiagaraDataInterfaceProxy
{
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override
	{
		return sizeof(FNiagaraPassedInstanceDataForRT);
	}

	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override;

	void InitializePerInstanceData(const FNiagaraSystemInstanceID& SystemInstance, FStaticMeshGpuSpawnBuffer* MeshGPUSpawnBuffer);
	void DestroyPerInstanceData(NiagaraEmitterInstanceBatcher* Batcher, const FNiagaraSystemInstanceID& SystemInstance);

	TMap<FNiagaraSystemInstanceID, FNiagaraStaticMeshData> SystemInstancesToMeshData;
};
