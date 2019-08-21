// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraCommon.h"
#include "VectorVM.h"
#include "NiagaraDataInterfaceSpatialHash.generated.h"

struct FNDISpatialHash_InstanceData;
class FSpatialHashGPUBuffers;

class FNiagaraDINearestNeighborBatch
{
public:
	FNiagaraDINearestNeighborBatch()
		: CurrentID(0)
	{
	}

	void ClearWrite()
	{
		uint32 PrevNum = NearestNeighborResults.Num();
		NearestNeighborResults.Reset();
		IDToResultIndex.Reset();
	}

	void Init(FName InBatchID, FNDISpatialHash_InstanceData* InSpatialHashInstanceData);

	int32 SubmitQuery(FNiagaraID ParticleID, FVector Position, float SearchRadius, uint32 MaxNeighbors, bool bIncludeSelf);
	bool GetQueryResult(uint32 InQueryID, TArray<FNiagaraID>& Result);

private:
	/** Stores the results of nearest neighbor queries. */
	TArray<FNiagaraID> NearestNeighborResults;

	/** Maps a query ID to the index in the NearestNeighborResults array that the results begin. */
	TMap<uint32, int32> IDToResultIndex;
	uint32 CurrentID;
	FNDISpatialHash_InstanceData* SpatialHashInstanceData;
};

struct FNDISpatialHash_InstanceData
{
	void AllocatePersistentTables();
	void ResetTables();
	void BuildTable();
	void BuildTableGPU();
	uint32 NearestNeighbor(FNiagaraID ParticleID, FVector Position, float SearchRadius, uint32 MaxNeighbors, bool bIncludeSelf, TArray<FNiagaraID> &ClosestParticles);

	bool Init(UNiagaraDataInterfaceSpatialHash* Interface, FNiagaraSystemInstance* SystemInstance);
	FORCEINLINE_DEBUGGABLE bool Tick(UNiagaraDataInterfaceSpatialHash* Interface, FNiagaraSystemInstance* SystemInstance, float InDeltaSeconds);
	FORCEINLINE_DEBUGGABLE void Release();

	FNiagaraSystemInstance* SystemInstance;
	FNiagaraDINearestNeighborBatch SpatialHashBatch;

	uint32 MaximumParticleCount;
	uint32 TableSize;
	uint32 MaximumNeighborCount;
	float MaximumSearchRadius;

	/** Number of particles this spatial hash keeps track of. Resets to 0 after a build finishes. */
	uint32 NumParticles;

	/** The length, width, and height of a cell. Automatically set based on the maximum search radius. */
	float CellLength;

	// Size = number of particles
	struct ParticleData
	{
		uint32 CellHash;
		uint32 ParticleID;
		FVector ParticlePosition;
		FNiagaraID ExternalID;
	};
	TArray<ParticleData> Particles;
	TArray<ParticleData> Particles_Built;

	// Size = table size
	TArray<int32> StartIndex;
	TArray<int32> EndIndex;

	/** GPU Buffers */
	FSpatialHashGPUBuffers* SpatialHashGpuBuffers;
};

UCLASS(EditInlineNew, Category = "Spatial Hash", meta = (DisplayName = "Spatial Hash"))
class NIAGARA_API UNiagaraDataInterfaceSpatialHash : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()
public:

	/** Maximum number of particles that can be stored in the spatial hash. */
	UPROPERTY(EditAnywhere, Category = "SpatialHash")
	uint32 MaximumParticleCount;

	/** Size of the hash table. Make this a prime number larger than the number of particles stored for better performance. */
	UPROPERTY(EditAnywhere, Category = "SpatialHash")
	uint32 TableSize;

	/** The maximum number of neighbors that will ever be searched for. */
	UPROPERTY(EditAnywhere, Category = "SpatialHash")
	uint32 MaximumNeighborCount;

	/** The maximum search radius that neighbors will ever be searched in. This determines the cell size. */
	UPROPERTY(EditAnywhere, Category = "SpatialHash")
	float MaximumSearchRadius;

	//UObject Interface
	virtual void PostInitProperties()override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PreEditChange(UProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//UObject Interface End

	//UNiagaraDataInterface Interface
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual int32 PerInstanceDataSize() const { return sizeof(FNDISpatialHash_InstanceData); }

	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds);
	virtual bool PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds);

	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target)const override { return Target == ENiagaraSimTarget::CPUSim; }
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;

	void AddParticle(FVectorVMContext& Context);
	void BuildTable(FVectorVMContext& Context);
	void PerformKNearestNeighborQuery(FVectorVMContext& Context);
	void GetClosestNeighborFromQueryByIndex(FVectorVMContext& Context);
	void Get16ClosestNeighborsFromQuery(FVectorVMContext& Context);

	// GPU sim functionality
	void GetCommonHLSL(FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FName& DefinitionFunctionName, FString InstanceFunctionName, FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual void GetParameterDefinitionHLSL(FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual FNiagaraDataInterfaceParametersCS* ConstructComputeParameters() const override;

	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FGuid& SystemInstance) override;

	virtual void PostExecute() override;

	//UNiagaraDataInterface Interface End

protected:
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
	void PushToRenderThread();

private:
	friend class FNDISPatialHash_InstanceData;
	void BuildTableHelper();
	void GetXClosestNeighborsFromQueryHelper(FNiagaraDINearestNeighborBatch* Batch, uint32 QueryID, uint32 NumberToRetrieve, TArray<FNiagaraID>& Neighbors);
	
	static FCriticalSection CriticalSection;

public:
	static const FString ParticleIDBufferName;
	static const FString ParticlePosBufferName;
	static const FString Built_ParticleIDBufferName;
	static const FString Built_ParticlePosBufferName;
	static const FString CellCountBufferName;
	static const FString CellStartIndicesBufferName;
	static const FString CellEndIndicesBufferName;

	static const FString TableSizeName;
	static const FString MaximumNeighborCountName;
	static const FString MaximumSearchRadiusName;
	static const FString NumParticlesName;
	static const FString CellLengthName;

	static const FString NearestNeighborResultsBufferName;
	static const FString CurrentNNIDName;
};

class FSpatialHashGPUBuffers : public FRenderResource
{
public:
	virtual ~FSpatialHashGPUBuffers() {}

	void Initialize(FNDISpatialHash_InstanceData* InstanceData);

	virtual void InitRHI() override;
	virtual void ReleaseRHI() override;

	virtual FString GetFriendlyName() const override { return TEXT("FSpatialHashGPUBuffers"); }

	FRWBuffer& GetParticleIDs() { return ParticleID; }
	FRWBuffer& GetParticlePos() { return ParticlePos; }
	FRWBuffer& GetBuiltParticleIDs() { return Built_ParticleID; }
	FRWBuffer& GetBuiltParticlePos() { return Built_ParticlePos; }
	FRWBuffer& GetCellCount() { return CellCount; }
	FRWBuffer& GetCellStartIndices() { return CellStartIndices; }
	FRWBuffer& GetCellEndIndices() { return CellEndIndices; }

	FRWBuffer& GetNumParticles() { return NumParticles; }

	FRWBuffer& GetNearestNeighborResults() { return NearestNeighborResults; }
	FRWBuffer& GetCurrentNNID() { return CurrentNNID; }

	int MaximumParticleCount;
	int TableSize;
	int NumberOfParticles;
	int MaximumNeighborCount;

private:
	FRWBuffer ParticleID;
	FRWBuffer ParticlePos;
	FRWBuffer Built_ParticleID;
	FRWBuffer Built_ParticlePos;
	FRWBuffer CellCount;
	FRWBuffer CellStartIndices;
	FRWBuffer CellEndIndices;

	FRWBuffer NumParticles;

	FRWBuffer NearestNeighborResults;
	FRWBuffer CurrentNNID;
};

struct FNiagaraDISpatialHashPassedDataToRT
{
	FSpatialHashGPUBuffers* SpatialHashGpuBuffers;
	uint32 MaximumParticleCount;
	uint32 TableSize;
	uint32 MaximumNeighborCount;
	float MaximumSearchRadius;
	uint32 NumParticles;
	float CellLength;
};

struct FNiagaraDataInterfaceProxySpatialHash : public FNiagaraDataInterfaceProxy
{
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override
	{
		return sizeof(FNiagaraDISpatialHashPassedDataToRT);
	}

	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FGuid& Instance) override;

	TMap<FGuid, FNiagaraDISpatialHashPassedDataToRT> SystemInstancesToData;
};

struct FNiagaraDataInterfaceParametersCS_SpatialHash : public FNiagaraDataInterfaceParametersCS
{
	virtual ~FNiagaraDataInterfaceParametersCS_SpatialHash() {}
	virtual void Bind(const FNiagaraDataInterfaceParamRef& ParamRef, const class FShaderParameterMap& ParameterMap);
	virtual void Serialize(FArchive& Ar);
	virtual void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const override;
	virtual void Unset(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const override;

private:
	FShaderResourceParameter ParticleIDBuffer;
	FShaderResourceParameter ParticlePosBuffer;
	FShaderResourceParameter Built_ParticleIDBuffer;
	FShaderResourceParameter Built_ParticlePosBuffer;
	FShaderResourceParameter CellCountBuffer;
	FShaderResourceParameter CellStartIndicesBuffer;
	FShaderResourceParameter CellEndIndicesBuffer;
	FShaderResourceParameter NumParticles;
	FShaderResourceParameter NearestNeighborResultsBuffer;
	FShaderResourceParameter CurrentNNID;

	FShaderParameter TableSize;
	FShaderParameter MaximumNeighborCount;
	FShaderParameter MaximumSearchRadius;
	FShaderParameter CellLength;
};