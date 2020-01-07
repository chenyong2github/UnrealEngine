// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"
#include "Containers/DynamicRHIResourceArray.h"
#include "RHI.h"
#include "VectorVM.h"
#include "RenderingThread.h"
#include "NiagaraDataSet.generated.h"

/** Helper class defining the layout and location of an FNiagaraVariable in an FNiagaraDataBuffer-> */
USTRUCT()
struct FNiagaraVariableLayoutInfo
{
	GENERATED_BODY()

	/** Start index for the float components in the main buffer. */
	UPROPERTY()
	uint32 FloatComponentStart;

	/** Start index for the int32 components in the main buffer. */
	UPROPERTY()
	uint32 Int32ComponentStart;

	uint32 GetNumFloatComponents()const { return LayoutInfo.FloatComponentByteOffsets.Num(); }
	uint32 GetNumInt32Components()const { return LayoutInfo.Int32ComponentByteOffsets.Num(); }

	/** This variable's type layout info. */
	UPROPERTY()
	FNiagaraTypeLayoutInfo LayoutInfo;
};

class FNiagaraDataSet;
class FNiagaraShader;
class FNiagaraGPUInstanceCountManager;
struct FNiagaraComputeExecutionContext;

//Base class for objects in Niagara that are owned by one object but are then passed for reading to other objects, potentially on other threads.
//This class allows us to know if the object is being used so we do not overwrite it and to ensure it's lifetime so we do not access freed data.
class FNiagaraSharedObject
{
public:
	FNiagaraSharedObject()
		: ReadRefCount(0)
	{}

	/** The owner of this object is now done with it but it may still be in use by others, possibly on other threads. Add to the deletion queue so it can be safely freed when it's no longer in use. */
	void Destroy();
	static void FlushDeletionList();

	FORCEINLINE bool IsInUse()const { return ReadRefCount.Load() != 0; }
	FORCEINLINE bool IsBeingRead()const { return ReadRefCount.Load() > 0; }
	FORCEINLINE bool IsBeingWritten()const { return ReadRefCount.Load() == INDEX_NONE; }

	FORCEINLINE void AddReadRef()
	{
		check(!IsBeingWritten());
		ReadRefCount++;
	}

	FORCEINLINE void ReleaseReadRef()
	{
		check(IsBeingRead());
		ReadRefCount--;
	}

	FORCEINLINE bool TryLock()
	{
		//Only lock if we have no readers.
		//Using INDEX_NONE as a special case value for write locks.
		int32 Expected = 0;
		return ReadRefCount.CompareExchange(Expected, INDEX_NONE);
	}

	FORCEINLINE void Unlock()
	{
		int32 Expected = INDEX_NONE;
		ensureAlwaysMsgf(ReadRefCount.CompareExchange(Expected, 0), TEXT("Trying to release a write lock on a Niagara shared object that is not locked for write."));
	}

protected:

	/**
	Count of other object currently reading this data. Keeps us from writing to or deleting this data while it's in use. These reads can be on any thread so atomic is used.
	INDEX_NONE used as special case marking this object as locked for write.
	*/
	TAtomic<int32> ReadRefCount;

	static FCriticalSection CritSec;
	static TArray<FNiagaraSharedObject*> DeferredDeletionList;

	virtual ~FNiagaraSharedObject() {}
};

/** Buffer containing one frame of Niagara simulation data. */
class NIAGARA_API FNiagaraDataBuffer : public FNiagaraSharedObject
{
	friend class FScopedNiagaraDataSetGPUReadback;
protected:
	virtual ~FNiagaraDataBuffer();

public:
	FNiagaraDataBuffer(FNiagaraDataSet* InOwner);
	void Allocate(uint32 NumInstances, bool bMaintainExisting = false);
	void AllocateGPU(uint32 InNumInstances, FNiagaraGPUInstanceCountManager& GPUInstanceCountManager, FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel);
	void SwapInstances(uint32 OldIndex, uint32 NewIndex);
	void KillInstance(uint32 InstanceIdx);
	void CopyTo(FNiagaraDataBuffer& DestBuffer, int32 SrcStartIdx, int32 DestStartIdx, int32 NumInstances)const;
	void GPUCopyFrom(float* GPUReadBackFloat, int* GPUReadBackInt, int32 StartIdx, int32 NumInstances, uint32 InSrcFloatStride, uint32 InSrcIntStride);
	void Dump(int32 StartIndex, int32 NumInstances, const FString& Label)const;

	FORCEINLINE TArray<uint8*>& GetRegisterTable() { return RegisterTable; }

	FORCEINLINE const TArray<uint8>& GetFloatBuffer()const { return FloatData; }
	FORCEINLINE const TArray<uint8>& GetInt32Buffer()const { return Int32Data; }

	FORCEINLINE const uint8* GetComponentPtrFloat(uint32 ComponentIdx)const	{ return FloatData.GetData() + FloatStride * ComponentIdx; }
	FORCEINLINE const uint8* GetComponentPtrInt32(uint32 ComponentIdx)const	{ return Int32Data.GetData() + Int32Stride * ComponentIdx; }
	FORCEINLINE uint8* GetComponentPtrFloat(uint32 ComponentIdx) { return FloatData.GetData() + FloatStride * ComponentIdx;	}
	FORCEINLINE uint8* GetComponentPtrInt32(uint32 ComponentIdx) { return Int32Data.GetData() + Int32Stride * ComponentIdx;	}

	FORCEINLINE float* GetInstancePtrFloat(uint32 ComponentIdx, uint32 InstanceIdx)	{ return (float*)GetComponentPtrFloat(ComponentIdx) + InstanceIdx; }
	FORCEINLINE int32* GetInstancePtrInt32(uint32 ComponentIdx, uint32 InstanceIdx)	{ return (int32*)GetComponentPtrInt32(ComponentIdx) + InstanceIdx; }
	FORCEINLINE float* GetInstancePtrFloat(uint32 ComponentIdx, uint32 InstanceIdx)const { return (float*)GetComponentPtrFloat(ComponentIdx) + InstanceIdx; }
	FORCEINLINE int32* GetInstancePtrInt32(uint32 ComponentIdx, uint32 InstanceIdx)const { return (int32*)GetComponentPtrInt32(ComponentIdx) + InstanceIdx;	}

	FORCEINLINE uint8* GetComponentPtrFloat(float* BasePtr, uint32 ComponentIdx) const { return (uint8*)BasePtr + FloatStride * ComponentIdx; }
	FORCEINLINE uint8* GetComponentPtrInt32(int* BasePtr, uint32 ComponentIdx) const { return (uint8*)BasePtr + Int32Stride * ComponentIdx; }
	FORCEINLINE float* GetInstancePtrFloat(float* BasePtr, uint32 ComponentIdx, uint32 InstanceIdx)const { return (float*)GetComponentPtrFloat(BasePtr, ComponentIdx) + InstanceIdx; }
	FORCEINLINE int32* GetInstancePtrInt32(int* BasePtr, uint32 ComponentIdx, uint32 InstanceIdx)const { return (int32*)GetComponentPtrInt32(BasePtr, ComponentIdx) + InstanceIdx; }

	FORCEINLINE uint32 GetNumInstances()const { return NumInstances; }
	FORCEINLINE uint32 GetNumInstancesAllocated()const { return NumInstancesAllocated; }

	FORCEINLINE void SetNumInstances(uint32 InNumInstances) { NumInstances = InNumInstances; }
	FORCEINLINE uint32 GetSizeBytes()const { return FloatData.Num() + Int32Data.Num(); }
	FORCEINLINE FRWBuffer& GetGPUBufferFloat() { return GPUBufferFloat; }
	FORCEINLINE FRWBuffer& GetGPUBufferInt() { return GPUBufferInt;	}
	FORCEINLINE uint32 GetGPUInstanceCountBufferOffset() const { return GPUInstanceCountBufferOffset; }
	FORCEINLINE void ClearGPUInstanceCountBufferOffset() { GPUInstanceCountBufferOffset = INDEX_NONE; }
	FORCEINLINE uint32 GetGPUNumAllocatedIDs() const { return NumIDsAllocatedForGPU; }
	FORCEINLINE FRWBuffer& GetGPUFreeIDs() { return GPUFreeIDs; }
	FORCEINLINE FRWBuffer& GetGPUIDToIndexTable() { return GPUIDToIndexTable; }

	FORCEINLINE int32 GetSafeComponentBufferSize() const { return GetSafeComponentBufferSize(GetNumInstancesAllocated()); }
	FORCEINLINE uint32 GetFloatStride() const { return FloatStride; }
	FORCEINLINE uint32 GetInt32Stride() const { return Int32Stride; }

	FORCEINLINE FNiagaraDataSet* GetOwner()const { return Owner; }

	int32 TransferInstance(FNiagaraDataBuffer& SourceBuffer, int32 InstanceIndex, bool bRemoveFromSource=true);

	bool CheckForNaNs()const;

	FORCEINLINE TArray<int32>& GetIDTable() { return IDToIndexTable; }

	void SetShaderParams(class FNiagaraShader *Shader, FRHICommandList &CommandList, bool bInput);
	void UnsetShaderParams(class FNiagaraShader *Shader, FRHICommandList &CommandList);

	void ReleaseGPUInstanceCount(FNiagaraGPUInstanceCountManager& GPUInstanceCountManager);

	FORCEINLINE const TArray<uint8*>& GetRegisterTable()const { return RegisterTable; }

	void BuildRegisterTable();
private:

	FORCEINLINE void CheckUsage(bool bReadOnly)const;

	FORCEINLINE int32 GetSafeComponentBufferSize(int32 RequiredSize) const
	{
		//Round up to VECTOR_WIDTH_BYTES.
		//Both aligns the component buffers to the vector width but also ensures their ops cannot stomp over one another.		
		return RequiredSize + VECTOR_WIDTH_BYTES - (RequiredSize % VECTOR_WIDTH_BYTES) + VECTOR_WIDTH_BYTES;
	}

	/** Back ptr to our owning data set. Used to access layout info for the buffer. */
	FNiagaraDataSet* Owner;

	//////////////////////////////////////////////////////////////////////////
	//CPU Data
	/** Float components of simulation data. */
	TArray<uint8> FloatData;
	/** Int32 components of simulation data. */
	TArray<uint8> Int32Data;

	/** Table of IDs to real buffer indices. */
	TArray<int32> IDToIndexTable;
	//////////////////////////////////////////////////////////////////////////

	//////////////////////////////////////////////////////////////////////////
	// GPU Data
	/** The buffer offset where the instance count is accumulated. */
	uint32 GPUInstanceCountBufferOffset;
	/** The num of allocated chunks, each being of size ALLOC_CHUNKSIZE */
	uint32 NumChunksAllocatedForGPU;
	/** GPU Buffer containing floating point values for GPU simulations. */
	FRWBuffer GPUBufferFloat;
	/** GPU Buffer containing floating point values for GPU simulations. */
	FRWBuffer GPUBufferInt;
	/** Size of the GPU ID buffers. */
	uint32 NumIDsAllocatedForGPU;
	/** GPU list of free particle IDs. */
	FRWBuffer GPUFreeIDs;
	/** GPU table which maps particle ID to index. */
	FRWBuffer GPUIDToIndexTable;
	//////////////////////////////////////////////////////////////////////////

	/** Number of instances in data. */
	uint32 NumInstances;
	/** Number of instances the buffer has been allocated for. */
	uint32 NumInstancesAllocated;
	/** Stride between components in the float buffer. */
	uint32 FloatStride;
	/** Stride between components in the int32 buffer. */
	uint32 Int32Stride;

	/** Table containing current base locations for all registers in this dataset. */
	TArray<uint8*> RegisterTable;//TODO: Should make inline? Feels like a useful size to keep local would be too big.
};

//////////////////////////////////////////////////////////////////////////

USTRUCT()
struct NIAGARA_API FNiagaraDataSetCompiledData
{
	GENERATED_BODY()

	/** Variables in the data set. */
	UPROPERTY()
	TArray<FNiagaraVariable> Variables;

	/** Data describing the layout of variable data. */
	UPROPERTY()
	TArray<FNiagaraVariableLayoutInfo> VariableLayouts;

	/** Total number of components of each type in the data set. */
	UPROPERTY()
	uint32 TotalFloatComponents;

	UPROPERTY()
	uint32 TotalInt32Components;

	/** Whether or not this dataset require persistent IDs. */
	UPROPERTY()
	uint32 bNeedsPersistentIDs : 1;

	/** Unique ID for this DataSet. Used to allow referencing from other emitters and Systems. */
	UPROPERTY()
	FNiagaraDataSetID ID;

	/** Sim target this DataSet is targeting (CPU/GPU). */
	UPROPERTY()
	ENiagaraSimTarget SimTarget;

	FNiagaraDataSetCompiledData();
	void BuildLayout();
	void Empty();

	static FNiagaraDataSetCompiledData DummyCompiledData;
};


//////////////////////////////////////////////////////////////////////////

/**
General storage class for all per instance simulation data in Niagara.
*/
class NIAGARA_API FNiagaraDataSet
{
	friend FNiagaraDataBuffer;
public:

	FNiagaraDataSet();
	~FNiagaraDataSet();
	FNiagaraDataSet& operator=(const FNiagaraDataSet&) = delete;

	FORCEINLINE void Init(const FNiagaraDataSetCompiledData* InDataSetCompiledData)
	{
		//CompiledData = InDataSetCompiledData != nullptr ? InDataSetCompiledData : &FNiagaraDataSetCompiledData::DummyCompiledData;
		//Temporarily taking a copy of the compiled data to avoid lifetime issues in some cases.
		CompiledData = InDataSetCompiledData != nullptr ? *InDataSetCompiledData : FNiagaraDataSetCompiledData::DummyCompiledData;
		bInitialized = true;
		Reset();
	}

	/** Resets current data but leaves variable/layout information etc intact. */
	void ResetBuffers();

	/** Begins a new simulation pass and grabs a destination buffer. Returns the new destination data buffer. */
	FNiagaraDataBuffer& BeginSimulate();

	/** Ends a simulation pass and sets the current simulation state. */
	void EndSimulate(bool SetCurrentData = true);

	/** Allocates space for NumInstances in the current destination buffer. */
	void Allocate(int32 NumInstances, bool bMaintainExisting = false);

	/** Returns size in bytes for all data buffers currently allocated by this dataset. */
	uint32 GetSizeBytes()const;

	FORCEINLINE bool IsInitialized()const { return bInitialized; }
	FORCEINLINE ENiagaraSimTarget GetSimTarget()const { return CompiledData.SimTarget; }
	FORCEINLINE FNiagaraDataSetID GetID()const { return CompiledData.ID; }	
	FORCEINLINE bool GetNeedsPersistentIDs()const { return CompiledData.bNeedsPersistentIDs; }

	FORCEINLINE TArray<int32>& GetFreeIDTable() { return FreeIDsTable; }
	FORCEINLINE int32& GetNumFreeIDs() { return NumFreeIDs; }
	FORCEINLINE int32& GetMaxUsedID() { return MaxUsedID; }
	FORCEINLINE int32& GetIDAcquireTag() { return IDAcquireTag; }
	FORCEINLINE void SetIDAcquireTag(int32 InTag) { IDAcquireTag = InTag; }

	FORCEINLINE const TArray<FNiagaraVariable>& GetVariables()const { return CompiledData.Variables; }
	FORCEINLINE uint32 GetNumVariables()const { return CompiledData.Variables.Num(); }
	FORCEINLINE bool HasVariable(const FNiagaraVariable& Var)const { return CompiledData.Variables.Contains(Var); }
	FORCEINLINE uint32 GetNumFloatComponents()const { return CompiledData.TotalFloatComponents; }
	FORCEINLINE uint32 GetNumInt32Components()const { return CompiledData.TotalInt32Components; }

	const TArray<FNiagaraVariableLayoutInfo>& GetVariableLayouts()const { return CompiledData.VariableLayouts; }
	const FNiagaraVariableLayoutInfo* GetVariableLayout(const FNiagaraVariable& Var)const;
	bool GetVariableComponentOffsets(const FNiagaraVariable& Var, int32 &FloatStart, int32 &IntStart) const;

	void CopyTo(FNiagaraDataSet& Other, int32 StartIdx = 0, int32 NumInstances = INDEX_NONE, bool bResetOther=true)const;

	void CopyFromGPUReadback(float* GPUReadBackFloat, int* GPUReadBackInt, int32 StartIdx = 0, int32 NumInstances = INDEX_NONE, uint32 FloatStride = 0, uint32 IntStride = 0);

	void CheckForNaNs()const;

	void Dump(int32 StartIndex, int32 NumInstances, const FString& Label)const;

	FORCEINLINE bool IsCurrentDataValid()const { return CurrentData != nullptr; }
	FORCEINLINE FNiagaraDataBuffer* GetCurrentData()const {	return CurrentData; }
	FORCEINLINE FNiagaraDataBuffer* GetDestinationData()const { return DestinationData; }

	FORCEINLINE FNiagaraDataBuffer& GetCurrentDataChecked()const
	{
		check(CurrentData);
		return *CurrentData;
	}

	FORCEINLINE FNiagaraDataBuffer& GetDestinationDataChecked()const
	{
		check(DestinationData);
		return *DestinationData;
	}

	/** Release the GPU instance counts so that they can be reused */
	void ReleaseGPUInstanceCounts(FNiagaraGPUInstanceCountManager& GPUInstanceCountManager);

private:

	void Reset();

	void BuildLayout();

	void ResetBuffersInternal();
	void ReleaseBuffers();

	FORCEINLINE void CheckCorrectThread()const
	{
		// In some rare occasions, the render thread might be null, like when offloading work to Lightmass 
		// The final GRenderingThread check keeps us from inadvertently failing when that happens.
#if DO_GUARD_SLOW
		ENiagaraSimTarget SimTarget = GetSimTarget();
		bool CPUSimOK = (SimTarget == ENiagaraSimTarget::CPUSim && !IsInRenderingThread());
		bool GPUSimOK = (SimTarget == ENiagaraSimTarget::GPUComputeSim && IsInRenderingThread());
		checkfSlow(!GRenderingThread || CPUSimOK || GPUSimOK, TEXT("NiagaraDataSet function being called on incorrect thread."));
#endif
	}

	//const FNiagaraDataSetCompiledData* CompiledData;
	//For safety we're temporarily taking a copy of the compiled data. In certain cases the lifetime of the CompiledData ptr cannot be guaranteed. 
	FNiagaraDataSetCompiledData CompiledData;

	/** Table of free IDs available to allocate next tick. */
	TArray<int32> FreeIDsTable;

	/** Number of free IDs in FreeIDTable. */
	int32 NumFreeIDs;

	/** Max ID seen in last execution. Allows us to shrink the IDTable. */
	int32 MaxUsedID;

	/** Tag to use when new IDs are acquired. Should be unique per tick. */
	int32 IDAcquireTag;

	/** Buffer containing the current simulation state. */
	FNiagaraDataBuffer* CurrentData;

	/** Buffer we're currently simulating into. Only valid while we're simulating i.e between PrepareForSimulate and EndSimulate calls.*/
	FNiagaraDataBuffer* DestinationData;

	/**
	Actual data storage. These are passed to and read directly by the RT.
	This is effectively a pool of buffers for this simulation.
	Typically this should only be two or three entries and we search for a free buffer to write into on BeginSimulate();
	We keep track of the Current and Previous buffers which move with each simulate.
	Additional buffers may be in here if they are currently being used by the render thread.
	*/
	TArray<FNiagaraDataBuffer*, TInlineAllocator<2>> Data;

	bool bInitialized;
};

/**
General iterator for getting and setting data in and FNiagaraDataSet.
*/
struct FNiagaraDataSetAccessorBase
{
	FNiagaraDataSetAccessorBase()
		: DataSet(nullptr)
		, VarLayout(nullptr)
	{}

	FNiagaraDataSetAccessorBase(FNiagaraDataSet* InDataSet, FNiagaraVariable InVar)
		: DataSet(InDataSet)
	{
		Var = InVar;
		VarLayout = DataSet->GetVariableLayout(InVar);
	}

	void Create(FNiagaraDataSet* InDataSet, FNiagaraVariable InVar)
	{
		DataSet = InDataSet;
		Var = InVar;
		VarLayout = DataSet->GetVariableLayout(InVar);
	}

	FORCEINLINE void SetDataSet(FNiagaraDataSet& InDataSet)
	{
		if (Var.IsValid())
		{
			DataSet = &InDataSet;
			//checkSlow(VarLayout == InDataSet.GetVariableLayout(Var));
		}
		else
		{
			DataSet = nullptr;
			VarLayout = nullptr;
		}
	}

	FORCEINLINE bool IsValid()const { return DataSet && VarLayout != nullptr; }
protected:

	FNiagaraDataSet* DataSet;
	const FNiagaraVariableLayoutInfo* VarLayout;
	FNiagaraVariable Var;
};

template<typename T>
struct FNiagaraDataSetAccessor : public FNiagaraDataSetAccessorBase
{
	FNiagaraDataSetAccessor<T>() {}
	FNiagaraDataSetAccessor(FNiagaraDataSet& InDataSet, FNiagaraVariable InVar)
		: FNiagaraDataSetAccessorBase(&InDataSet, InVar)
	{
		check(sizeof(T) == InVar.GetType().GetSize());
		checkf(false, TEXT("You must provide a fast runtime specialization for this type."));// Allow this slow generic version?
	}

	FORCEINLINE T operator[](int32 Index)const
	{
		return Get(Index);
	}

	FORCEINLINE T Get(int32 Index)const
	{
		T Ret;
		Get(Index, Ret);
		return Ret;
	}

	FORCEINLINE void Get(int32 Index, T& OutValue)const
	{
		checkSlow(DataSet);
		uint8* ValuePtr = (uint8*)&OutValue;

		FNiagaraDataBuffer* DataBuffer = DataSet->GetCurrentData();
		checkSlow(DataBuffer);

		for (uint32 CompIdx = 0; CompIdx < VarLayout->GetNumFloatComponents(); ++CompIdx)
		{
			uint32 CompBufferOffset = VarLayout->FloatComponentStart + CompIdx;
			float* Src = DataBuffer->GetInstancePtrFloat(CompBufferOffset, Index);
			float* Dst = (float*)(ValuePtr + VarLayout->LayoutInfo.FloatComponentByteOffsets[CompIdx]);
			*Dst = *Src;
		}

		for (uint32 CompIdx = 0; CompIdx < VarLayout->GetNumInt32Components(); ++CompIdx)
		{
			uint32 CompBufferOffset = VarLayout->Int32ComponentStart + CompIdx;
			int32* Src = DataBuffer->GetInstancePtrInt32(CompBufferOffset, Index);
			int32* Dst = (int32*)(ValuePtr + VarLayout->LayoutInfo.Int32ComponentByteOffsets[CompIdx]);
			*Dst = *Src;
		}
	}

	FORCEINLINE void Set(int32 Index, const T& InValue)
	{
		checkSlow(DataSet);
		uint8* ValuePtr = (uint8*)&InValue;

		FNiagaraDataBuffer* DataBuffer = DataSet->GetDestinationData();
		checkSlow(DataBuffer);

		for (uint32 CompIdx = 0; CompIdx < VarLayout->GetNumFloatComponents(); ++CompIdx)
		{
			uint32 CompBufferOffset = VarLayout->FloatComponentStart + CompIdx;
			float* Dst = DataBuffer->GetInstancePtrFloat(CompBufferOffset, Index);
			float* Src = (float*)(ValuePtr + VarLayout->LayoutInfo.FloatComponentByteOffsets[CompIdx]);
			*Dst = *Src;
		}

		for (uint32 CompIdx = 0; CompIdx < VarLayout->GetNumInt32Components(); ++CompIdx)
		{
			uint32 CompBufferOffset = VarLayout->Int32ComponentStart + CompIdx;
			int32* Dst = DataBuffer->GetInstancePtrInt32(CompBufferOffset, Index);
			int32* Src = (int32*)(ValuePtr + VarLayout->LayoutInfo.Int32ComponentByteOffsets[CompIdx]);
			*Dst = *Src;
		}
	}
};

template<>
struct FNiagaraDataSetAccessor<FNiagaraBool> : public FNiagaraDataSetAccessorBase
{
	FNiagaraDataSetAccessor<FNiagaraBool>() {}
	FNiagaraDataSetAccessor<FNiagaraBool>(FNiagaraDataSet& InDataSet, FNiagaraVariable InVar)
		: FNiagaraDataSetAccessorBase(&InDataSet, InVar)
	{
		check(sizeof(FNiagaraBool) == InVar.GetType().GetSize());
		InitForAccess();
	}

	void InitForAccess()
	{
		SrcBase = nullptr;
		DestBase = nullptr;
		FNiagaraDataBuffer* SrcBuffer = DataSet->GetCurrentData();
		if (IsValid() && SrcBuffer)
		{
			SrcBase = (int32*)SrcBuffer->GetComponentPtrInt32(VarLayout->Int32ComponentStart);

			//Writes are only valid if we're during a simulation pass.
			FNiagaraDataBuffer* DestBuffer = DataSet->GetDestinationData();
			if (DestBuffer)
			{
				DestBase = (int32*)DestBuffer->GetComponentPtrInt32(VarLayout->Int32ComponentStart);
			}
		}
	}

	FORCEINLINE bool IsValidForRead()const { return SrcBase != nullptr; }
	FORCEINLINE bool IsValidForWrite()const { return DestBase != nullptr; }

	FORCEINLINE FNiagaraBool operator[](int32 Index)const
	{
		return Get(Index);
	}

	FORCEINLINE FNiagaraBool Get(int32 Index)const
	{
		FNiagaraBool Ret;
		Get(Index, Ret);
		return Ret;
	}

	FORCEINLINE FNiagaraBool GetSafe(int32 Index, bool Default = true)const
	{
		if (IsValidForRead())
		{
			return Get(Index);
		}

		return Default;
	}

	FORCEINLINE void Get(int32 Index, FNiagaraBool& OutValue)const
	{
		checkSlow(IsValidForRead());
		OutValue.SetRawValue(SrcBase[Index]);
	}

	FORCEINLINE void Set(int32 Index, const FNiagaraBool& InValue)
	{
		checkSlow(IsValidForWrite());
		DestBase[Index] = InValue.GetRawValue();
	}

private:

	int32* SrcBase;
	int32* DestBase;
};

template<>
struct FNiagaraDataSetAccessor<int32> : public FNiagaraDataSetAccessorBase
{
	FNiagaraDataSetAccessor<int32>() {}
	FNiagaraDataSetAccessor<int32>(FNiagaraDataSet& InDataSet, FNiagaraVariable InVar)
		: FNiagaraDataSetAccessorBase(&InDataSet, InVar)
	{
		check(sizeof(int32) == InVar.GetType().GetSize());
		InitForAccess();
	}

	void InitForAccess()
	{
		SrcBase = nullptr;
		DestBase = nullptr;
		if (IsValid())
		{
			FNiagaraDataBuffer* SrcBuffer = DataSet->GetCurrentData();
			if (SrcBuffer)
			{
				SrcBase = (int32*)SrcBuffer->GetComponentPtrInt32(VarLayout->Int32ComponentStart);

				//Writes are only valid if we're during a simulation pass.
				FNiagaraDataBuffer* DestBuffer = DataSet->GetDestinationData();
				if (DestBuffer)
				{
					DestBase = (int32*)DestBuffer->GetComponentPtrInt32(VarLayout->Int32ComponentStart);
				}
			}
		}
	}

	FORCEINLINE bool IsValidForRead()const { return SrcBase != nullptr; }
	FORCEINLINE bool IsValidForWrite()const { return DestBase != nullptr; }

	FORCEINLINE int32 operator[](int32 Index)const
	{
		return Get(Index);
	}

	FORCEINLINE int32 Get(int32 Index)const
	{
		int32 Ret;
		Get(Index, Ret);
		return Ret;
	}

	FORCEINLINE int32 GetSafe(int32 Index, int32 Default = 0)const
	{
		if (IsValidForRead())
		{
			FNiagaraDataBuffer* DataBuffer = DataSet->GetCurrentData();
			if (DataBuffer && Index >= 0 && (uint32)Index < DataBuffer->GetNumInstances())
			{
				return Get(Index);
			}
			else
			{
				ensure(DataBuffer && Index >= 0 && (uint32)Index < DataBuffer->GetNumInstances()); // just to capture the badness in the logs or debugger if attached.
			}
		}

		return Default;
	}

	FORCEINLINE void Get(int32 Index, int32& OutValue)const
	{
		checkSlow(IsValidForRead());
		OutValue = SrcBase[Index];
	}

	FORCEINLINE void Set(int32 Index, const int32& InValue)
	{
		checkSlow(IsValidForWrite());
		DestBase[Index] = InValue;
	}

private:

	int32* SrcBase;
	int32* DestBase;
};

template<>
struct FNiagaraDataSetAccessor<float> : public FNiagaraDataSetAccessorBase
{
	FNiagaraDataSetAccessor<float>() {}
	FNiagaraDataSetAccessor<float>(FNiagaraDataSet& InDataSet, FNiagaraVariable InVar)
		: FNiagaraDataSetAccessorBase(&InDataSet, InVar)
	{
		check(sizeof(float) == InVar.GetType().GetSize());
		InitForAccess();
	}

	void InitForAccess()
	{
		SrcBase = nullptr;
		DestBase = nullptr;
		FNiagaraDataBuffer* SrcBuffer = DataSet->GetCurrentData();
		if (IsValid() && SrcBuffer)
		{
			SrcBase = (float*)SrcBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart);

			//Writes are only valid if we're during a simulation pass.
			FNiagaraDataBuffer* DestBuffer = DataSet->GetDestinationData();
			if (DestBuffer)
			{
				DestBase = (float*)DestBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart);
			}
		}
	}

	FORCEINLINE bool IsValidForRead()const { return SrcBase != nullptr; }
	FORCEINLINE bool IsValidForWrite()const { return DestBase != nullptr; }

	FORCEINLINE float operator[](int32 Index)const
	{
		return Get(Index);
	}

	FORCEINLINE float GetSafe(int32 Index, float Default = 0.0f)const
	{
		if (IsValidForRead())
		{
			FNiagaraDataBuffer* DataBuffer = DataSet->GetCurrentData();
			if (DataBuffer && Index >= 0 && (uint32)Index < DataBuffer->GetNumInstances())
			{
				return Get(Index);
			}
			else
			{
				ensure(DataBuffer && Index >= 0 && (uint32)Index < DataBuffer->GetNumInstances()); // just to capture the badness in the logs or debugger if attached.
			}
		}

		return Default;
	}

	FORCEINLINE float Get(int32 Index)const
	{
		float Ret;
		Get(Index, Ret);
		return Ret;
	}

	FORCEINLINE void Get(int32 Index, float& OutValue)const
	{
		checkSlow(IsValidForRead());
		OutValue = SrcBase[Index];
	}

	FORCEINLINE void Set(int32 Index, const float& InValue)
	{
		checkSlow(IsValidForWrite());
		DestBase[Index] = InValue;
	}

private:
	float* SrcBase;
	float* DestBase;
};

template<>
struct FNiagaraDataSetAccessor<FVector2D> : public FNiagaraDataSetAccessorBase
{
	FNiagaraDataSetAccessor<FVector2D>() {}
	FNiagaraDataSetAccessor<FVector2D>(FNiagaraDataSet& InDataSet, FNiagaraVariable InVar)
		: FNiagaraDataSetAccessorBase(&InDataSet, InVar)
	{
		check(sizeof(FVector2D) == InVar.GetType().GetSize());
		InitForAccess();
	}

	void InitForAccess()
	{
		SrcXBase = nullptr;
		SrcYBase = nullptr;
		DestXBase = nullptr;
		DestYBase = nullptr;
		FNiagaraDataBuffer* SrcBuffer = DataSet->GetCurrentData();
		if (IsValid() && SrcBuffer)
		{
			SrcXBase = (float*)SrcBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart);
			SrcYBase = (float*)SrcBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart + 1);

			//Writes are only valid if we're during a simulation pass.
			FNiagaraDataBuffer* DestBuffer = DataSet->GetDestinationData();
			if (DestBuffer)
			{
				DestXBase = (float*)DestBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart);
				DestYBase = (float*)DestBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart + 1);
			}
		}
	}

	FORCEINLINE bool IsValidForRead()const { return SrcXBase != nullptr && SrcYBase != nullptr; }
	FORCEINLINE bool IsValidForWrite()const { return DestXBase != nullptr && DestYBase != nullptr; }

	FORCEINLINE FVector2D operator[](int32 Index)const
	{
		return Get(Index);
	}

	FORCEINLINE FVector2D GetSafe(int32 Index, FVector2D Default = FVector2D::ZeroVector)const
	{
		if (IsValidForRead())
		{
			FNiagaraDataBuffer* DataBuffer = DataSet->GetCurrentData();
			if (DataBuffer && Index >= 0 && (uint32)Index < DataBuffer->GetNumInstances())
			{
				return Get(Index);
			}
			else
			{
				ensure(DataBuffer && Index >= 0 && (uint32)Index < DataBuffer->GetNumInstances()); // just to capture the badness in the logs or debugger if attached.
			}
		}

		return Default;
	}

	FORCEINLINE FVector2D Get(int32 Index)const
	{
		FVector2D Ret;
		Get(Index, Ret);
		return Ret;
	}

	FORCEINLINE void Get(int32 Index, FVector2D& OutValue)const
	{
		checkSlow(IsValidForRead());
		OutValue.X = SrcXBase[Index];
		OutValue.Y = SrcYBase[Index];
	}

	FORCEINLINE void Set(int32 Index, const FVector2D& InValue)
	{
		checkSlow(IsValidForWrite());
		DestXBase[Index] = InValue.X;
		DestYBase[Index] = InValue.Y;
	}

private:

	float* SrcXBase;
	float* SrcYBase;
	float* DestXBase;
	float* DestYBase;
};

template<>
struct FNiagaraDataSetAccessor<FVector> : public FNiagaraDataSetAccessorBase
{
	FNiagaraDataSetAccessor<FVector>() {}
	FNiagaraDataSetAccessor<FVector>(FNiagaraDataSet& InDataSet, FNiagaraVariable InVar)
		: FNiagaraDataSetAccessorBase(&InDataSet, InVar)
	{
		check(sizeof(FVector) == InVar.GetType().GetSize());
		InitForAccess();
	}

	void InitForAccess()
	{
		SrcXBase = nullptr;
		SrcYBase = nullptr;
		SrcZBase = nullptr;
		DestXBase = nullptr;
		DestYBase = nullptr;
		DestZBase = nullptr;
		FNiagaraDataBuffer* SrcBuffer = DataSet->GetCurrentData();
		if (IsValid() && SrcBuffer)
		{
			SrcXBase = (float*)SrcBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart);
			SrcYBase = (float*)SrcBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart + 1);
			SrcZBase = (float*)SrcBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart + 2);

			//Writes are only valid if we're during a simulation pass.
			FNiagaraDataBuffer* DestBuffer = DataSet->GetDestinationData();
			if (DestBuffer)
			{
				DestXBase = (float*)DestBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart);
				DestYBase = (float*)DestBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart + 1);
				DestZBase = (float*)DestBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart + 2);
			}
		}
	}

	FORCEINLINE bool IsValidForRead()const { return SrcXBase != nullptr && SrcYBase != nullptr && SrcZBase != nullptr; }
	FORCEINLINE bool IsValidForWrite()const { return DestXBase != nullptr && DestYBase != nullptr && DestZBase != nullptr; }

	FORCEINLINE FVector operator[](int32 Index)const
	{
		return Get(Index);
	}

	FORCEINLINE FVector GetSafe(int32 Index, FVector Default = FVector::ZeroVector)const
	{
		if (IsValidForRead())
		{
			FNiagaraDataBuffer* DataBuffer = DataSet->GetCurrentData();
			if (DataBuffer && Index >= 0 && (uint32)Index < DataBuffer->GetNumInstances())
			{
				return Get(Index);
			}
			else
			{
				ensure(DataBuffer && Index >= 0 && (uint32)Index < DataBuffer->GetNumInstances()); // just to capture the badness in the logs or debugger if attached.
			}
		}

		return Default;
	}

	FORCEINLINE FVector Get(int32 Index)const
	{
		FVector Ret;
		Get(Index, Ret);
		return Ret;
	}

	FORCEINLINE void Get(int32 Index, FVector& OutValue)const
	{
		checkSlow(IsValidForRead());
		OutValue.X = SrcXBase[Index];
		OutValue.Y = SrcYBase[Index];
		OutValue.Z = SrcZBase[Index];
	}

	FORCEINLINE void Set(int32 Index, const FVector& InValue)
	{
		checkSlow(IsValidForWrite());
		DestXBase[Index] = InValue.X;
		DestYBase[Index] = InValue.Y;
		DestZBase[Index] = InValue.Z;
	}

private:

	float* SrcXBase;
	float* SrcYBase;
	float* SrcZBase;
	float* DestXBase;
	float* DestYBase;
	float* DestZBase;
};

template<>
struct FNiagaraDataSetAccessor<FVector4> : public FNiagaraDataSetAccessorBase
{
	FNiagaraDataSetAccessor<FVector4>() {}
	FNiagaraDataSetAccessor<FVector4>(FNiagaraDataSet& InDataSet, FNiagaraVariable InVar)
		: FNiagaraDataSetAccessorBase(&InDataSet, InVar)
	{
		check(sizeof(FVector4) == InVar.GetType().GetSize());
		InitForAccess();
	}

	void InitForAccess()
	{
		SrcXBase = nullptr;
		SrcYBase = nullptr;
		SrcZBase = nullptr;
		SrcWBase = nullptr;
		DestXBase = nullptr;
		DestYBase = nullptr;
		DestZBase = nullptr;
		DestWBase = nullptr;
		FNiagaraDataBuffer* SrcBuffer = DataSet->GetCurrentData();
		if (IsValid() && SrcBuffer)
		{
			SrcXBase = (float*)SrcBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart);
			SrcYBase = (float*)SrcBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart + 1);
			SrcZBase = (float*)SrcBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart + 2);
			SrcWBase = (float*)SrcBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart + 3);

			//Writes are only valid if we're during a simulation pass.
			FNiagaraDataBuffer* DestBuffer = DataSet->GetDestinationData();
			if (DestBuffer)
			{
				DestXBase = (float*)DestBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart);
				DestYBase = (float*)DestBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart + 1);
				DestZBase = (float*)DestBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart + 2);
				DestWBase = (float*)DestBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart + 3);
			}
		}
	}

	FORCEINLINE bool IsValidForRead()const { return SrcXBase != nullptr && SrcYBase != nullptr && SrcZBase != nullptr && SrcWBase != nullptr; }
	FORCEINLINE bool IsValidForWrite()const { return DestXBase != nullptr && DestYBase != nullptr && DestZBase != nullptr && DestWBase != nullptr; }

	FORCEINLINE FVector4 operator[](int32 Index)const
	{
		return Get(Index);
	}

	FORCEINLINE FVector4 GetSafe(int32 Index, const FVector4& Default = FVector4(0.0f, 0.0f, 0.0f, 0.0f))const
	{
		if (IsValidForRead())
		{
			FNiagaraDataBuffer* DataBuffer = DataSet->GetCurrentData();
			if (DataBuffer && Index >= 0 && (uint32)Index < DataBuffer->GetNumInstances())
			{
				return Get(Index);
			}
			else
			{
				ensure(DataBuffer && Index >= 0 && (uint32)Index < DataBuffer->GetNumInstances()); // just to capture the badness in the logs or debugger if attached.
			}
		}

		return Default;
	}

	FORCEINLINE FVector4 Get(int32 Index)const
	{
		FVector4 Ret;
		Get(Index, Ret);
		return Ret;
	}

	FORCEINLINE void Get(int32 Index, FVector4& OutValue)const
	{
		checkSlow(IsValidForRead());
		OutValue.X = SrcXBase[Index];
		OutValue.Y = SrcYBase[Index];
		OutValue.Z = SrcZBase[Index];
		OutValue.W = SrcWBase[Index];
	}

	FORCEINLINE void Set(int32 Index, const FVector4& InValue)
	{
		checkSlow(IsValidForWrite());
		DestXBase[Index] = InValue.X;
		DestYBase[Index] = InValue.Y;
		DestZBase[Index] = InValue.Z;
		DestWBase[Index] = InValue.W;
	}

private:

	float* SrcXBase;
	float* SrcYBase;
	float* SrcZBase;
	float* SrcWBase;
	float* DestXBase;
	float* DestYBase;
	float* DestZBase;
	float* DestWBase;
};


template<>
struct FNiagaraDataSetAccessor<FQuat> : public FNiagaraDataSetAccessorBase
{
	FNiagaraDataSetAccessor<FQuat>() {}
	FNiagaraDataSetAccessor<FQuat>(FNiagaraDataSet& InDataSet, FNiagaraVariable InVar)
		: FNiagaraDataSetAccessorBase(&InDataSet, InVar)
	{
		check(sizeof(FQuat) == InVar.GetType().GetSize());
		InitForAccess();
	}

	void InitForAccess()
	{
		SrcXBase = nullptr;
		SrcYBase = nullptr;
		SrcZBase = nullptr;
		SrcWBase = nullptr;
		DestXBase = nullptr;
		DestYBase = nullptr;
		DestZBase = nullptr;
		DestWBase = nullptr;
		FNiagaraDataBuffer* SrcBuffer = DataSet->GetCurrentData();
		if (IsValid() && SrcBuffer)
		{
			SrcXBase = (float*)SrcBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart);
			SrcYBase = (float*)SrcBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart + 1);
			SrcZBase = (float*)SrcBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart + 2);
			SrcWBase = (float*)SrcBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart + 3);

			FNiagaraDataBuffer* DestBuffer = DataSet->GetDestinationData();
			if (DestBuffer)
			{
				//Writes are only valid if we're during a simulation pass.
				DestXBase = (float*)DestBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart);
				DestYBase = (float*)DestBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart + 1);
				DestZBase = (float*)DestBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart + 2);
				DestWBase = (float*)DestBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart + 3);
			}
		}
	}

	FORCEINLINE bool IsValidForRead()const { return SrcXBase != nullptr && SrcYBase != nullptr && SrcZBase != nullptr && SrcWBase != nullptr; }
	FORCEINLINE bool IsValidForWrite()const { return DestXBase != nullptr && DestYBase != nullptr && DestZBase != nullptr && DestWBase != nullptr; }

	FORCEINLINE FQuat operator[](int32 Index)const
	{
		return Get(Index);
	}

	FORCEINLINE FQuat GetSafe(int32 Index, const FQuat& Default = FQuat(0.0f, 0.0f, 0.0f, 1.0f))const
	{
		if (IsValidForRead())
		{
			FNiagaraDataBuffer* DataBuffer = DataSet->GetCurrentData();
			if (DataBuffer && Index >= 0 && (uint32)Index < DataBuffer->GetNumInstances())
			{
				return Get(Index);
			}
			else
			{
				ensure(DataBuffer && Index >= 0 && (uint32)Index < DataBuffer->GetNumInstances()); // just to capture the badness in the logs or debugger if attached.
			}
		}

		return Default;
	}

	FORCEINLINE FQuat Get(int32 Index)const
	{
		FQuat Ret;
		Get(Index, Ret);
		return Ret;
	}

	FORCEINLINE void Get(int32 Index, FQuat& OutValue)const
	{
		checkSlow(IsValidForRead());
		OutValue.X = SrcXBase[Index];
		OutValue.Y = SrcYBase[Index];
		OutValue.Z = SrcZBase[Index];
		OutValue.W = SrcWBase[Index];
	}

	FORCEINLINE void Set(int32 Index, const FQuat& InValue)
	{
		checkSlow(IsValidForWrite());
		DestXBase[Index] = InValue.X;
		DestYBase[Index] = InValue.Y;
		DestZBase[Index] = InValue.Z;
		DestWBase[Index] = InValue.W;
	}

private:

	float* SrcXBase;
	float* SrcYBase;
	float* SrcZBase;
	float* SrcWBase;
	float* DestXBase;
	float* DestYBase;
	float* DestZBase;
	float* DestWBase;
};

template<>
struct FNiagaraDataSetAccessor<FLinearColor> : public FNiagaraDataSetAccessorBase
{
	FNiagaraDataSetAccessor<FLinearColor>() {}
	FNiagaraDataSetAccessor<FLinearColor>(FNiagaraDataSet& InDataSet, FNiagaraVariable InVar)
		: FNiagaraDataSetAccessorBase(&InDataSet, InVar)
	{
		check(sizeof(FLinearColor) == InVar.GetType().GetSize());
		InitForAccess();
	}

	void InitForAccess()
	{
		SrcRBase = nullptr;
		SrcGBase = nullptr;
		SrcBBase = nullptr;
		SrcABase = nullptr;
		DestRBase = nullptr;
		DestGBase = nullptr;
		DestBBase = nullptr;
		DestABase = nullptr;
		FNiagaraDataBuffer* SrcBuffer = DataSet->GetCurrentData();
		if (IsValid() && SrcBuffer)
		{
			SrcRBase = (float*)SrcBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart);
			SrcGBase = (float*)SrcBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart + 1);
			SrcBBase = (float*)SrcBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart + 2);
			SrcABase = (float*)SrcBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart + 3);

			//Writes are only valid if we're during a simulation pass.
			FNiagaraDataBuffer* DestBuffer = DataSet->GetDestinationData();
			if (DestBuffer)
			{
				DestRBase = (float*)DestBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart);
				DestGBase = (float*)DestBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart + 1);
				DestBBase = (float*)DestBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart + 2);
				DestABase = (float*)DestBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart + 3);
			}
		}
	}

	FORCEINLINE bool IsValidForRead()const { return SrcRBase != nullptr && SrcGBase != nullptr && SrcBBase != nullptr && SrcABase != nullptr; }
	FORCEINLINE bool IsValidForWrite()const { return DestRBase != nullptr && DestGBase != nullptr && DestBBase != nullptr && DestABase != nullptr; }

	FORCEINLINE FLinearColor operator[](int32 Index)const
	{
		return Get(Index);
	}

	FORCEINLINE FLinearColor GetSafe(int32 Index, FLinearColor Default = FLinearColor::White)const
	{
		if (IsValidForRead())
		{
			FNiagaraDataBuffer* DataBuffer = DataSet->GetCurrentData();
			if (DataBuffer && Index >= 0 && (uint32)Index < DataBuffer->GetNumInstances())
			{
				return Get(Index);
			}
			else
			{
				ensure(DataBuffer && Index >= 0 && (uint32)Index < DataBuffer->GetNumInstances()); // just to capture the badness in the logs or debugger if attached.
			}
		}

		return Default;
	}

	FORCEINLINE FLinearColor Get(int32 Index)const
	{
		FLinearColor Ret;
		Get(Index, Ret);
		return Ret;
	}

	FORCEINLINE void Get(int32 Index, FLinearColor& OutValue)const
	{
		checkSlow(IsValidForRead());
		OutValue.R = SrcRBase[Index];
		OutValue.G = SrcGBase[Index];
		OutValue.B = SrcBBase[Index];
		OutValue.A = SrcABase[Index];
	}

	FORCEINLINE void Set(int32 Index, const FLinearColor& InValue)
	{
		checkSlow(IsValidForWrite());
		DestRBase[Index] = InValue.R;
		DestGBase[Index] = InValue.G;
		DestBBase[Index] = InValue.B;
		DestABase[Index] = InValue.A;
	}

private:

	float* SrcRBase;
	float* SrcGBase;
	float* SrcBBase;
	float* SrcABase;
	float* DestRBase;
	float* DestGBase;
	float* DestBBase;
	float* DestABase;
};

template<>
struct FNiagaraDataSetAccessor<FNiagaraSpawnInfo> : public FNiagaraDataSetAccessorBase
{
	FNiagaraDataSetAccessor<FNiagaraSpawnInfo>() {}
	FNiagaraDataSetAccessor<FNiagaraSpawnInfo>(FNiagaraDataSet& InDataSet, FNiagaraVariable InVar)
		: FNiagaraDataSetAccessorBase(&InDataSet, InVar)
	{
		check(sizeof(FNiagaraSpawnInfo) == InVar.GetType().GetSize());
		InitForAccess();
	}

	void InitForAccess()
	{
		SrcCountBase = nullptr;
		SrcInterpStartDtBase = nullptr;
		SrcIntervalDtBase = nullptr;
		SrcGroupBase = nullptr;
		DestCountBase = nullptr;
		DestInterpStartDtBase = nullptr;
		DestIntervalDtBase = nullptr;
		DestGroupBase = nullptr;
		FNiagaraDataBuffer* SrcBuffer = DataSet->GetCurrentData();
		if (IsValid() && SrcBuffer)
		{
			SrcCountBase = (int32*)SrcBuffer->GetComponentPtrInt32(VarLayout->Int32ComponentStart);
			SrcInterpStartDtBase = (float*)SrcBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart);
			SrcIntervalDtBase = (float*)SrcBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart + 1);
			SrcGroupBase = (int32*)SrcBuffer->GetComponentPtrInt32(VarLayout->Int32ComponentStart + 1);

			//Writes are only valid if we're during a simulation pass.
			FNiagaraDataBuffer* DestBuffer = DataSet->GetDestinationData();
			if (DestBuffer)
			{
				DestCountBase = (int32*)DestBuffer->GetComponentPtrInt32(VarLayout->Int32ComponentStart);
				DestInterpStartDtBase = (float*)DestBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart);
				DestIntervalDtBase = (float*)DestBuffer->GetComponentPtrFloat(VarLayout->FloatComponentStart + 1);
				DestGroupBase = (int32*)DestBuffer->GetComponentPtrInt32(VarLayout->Int32ComponentStart + 1);
			}
		}
	}

	FORCEINLINE bool IsValidForRead()const { return SrcCountBase != nullptr && SrcInterpStartDtBase != nullptr && SrcIntervalDtBase != nullptr && SrcGroupBase != nullptr; }
	FORCEINLINE bool IsValidForWrite()const { return DestCountBase != nullptr && DestInterpStartDtBase != nullptr && DestIntervalDtBase != nullptr && DestGroupBase != nullptr; }

	FORCEINLINE FNiagaraSpawnInfo operator[](int32 Index)const
	{
		return Get(Index);
	}

	FORCEINLINE FNiagaraSpawnInfo GetSafe(int32 Index, FNiagaraSpawnInfo Default = FNiagaraSpawnInfo())const
	{
		if (IsValidForRead())
		{
			FNiagaraDataBuffer* DataBuffer = DataSet->GetCurrentData();
			if (DataBuffer && Index >= 0 && (uint32)Index < DataBuffer->GetNumInstances())
			{
				return Get(Index);
			}
			else
			{
				ensure(DataBuffer && Index >= 0 && (uint32)Index < DataBuffer->GetNumInstances()); // just to capture the badness in the logs or debugger if attached.
			}
		}

		return Default;
	}

	FORCEINLINE FNiagaraSpawnInfo Get(int32 Index)const
	{
		FNiagaraSpawnInfo Ret;
		Get(Index, Ret);
		return Ret;
	}

	FORCEINLINE void Get(int32 Index, FNiagaraSpawnInfo& OutValue)const
	{
		checkSlow(IsValidForRead());
		OutValue.Count = SrcCountBase[Index];
		OutValue.InterpStartDt = SrcInterpStartDtBase[Index];
		OutValue.IntervalDt = SrcIntervalDtBase[Index];
		OutValue.SpawnGroup = SrcGroupBase[Index];
	}

	FORCEINLINE void Set(int32 Index, const FNiagaraSpawnInfo& InValue)
	{
		checkSlow(IsValidForWrite());
		DestCountBase[Index] = InValue.Count;
		DestInterpStartDtBase[Index] = InValue.InterpStartDt;
		DestIntervalDtBase[Index] = InValue.IntervalDt;
		DestGroupBase[Index] = InValue.SpawnGroup;
	}

private:

	int32* SrcCountBase;
	float* SrcInterpStartDtBase;
	float* SrcIntervalDtBase;
	int32* SrcGroupBase;

	int32* DestCountBase;
	float* DestInterpStartDtBase;
	float* DestIntervalDtBase;
	int32* DestGroupBase;
};

template<>
struct FNiagaraDataSetAccessor<FNiagaraID> : public FNiagaraDataSetAccessorBase
{
	FNiagaraDataSetAccessor<FNiagaraID>() {}
	FNiagaraDataSetAccessor<FNiagaraID>(FNiagaraDataSet& InDataSet, FNiagaraVariable InVar)
		: FNiagaraDataSetAccessorBase(&InDataSet, InVar)
	{
		InitForAccess();
	}

	void InitForAccess()
	{
		SrcIndexBase = nullptr;
		SrcTagBase = nullptr;
		DestIndexBase = nullptr;
		DestTagBase = nullptr;
		FNiagaraDataBuffer* SrcBuffer = DataSet->GetCurrentData();
		if (IsValid() && SrcBuffer)
		{
			SrcIndexBase = (int32*)SrcBuffer->GetComponentPtrInt32(VarLayout->Int32ComponentStart);
			SrcTagBase = (int32*)SrcBuffer->GetComponentPtrInt32(VarLayout->Int32ComponentStart + 1);

			//Writes are only valid if we're during a simulation pass.
			FNiagaraDataBuffer* DestBuffer = DataSet->GetDestinationData();
			if (DestBuffer)
			{
				DestIndexBase = (int32*)DestBuffer->GetComponentPtrInt32(VarLayout->Int32ComponentStart);
				DestTagBase = (int32*)DestBuffer->GetComponentPtrInt32(VarLayout->Int32ComponentStart + 1);
			}
		}
	}

	FORCEINLINE bool IsValidForRead()const { return SrcIndexBase != nullptr && SrcTagBase != nullptr; }
	FORCEINLINE bool IsValidForWrite()const { return DestIndexBase != nullptr && DestTagBase != nullptr; }

	FORCEINLINE FNiagaraID operator[](int32 Index)const
	{
		return Get(Index);
	}

	FORCEINLINE FNiagaraID GetSafe(int32 Index, FNiagaraID Default = FNiagaraID())const
	{
		if (IsValidForRead())
		{
			FNiagaraDataBuffer* DataBuffer = DataSet->GetCurrentData();
			if (DataBuffer && Index >= 0 && (uint32)Index < DataBuffer->GetNumInstances())
			{
				return Get(Index);
			}
			else
			{
				ensure(DataBuffer && Index >= 0 && (uint32)Index < DataBuffer->GetNumInstances()); // just to capture the badness in the logs or debugger if attached.
			}
		}

		return Default;
	}

	FORCEINLINE FNiagaraID Get(int32 Index)const
	{
		FNiagaraID Ret;
		Get(Index, Ret);
		return Ret;
	}

	FORCEINLINE void Get(int32 Index, FNiagaraID& OutValue)const
	{
		checkSlow(IsValidForRead());
		OutValue.Index = SrcIndexBase[Index];
		OutValue.AcquireTag = SrcTagBase[Index];
	}

	FORCEINLINE void Set(int32 Index, const FNiagaraID& InValue)
	{
		checkSlow(IsValidForWrite());
		DestIndexBase[Index] = InValue.Index;
		DestTagBase[Index] = InValue.AcquireTag;
	}

private:

	int32* SrcIndexBase;
	int32* SrcTagBase;
	int32* DestIndexBase;
	int32* DestTagBase;
};

/**
Iterator that will pull or push data between a NiagaraDataBuffer and some FNiagaraVariables it contains.
Super slow. Don't use at runtime.
*/
struct FNiagaraDataVariableIterator
{
	FNiagaraDataVariableIterator(const FNiagaraDataBuffer* InData, uint32 StartIdx = 0)
		: Data(InData)
		, CurrIdx(StartIdx)
	{
		Variables = Data->GetOwner()->GetVariables();
	}

	void Get()
	{
		const TArray<FNiagaraVariableLayoutInfo>& VarLayouts = Data->GetOwner()->GetVariableLayouts();
		for (int32 VarIdx = 0; VarIdx < Variables.Num(); ++VarIdx)
		{
			FNiagaraVariable& Var = Variables[VarIdx];
			const FNiagaraVariableLayoutInfo& Layout = VarLayouts[VarIdx];
			Var.AllocateData();
			const uint8* ValuePtr = Var.GetData();

			for (uint32 CompIdx = 0; CompIdx < Layout.GetNumFloatComponents(); ++CompIdx)
			{
				uint32 CompBufferOffset = Layout.FloatComponentStart + CompIdx;
				float* Src = Data->GetInstancePtrFloat(CompBufferOffset, CurrIdx);
				float* Dst = (float*)(ValuePtr + Layout.LayoutInfo.FloatComponentByteOffsets[CompIdx]);
				*Dst = *Src;
			}

			for (uint32 CompIdx = 0; CompIdx < Layout.GetNumInt32Components(); ++CompIdx)
			{
				uint32 CompBufferOffset = Layout.Int32ComponentStart + CompIdx;
				int32* Src = Data->GetInstancePtrInt32(CompBufferOffset, CurrIdx);
				int32* Dst = (int32*)(ValuePtr + Layout.LayoutInfo.Int32ComponentByteOffsets[CompIdx]);
				*Dst = *Src;
			}
		}
	}

	void Advance() { ++CurrIdx; }
	bool IsValid()const { return Data && CurrIdx < Data->GetNumInstances(); }
	uint32 GetCurrIndex()const { return CurrIdx; }
	const TArray<FNiagaraVariable>& GetVariables()const { return Variables; }
private:

	const FNiagaraDataBuffer* Data;
	TArray<FNiagaraVariable> Variables;

	uint32 CurrIdx;
};

/**
Allows immediate access to GPU data on the CPU, you can then use FNiagaraDataSetAccessor to access the data.
This will make a copy of the GPU data and will stall the CPU until the data is ready from the GPU,
therefore it should only be used for tools / debugging.  For async readback see FNiagaraSystemInstance::RequestCapture.
*/
class NIAGARA_API FScopedNiagaraDataSetGPUReadback
{
public:
	FScopedNiagaraDataSetGPUReadback() {}
	FORCEINLINE ~FScopedNiagaraDataSetGPUReadback()
	{
		if (DataBuffer != nullptr)
		{
			DataBuffer->FloatData.Empty();
			DataBuffer->Int32Data.Empty();
		}
	}

	void ReadbackData(class NiagaraEmitterInstanceBatcher* Batcher, FNiagaraDataSet* InDataSet);
	uint32 GetNumInstances() const { check(DataSet != nullptr); return NumInstances; }

private:
	FNiagaraDataSet*	DataSet = nullptr;
	FNiagaraDataBuffer* DataBuffer = nullptr;
	NiagaraEmitterInstanceBatcher* Batcher = nullptr;
	uint32				NumInstances = 0;
};


//////////////////////////////////////////////////////////////////////////

FORCEINLINE void FNiagaraDataBuffer::CheckUsage(bool bReadOnly)const
{
	checkSlow(Owner);

	//We can read on the RT but any modifications must be GT (or GT Task).
	//For GPU sims we must be on the RT.
	checkSlow((Owner->GetSimTarget() == ENiagaraSimTarget::CPUSim  && (IsInGameThread() || (bReadOnly || !IsInRenderingThread()))) ||
		(Owner->GetSimTarget() == ENiagaraSimTarget::GPUComputeSim) && IsInRenderingThread());
}
