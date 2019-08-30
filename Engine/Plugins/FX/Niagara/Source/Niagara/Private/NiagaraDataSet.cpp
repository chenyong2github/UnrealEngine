// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataSet.h"
#include "NiagaraCommon.h"
#include "NiagaraShader.h"
#include "GlobalShader.h"
#include "UpdateTextureShaders.h"
#include "ShaderParameterUtils.h"
#include "NiagaraStats.h"
#include "NiagaraRenderer.h"
#include "NiagaraGPUInstanceCountManager.h"
#include "NiagaraEmitterInstanceBatcher.h"

DECLARE_CYCLE_STAT(TEXT("InitRenderData"), STAT_InitRenderData, STATGROUP_Niagara);

//////////////////////////////////////////////////////////////////////////

FCriticalSection FNiagaraSharedObject::CritSec;
TArray<FNiagaraSharedObject*> FNiagaraSharedObject::DeferredDeletionList;
void FNiagaraSharedObject::Destroy()
{
	FScopeLock Lock(&CritSec);
	check(this != nullptr);
	check(DeferredDeletionList.Contains(this) == false);
	DeferredDeletionList.Add(this);
}

void FNiagaraSharedObject::FlushDeletionList()
{
	//Always do this on RT. GPU buffers must be freed on RT and we may as well do CPU frees at the same time.
	ENQUEUE_RENDER_COMMAND(FlushDeletionListCommand)([&](FRHICommandListImmediate& RHICmdList)
	{
		FScopeLock Lock(&CritSec);//Possibly make this a lock free queue?
		int32 i = 0;
		while (i < DeferredDeletionList.Num())
		{
			check(DeferredDeletionList[i] != nullptr);
			if (DeferredDeletionList[i]->IsInUse() == false)
			{
				delete DeferredDeletionList[i];
				DeferredDeletionList.RemoveAtSwap(i);
			}
			else
			{
				++i;
			}
		}
	});
}

//////////////////////////////////////////////////////////////////////////
static int32 GNiagaraDataBufferMinSize = 512;
static FAutoConsoleVariableRef CVarRenderDataBlockSize(
	TEXT("fx.NiagaraDataBufferMinSize"),
	GNiagaraDataBufferMinSize,
	TEXT("Niagara data buffer minimum allocation size in bytes (Default=512). \n"),
	ECVF_Default
);

static int32 GNiagaraDataBufferShrinkFactor = 3;
static FAutoConsoleVariableRef CVarNiagaraRenderBufferShrinkFactor(
	TEXT("fx.NiagaraDataBufferShrinkFactor"),
	GNiagaraDataBufferShrinkFactor,
	TEXT("Niagara data buffer size threshold for shrinking. (Default=3) \n")
	TEXT("The buffer will be reallocated when the used size becomes 1/F of the allocated size. \n"),
	ECVF_Default
);

FNiagaraDataSet::FNiagaraDataSet()
	: TotalFloatComponents(0)
	, TotalInt32Components(0)
	, SimTarget(ENiagaraSimTarget::CPUSim)
	, bFinalized(0)
	, bNeedsPersistentIDs(0)
	, NumFreeIDs(0)
	, MaxUsedID(0)
	, IDAcquireTag(0)
	, CurrentData(nullptr)
	, DestinationData(nullptr)
{
}

FNiagaraDataSet::~FNiagaraDataSet()
{
// 	int32 CurrBytes = RenderDataFloat.NumBytes + RenderDataInt.NumBytes;
// 	DEC_MEMORY_STAT_BY(STAT_NiagaraVBMemory, CurrBytes);
	ReleaseBuffers();
}

void FNiagaraDataSet::Init(FNiagaraDataSetID InID, ENiagaraSimTarget InSimTarget, const FString& InDebugName)
{
	Reset();
	ID = InID;
	SimTarget = InSimTarget;
	DebugName = InDebugName;
}

void FNiagaraDataSet::Reset()
{
	ResetBuffers();

	Variables.Empty();
	VariableLayouts.Empty();
	bFinalized = false;
	TotalFloatComponents = 0;
	TotalInt32Components = 0;
	bNeedsPersistentIDs = 0;
}

void FNiagaraDataSet::ResetBuffers()
{
	if (SimTarget == ENiagaraSimTarget::CPUSim)
	{
		ResetBuffersInternal();
	}
	else
	{
		check(SimTarget == ENiagaraSimTarget::GPUComputeSim);
		ENQUEUE_RENDER_COMMAND(ResetBuffersCommand)([=](FRHICommandListImmediate& RHICmdList)
		{
			ResetBuffersInternal();
		});
	}
}

void FNiagaraDataSet::ResetBuffersInternal()
{
	CheckCorrectThread();

	CurrentData = nullptr;
	DestinationData = nullptr;

	FreeIDsTable.Reset();
	NumFreeIDs = 0;
	MaxUsedID = INDEX_NONE;

	//Ensure we have a valid current buffer
	BeginSimulate();
	EndSimulate();
}

void FNiagaraDataSet::ReleaseBuffers()
{
	CheckCorrectThread();
	if (Data.Num() > 0)
	{
		for (FNiagaraDataBuffer* Buffer : Data)
		{
			Buffer->Destroy();
		}
		Data.Empty();
	}
}

FNiagaraDataBuffer& FNiagaraDataSet::BeginSimulate()
{
	//CheckCorrectThread();
	check(DestinationData == nullptr);

	//Find a free buffer we can write into.
	//Linear search but there should only be 2 or three entries.
	for (FNiagaraDataBuffer* Buffer : Data)
	{
		check(Buffer);
		if (Buffer != CurrentData && Buffer->TryLock())
		{
			DestinationData = Buffer;
			break;
		}
	}

	if (DestinationData == nullptr)
	{
		Data.Add(new FNiagaraDataBuffer(this));
		DestinationData = Data.Last();
		verify(DestinationData->TryLock());
		check(DestinationData->IsBeingWritten());
	}

	DestinationData->SetNumInstances(0);
	DestinationData->GetIDTable().Reset();

	return GetDestinationDataChecked();
}

void FNiagaraDataSet::EndSimulate(bool SetCurrentData)
{
	//CheckCorrectThread();
	//Destination is now complete so make it the current simulation state.
	DestinationData->Unlock();
	check(!DestinationData->IsInUse());

	if (SetCurrentData)
	{
		CurrentData = DestinationData;
	}

	DestinationData = nullptr;
}


void FNiagaraDataSet::Allocate(int32 NumInstances, bool bMaintainExisting)
{
	check(bFinalized);
	CheckCorrectThread();
	check(DestinationData);

	DestinationData->GetIDTable().Reset();
	if (bMaintainExisting)
	{
		CurrentData->CopyTo(*DestinationData);
	}

	DestinationData->Allocate(NumInstances, bMaintainExisting);

#if NIAGARA_NAN_CHECKING
	CheckForNaNs();
#endif

	if (bNeedsPersistentIDs)
	{
		TArray<int32>& CurrentIDTable = CurrentData->GetIDTable();
		TArray<int32>& DestinationIDTable = DestinationData->GetIDTable();

		int32 NumUsedIDs = MaxUsedID + 1;

		int32 RequiredIDs = FMath::Max(NumInstances, NumUsedIDs);
		int32 ExistingNumIDs = CurrentIDTable.Num();
		int32 NumNewIDs = RequiredIDs - ExistingNumIDs;

		//////////////////////////////////////////////////////////////////////////
		//TODO: We should replace this with a lock free list that uses just a single table with RequiredIDs elements.
		//Unused slots in the array can form a linked list so that we need only one array with a Head index for the FreeID list
		//This will be faster and likely simpler than the current implementation while also working on GPU.
		//////////////////////////////////////////////////////////////////////////
		if (RequiredIDs > ExistingNumIDs)
		{
			//UE_LOG(LogNiagara, Warning, TEXT("Growing ID Table! OldSize:%d | NewSize:%d"), ExistingNumIDs, RequiredIDs);
			int32 NewNumIds = RequiredIDs - ExistingNumIDs;
#if 0
			DestinationIDTable.SetNumUninitialized(RequiredIDs);
#else
			while (DestinationIDTable.Num() < RequiredIDs)
			{
				DestinationIDTable.Add(INDEX_NONE);
			}
#endif

			//Free ID Table must always be at least as large as the data buffer + it's current size in the case all particles die this frame.
			FreeIDsTable.AddUninitialized(NumNewIDs);

			//Free table should always have enough room for these new IDs.
			check(NumFreeIDs + NumNewIDs <= FreeIDsTable.Num());

			//ID Table grows so add any new IDs to the free array. Add in reverse order to maintain a continuous increasing allocation when popping.
			for (int32 NewFreeID = RequiredIDs - 1; NewFreeID >= ExistingNumIDs; --NewFreeID)
			{
				FreeIDsTable[NumFreeIDs++] = NewFreeID;
			}
			//UE_LOG(LogNiagara, Warning, TEXT("DataSetAllocate: Adding New Free IDs: %d - "), NewNumIds);
		}
#if 0
		else if (RequiredIDs < ExistingNumIDs >> 1)//Configurable?
		{
			//If the max id we use has reduced significantly then we can shrink the tables.
			//Have to go through the FreeIDs and remove any that are greater than the new table size.
			//UE_LOG(LogNiagara, Warning, TEXT("DataSetAllocate: Shrinking ID Table! OldSize:%d | NewSize:%d"), ExistingNumIDs, RequiredIDs);
			for (int32 CheckedFreeID = 0; CheckedFreeID < NumFreeIDs;)
			{
				checkSlow(NumFreeIDs <= FreeIDsTable.Num());
				if (FreeIDsTable[CheckedFreeID] >= RequiredIDs)
				{
					//UE_LOG(LogNiagara, Warning, TEXT("RemoveSwap FreeID: Removed:%d | Swapped:%d"), FreeIDsTable[CheckedFreeID], FreeIDsTable.Last());		
					int32 FreeIDIndex = --NumFreeIDs;
					FreeIDsTable[CheckedFreeID] = FreeIDsTable[FreeIDIndex];
					FreeIDsTable[FreeIDIndex] = INDEX_NONE;
				}
				else
				{
					++CheckedFreeID;
				}
			}

			check(NumFreeIDs <= RequiredIDs);
			FreeIDsTable.SetNumUninitialized(NumFreeIDs);
		}
#endif
		else
		{
			//Drop in required size not great enough so just allocate same size.
			RequiredIDs = ExistingNumIDs;
		}

		DestinationIDTable.SetNumUninitialized(RequiredIDs);
		MaxUsedID = INDEX_NONE;//reset the max ID ready for it to be filled in during simulation.

// 		UE_LOG(LogNiagara, Warning, TEXT("DataSetAllocate: NumInstances:%d | ID Table Size:%d | NumFreeIDs:%d | FreeTableSize:%d"), NumInstances, DestinationData->GetIDTable().Num(), NumFreeIDs, FreeIDsTable.Num());
// 		UE_LOG(LogNiagara, Warning, TEXT("== FreeIDs %d =="), NumFreeIDs);
// 		for (int32 i=0; i< NumFreeIDs;++i)
// 		{
// 			UE_LOG(LogNiagara, Warning, TEXT("%d"), FreeIDsTable[i]);
// 		}
	}
}

uint32 FNiagaraDataSet::GetSizeBytes()const
{
	uint32 Size = 0;
	for (FNiagaraDataBuffer* Buffer : Data)
	{
		check(Buffer);
		Size += Buffer->GetSizeBytes();
	}
	return Size;
}

void FNiagaraDataSet::ClearRegisterTable(uint8** Registers, int32& NumRegisters)
{
	for (FNiagaraVariableLayoutInfo& VarLayout : VariableLayouts)
	{
		int32 NumComps = VarLayout.GetNumFloatComponents() + VarLayout.GetNumInt32Components();
		for (int32 CompIdx = 0; CompIdx < NumComps; ++CompIdx)
		{
			Registers[NumRegisters + CompIdx] = nullptr;
		}
		NumRegisters += NumComps;
	}
}


void FNiagaraDataSet::CheckForNaNs()const
{
	for (const FNiagaraDataBuffer* Buffer : Data)
	{
		if (Buffer->CheckForNaNs())
		{
			Buffer->Dump(0, Buffer->GetNumInstances(), TEXT("Found Niagara buffer containing NaNs!"));
			ensureAlwaysMsgf(false, TEXT("NiagaraDataSet contains NaNs!"));
		}
	}
}

void FNiagaraDataSet::Dump(int32 StartIndex, int32 NumInstances, const FString& Label)const
{
	if (CurrentData)
	{
		CurrentData->Dump(StartIndex, NumInstances, Label);
	}

	if (GetDestinationData())
	{
		FString DestLabel = Label + TEXT("[Destination]");
		DestinationData->Dump(StartIndex, NumInstances, DestLabel);
	}
}

void FNiagaraDataSet::ReleaseGPUInstanceCounts(FNiagaraGPUInstanceCountManager& GPUInstanceCountManager)
{
	for (FNiagaraDataBuffer* Buffer : Data)
	{
		Buffer->ReleaseGPUInstanceCount(GPUInstanceCountManager);
	}
}

void FNiagaraDataSet::BuildLayout()
{
	VariableLayouts.Empty();
	TotalFloatComponents = 0;
	TotalInt32Components = 0;

	VariableLayouts.Reserve(Variables.Num());
	for (FNiagaraVariable& Var : Variables)
	{
		FNiagaraVariableLayoutInfo& VarInfo = VariableLayouts[VariableLayouts.AddDefaulted()];
		FNiagaraTypeLayoutInfo::GenerateLayoutInfo(VarInfo.LayoutInfo, Var.GetType().GetScriptStruct());
		VarInfo.FloatComponentStart = TotalFloatComponents;
		VarInfo.Int32ComponentStart = TotalInt32Components;
		TotalFloatComponents += VarInfo.GetNumFloatComponents();
		TotalInt32Components += VarInfo.GetNumInt32Components();
	}
}

void FNiagaraDataSet::AddVariable(FNiagaraVariable& Variable)
{
	check(!bFinalized);
	Variables.AddUnique(Variable);
}

void FNiagaraDataSet::AddVariables(const TArray<FNiagaraVariable>& Vars)
{
	check(!bFinalized);
	for (const FNiagaraVariable& Var : Vars)
	{
		Variables.AddUnique(Var);
	}
}

void FNiagaraDataSet::Finalize()
{
	check(!bFinalized);
	bFinalized = true;
	BuildLayout();

	ResetBuffers();
}

const FNiagaraVariableLayoutInfo* FNiagaraDataSet::GetVariableLayout(const FNiagaraVariable& Var)const
{
	int32 VarLayoutIndex = Variables.IndexOfByKey(Var);
	return VarLayoutIndex != INDEX_NONE ? &VariableLayouts[VarLayoutIndex] : nullptr;
}

bool FNiagaraDataSet::GetVariableComponentOffsets(const FNiagaraVariable& Var, int32 &FloatStart, int32 &IntStart) const
{
	const FNiagaraVariableLayoutInfo *Info = GetVariableLayout(Var);
	if (Info)
	{
		FloatStart = Info->FloatComponentStart;
		IntStart = Info->Int32ComponentStart;
		return true;
	}

	FloatStart = -1;
	IntStart = -1;
	return false;
}

void FNiagaraDataSet::CopyTo(FNiagaraDataSet& Other, int32 StartIdx, int32 NumInstances, bool bResetOther)const
{
	CheckCorrectThread();

	if (bResetOther)
	{
		Other.Reset();
		Other.Variables = Variables;
		Other.VariableLayouts = VariableLayouts;
		Other.TotalFloatComponents = TotalFloatComponents;
		Other.TotalInt32Components = TotalInt32Components;
		Other.Finalize();
	}
	else
	{
		checkSlow(Other.GetVariables() == Variables);
	}

	//Read the most current data. Even if it's possibly partially complete simulation data.
	FNiagaraDataBuffer* SourceBuffer = GetDestinationData() ? GetDestinationData() : GetCurrentData();
	FNiagaraDataBuffer* OtherCurrentBuffer = Other.GetCurrentData();

	if (SourceBuffer != nullptr)
	{
		int32 SourceInstances = SourceBuffer->GetNumInstances();
		int32 OrigNumInstances = OtherCurrentBuffer->GetNumInstances();

		if (StartIdx >= SourceInstances)
		{
			return; //We can't start beyond the end of the source buffer.
		}

		if (NumInstances == INDEX_NONE || StartIdx + NumInstances >= SourceInstances)
		{
			NumInstances = SourceBuffer->GetNumInstances() - StartIdx;
		}

		FNiagaraDataBuffer& OtherDestBuffer = Other.BeginSimulate();

		//We need to allocate enough space for the new data and existing data if we're keeping it.
		int32 RequiredInstances = bResetOther ? NumInstances : NumInstances + OrigNumInstances;
		OtherDestBuffer.Allocate(RequiredInstances);
		OtherDestBuffer.SetNumInstances(RequiredInstances);

		//Copy the data in our current buffer over into the new buffer.
		if (!bResetOther)
		{
			OtherCurrentBuffer->CopyTo(OtherDestBuffer, 0, 0, OtherCurrentBuffer->GetNumInstances());
		}

		//Now copy the data from the source buffer into the newly allocated space.
		SourceBuffer->CopyTo(OtherDestBuffer, 0, OrigNumInstances, NumInstances);

		Other.EndSimulate();
	}
}

void FNiagaraDataSet::CopyFromGPUReadback(float* GPUReadBackFloat, int* GPUReadBackInt, int32 StartIdx /* = 0 */, int32 NumInstances /* = INDEX_NONE */, uint32 FloatStride, uint32 IntStride)
{
	check(IsInRenderingThread());
	check(bFinalized);//We should be finalized with proper layout information already.

	FNiagaraDataBuffer& DestBuffer = BeginSimulate();
	DestBuffer.GPUCopyFrom(GPUReadBackFloat, GPUReadBackInt, StartIdx, NumInstances, FloatStride, IntStride);
	EndSimulate();
}
 
//////////////////////////////////////////////////////////////////////////

FNiagaraDataBuffer::FNiagaraDataBuffer(FNiagaraDataSet* InOwner)
	: Owner(InOwner)
	, GPUInstanceCountBufferOffset(INDEX_NONE)
	, NumChunksAllocatedForGPU(0)
	, NumInstances(0)
	, NumInstancesAllocated(0)
	, FloatStride(0)
	, Int32Stride(0)
{
}

FNiagaraDataBuffer::~FNiagaraDataBuffer()
{
	check(!IsInUse());
	// If this is data for a GPU emitter, we have to release the GPU instance counts for reuse.
	// The only exception is if the batcher was pending kill and we couldn't enqueue a rendering command, 
	// in which case this would have been released on the game thread and not from the batcher DataSetsToDestroy_RT.
	check(!IsInRenderingThread() || GPUInstanceCountBufferOffset == INDEX_NONE);
	DEC_MEMORY_STAT_BY(STAT_NiagaraParticleMemory, FloatData.GetAllocatedSize() + Int32Data.GetAllocatedSize());
}

void FNiagaraDataBuffer::CheckUsage(bool bReadOnly)const
{
	check(Owner);
	if (Owner->SimTarget == ENiagaraSimTarget::CPUSim)
	{
		//We can read on the RT but any modifications must be GT (or GT Task).
		check(IsInGameThread() || (bReadOnly || !IsInRenderingThread()));
	}
	else
	{
		check(Owner->SimTarget == ENiagaraSimTarget::GPUComputeSim);
		//Everything other than init for GPU sims should be done on the RT.
		check(IsInRenderingThread());
	}
}

int32 FNiagaraDataBuffer::TransferInstance(FNiagaraDataBuffer& SourceBuffer, int32 InstanceIndex, bool bRemoveFromSource)
{
	CheckUsage(false);
	if (SourceBuffer.GetNumInstances() > (uint32)InstanceIndex)
	{
		int32 OldNumInstances = NumInstances;
		if (NumInstances == NumInstancesAllocated)
		{
			//Have to allocate some more space.
			Allocate(NumInstancesAllocated + 1, true);
		}

		SetNumInstances(OldNumInstances + 1);

		/** Copy the instance data. */
		for (int32 CompIdx = (int32)Owner->TotalFloatComponents - 1; CompIdx >= 0; --CompIdx)
		{
			float* Src = SourceBuffer.GetInstancePtrFloat(CompIdx, InstanceIndex);
			float* Dst = GetInstancePtrFloat(CompIdx, OldNumInstances);
			*Dst = *Src;
		}
		for (int32 CompIdx = (int32)Owner->TotalInt32Components - 1; CompIdx >= 0; --CompIdx)
		{
			int32* Src = SourceBuffer.GetInstancePtrInt32(CompIdx, InstanceIndex);
			int32* Dst = GetInstancePtrInt32(CompIdx, OldNumInstances);
			*Dst = *Src;
		}

		if (bRemoveFromSource)
		{
			SourceBuffer.KillInstance(InstanceIndex);
		}

		return OldNumInstances;
	}

	return INDEX_NONE;
}

bool FNiagaraDataBuffer::CheckForNaNs()const
{
	CheckUsage(true);
	bool bContainsNaNs = false;
	int32 NumFloatComponents = Owner->GetNumFloatComponents();
	for (int32 CompIdx = 0; CompIdx < NumFloatComponents && !bContainsNaNs; ++CompIdx)
	{
		for (int32 InstIdx = 0; InstIdx < (int32)NumInstances && !bContainsNaNs; ++InstIdx)
		{
			float Val = *GetInstancePtrFloat(CompIdx, InstIdx);
			bContainsNaNs = FMath::IsNaN(Val) || !FMath::IsFinite(Val);
		}
	}

	return bContainsNaNs;
}

void FNiagaraDataBuffer::Allocate(uint32 InNumInstances, bool bMaintainExisting)
{
	//CheckUsage(false);
	check(Owner->SimTarget == ENiagaraSimTarget::CPUSim);

	NumInstancesAllocated = InNumInstances;
	NumInstances = 0;

	DEC_MEMORY_STAT_BY(STAT_NiagaraParticleMemory, FloatData.GetAllocatedSize() + Int32Data.GetAllocatedSize());

	const uint32 OldFloatStride = FloatStride;
	TArray<uint8> OldFloatData;
	const uint32 OldInt32Stride = Int32Stride;
	TArray<uint8> OldIntData;

	if (bMaintainExisting)
	{
		//Need to copy off old data so we can copy it back into the newly laid out buffers. TODO: Avoid this needless copying.
		OldFloatData = FloatData;
		OldIntData = Int32Data;
	}

	FloatStride = GetSafeComponentBufferSize(NumInstancesAllocated * sizeof(float));
	{
		const int32 NewNum = FloatStride * Owner->GetNumFloatComponents();
		const bool bAllowShrink = GNiagaraDataBufferShrinkFactor * FMath::Max(GNiagaraDataBufferMinSize, NewNum) < FloatData.Max() || !NewNum;
		FloatData.SetNum(NewNum, bAllowShrink);
	}

	Int32Stride = GetSafeComponentBufferSize(NumInstancesAllocated * sizeof(int32));
	{
		const int32 NewNum = Int32Stride * Owner->GetNumInt32Components();
		const bool bAllowShrink = GNiagaraDataBufferShrinkFactor * FMath::Max(GNiagaraDataBufferMinSize, NewNum) < Int32Data.Max() || !NewNum;
		Int32Data.SetNum(NewNum, bAllowShrink);
	}

	INC_MEMORY_STAT_BY(STAT_NiagaraParticleMemory, FloatData.GetAllocatedSize() + Int32Data.GetAllocatedSize());

	//In some cases we want the existing data in the buffer to be maintained which due to the data layout requires some fix up.
	if (bMaintainExisting)
	{
		if (FloatStride != OldFloatStride && FloatStride > 0 && OldFloatStride > 0)
		{
			const uint32 BytesToCopy = FMath::Min(OldFloatStride, FloatStride);
			for (int32 CompIdx = (int32)Owner->TotalFloatComponents-1; CompIdx >= 0; --CompIdx)
			{
				uint8* Src = OldFloatData.GetData() + OldFloatStride * CompIdx;
				uint8* Dst = FloatData.GetData() + FloatStride * CompIdx;
				FMemory::Memcpy(Dst, Src, BytesToCopy);
			}
		}
		if (Int32Stride != OldInt32Stride && Int32Stride > 0 && OldInt32Stride > 0)
		{
			const uint32 BytesToCopy = FMath::Min(OldInt32Stride, Int32Stride);
			for (int32 CompIdx = (int32)Owner->TotalInt32Components - 1; CompIdx >= 0; --CompIdx)
			{
				uint8* Src = OldIntData.GetData() + OldInt32Stride * CompIdx;
				uint8* Dst = Int32Data.GetData() + Int32Stride * CompIdx;
				FMemory::Memcpy(Dst, Src, BytesToCopy);
			}
		}
	}
	else
	{
		IDToIndexTable.Reset();
	}
}

void FNiagaraDataBuffer::AllocateGPU(uint32 InNumInstances, FNiagaraGPUInstanceCountManager& GPUInstanceCountManager, FRHICommandList &RHICmdList)
{
	CheckUsage(false);

	check(Owner->SimTarget == ENiagaraSimTarget::GPUComputeSim);

	// Release previous entry if any.
	GPUInstanceCountManager.FreeEntry(GPUInstanceCountBufferOffset);
	// Get a new entry currently set to 0, since simulation will increment it to the actual instance count.
	GPUInstanceCountBufferOffset = GPUInstanceCountManager.AcquireEntry();

	// ALLOC_CHUNKSIZE must be greater than zero and divisible by the thread group size
	const uint32 ALLOC_CHUNKSIZE = 4096;
	static_assert((ALLOC_CHUNKSIZE > 0) && ((ALLOC_CHUNKSIZE % NIAGARA_COMPUTE_THREADGROUP_SIZE) == 0), "ALLOC_CHUNKSIZE must be divisible by NIAGARA_COMPUTE_THREADGROUP_SIZE");

	NumInstancesAllocated = InNumInstances;

	// Round the count up to the nearest threadgroup size
	const uint32 PaddedNumInstances = FMath::DivideAndRoundUp(NumInstancesAllocated, NIAGARA_COMPUTE_THREADGROUP_SIZE) * NIAGARA_COMPUTE_THREADGROUP_SIZE;

	// Pack the data so that the space between elements is the padded thread group size
	FloatStride = PaddedNumInstances * sizeof(float);
	Int32Stride = PaddedNumInstances * sizeof(int32);

	// When the number of elements that we are going to need is greater than the number we have reserved, we need to expand it.
	if (PaddedNumInstances > NumChunksAllocatedForGPU * ALLOC_CHUNKSIZE)
	{
		NumChunksAllocatedForGPU = FMath::DivideAndRoundUp(PaddedNumInstances, ALLOC_CHUNKSIZE);
		const uint32 NumElementsToAlloc = NumChunksAllocatedForGPU * ALLOC_CHUNKSIZE;
		if (NumElementsToAlloc == 0)
		{
			return;
		}

		if (Owner->GetNumFloatComponents())
		{
			if (GPUBufferFloat.Buffer)
			{
				GPUBufferFloat.Release();
			}
			GPUBufferFloat.Initialize(sizeof(float), NumElementsToAlloc * Owner->GetNumFloatComponents(), EPixelFormat::PF_R32_FLOAT, BUF_Static, *Owner->DebugName );
		}
		if (Owner->GetNumInt32Components())
		{
			if (GPUBufferInt.Buffer)
			{
				GPUBufferInt.Release();
			}
			GPUBufferInt.Initialize(sizeof(int32), NumElementsToAlloc * Owner->GetNumInt32Components(), EPixelFormat::PF_R32_SINT, BUF_Static, *Owner->DebugName);
		}
	}
}

void FNiagaraDataBuffer::SwapInstances(uint32 OldIndex, uint32 NewIndex) 
{
	CheckUsage(false);

	for (uint32 CompIdx = 0; CompIdx < Owner->TotalFloatComponents; ++CompIdx)
	{
		float* Src = GetInstancePtrFloat(CompIdx, OldIndex);
		float* Dst = GetInstancePtrFloat(CompIdx, NewIndex);
		float Temp = *Dst;
		*Dst = *Src;
		*Src = Temp;
	}
	for (uint32 CompIdx = 0; CompIdx < Owner->TotalInt32Components; ++CompIdx)
	{
		int32* Src = GetInstancePtrInt32(CompIdx, OldIndex);
		int32* Dst = GetInstancePtrInt32(CompIdx, NewIndex);
		int32 Temp = *Dst;
		*Dst = *Src;
		*Src = Temp;
	}
}

void FNiagaraDataBuffer::KillInstance(uint32 InstanceIdx)
{
	CheckUsage(false);
	check(InstanceIdx < NumInstances);
	--NumInstances;

	for (uint32 CompIdx = 0; CompIdx < Owner->TotalFloatComponents; ++CompIdx)
	{
		float* Src = GetInstancePtrFloat(CompIdx, NumInstances);
		float* Dst = GetInstancePtrFloat(CompIdx, InstanceIdx);
		*Dst = *Src;
	}
	for (uint32 CompIdx = 0; CompIdx < Owner->TotalInt32Components; ++CompIdx)
	{
		int32* Src = GetInstancePtrInt32(CompIdx, NumInstances);
		int32* Dst = GetInstancePtrInt32(CompIdx, InstanceIdx);
		*Dst = *Src;
	}

#if NIAGARA_NAN_CHECKING
	CheckForNaNs();
#endif
}

void FNiagaraDataBuffer::CopyTo(FNiagaraDataBuffer& DestBuffer, int32 StartIdx, int32 DestStartIdx, int32 InNumInstances)const
{
	CheckUsage(false);

	if (StartIdx < 0 || (uint32)StartIdx >= NumInstances)
	{
		return;
	}

	uint32 InstancesToCopy = InNumInstances;
	if (InstancesToCopy == INDEX_NONE)
	{
		InstancesToCopy = NumInstances - StartIdx;
	}

	if (InstancesToCopy != 0)
	{
		uint32 NewNumInstances = DestStartIdx + InstancesToCopy;
		if (DestStartIdx < 0 || NewNumInstances >= DestBuffer.GetNumInstances())
		{
			DestBuffer.Allocate(NewNumInstances, true);
		}
		DestBuffer.SetNumInstances(NewNumInstances);

		for (uint32 CompIdx = 0; CompIdx < Owner->TotalFloatComponents; ++CompIdx)
		{
			const float* SrcStart = GetInstancePtrFloat(CompIdx, StartIdx);
			const float* SrcEnd = GetInstancePtrFloat(CompIdx, StartIdx + InstancesToCopy);
			float* Dst = DestBuffer.GetInstancePtrFloat(CompIdx, DestStartIdx);
			size_t Count = SrcEnd - SrcStart;
			FMemory::Memcpy(Dst, SrcStart, Count*sizeof(float));

			if (Count > 0)
			{
				for (size_t i = 0; i < Count; i++)
				{
					checkSlow(SrcStart[i] == Dst[i]);
				}
			}
		}
		for (uint32 CompIdx = 0; CompIdx < Owner->TotalInt32Components; ++CompIdx)
		{
			const int32* SrcStart = GetInstancePtrInt32(CompIdx, StartIdx);
			const int32* SrcEnd = GetInstancePtrInt32(CompIdx, StartIdx + InstancesToCopy);
			int32* Dst = DestBuffer.GetInstancePtrInt32(CompIdx, DestStartIdx);
			size_t Count = SrcEnd - SrcStart;
			FMemory::Memcpy(Dst, SrcStart, Count * sizeof(int32));

			if (Count > 0)
			{
				for (size_t i = 0; i < Count; i++)
				{
					checkSlow(SrcStart[i] == Dst[i]);
				}
			}
		}
	}
}

void FNiagaraDataBuffer::GPUCopyFrom(float* GPUReadBackFloat, int* GPUReadBackInt, int32 InStartIdx, int32 InNumInstances, uint32 InSrcFloatStride, uint32 InSrcIntStride)
{
	//CheckUsage(false); //Have to disable this as in this specific case we write to a "CPUSim" from the RT.

	if (InNumInstances <= 0)
	{
		return;
	}

	Allocate(InNumInstances);
	SetNumInstances(InNumInstances);

	if (GPUReadBackFloat)
	{
		for (uint32 CompIdx = 0; CompIdx < Owner->TotalFloatComponents; ++CompIdx)
		{
			// We have to reimplement the logic from GetInstancePtrFloat here because the incoming stride may be different than this 
			// data buffer's stride.
			const float* SrcStart = (const float*)((uint8*)GPUReadBackFloat + InSrcFloatStride * CompIdx) + InStartIdx; 
			const float* SrcEnd = (const float*)((uint8*)GPUReadBackFloat + InSrcFloatStride * CompIdx) + InStartIdx + InNumInstances;
			float* Dst = GetInstancePtrFloat(CompIdx, 0);
			size_t Count = SrcEnd - SrcStart;
			FMemory::Memcpy(Dst, SrcStart, Count * sizeof(float));

			if (Count > 0)
			{
				for (size_t i = 0; i < Count; i++)
				{
					check(SrcStart[i] == Dst[i]);
				}
			}
		}
	}
	if (GPUReadBackInt)
	{
		for (uint32 CompIdx = 0; CompIdx < Owner->TotalInt32Components; ++CompIdx)
		{
			// We have to reimplement the logic from GetInstancePtrInt here because the incoming stride may be different than this 
			// data buffer's stride.
			const int32* SrcStart = (const int32*)((uint8*)GPUReadBackInt + InSrcIntStride * CompIdx) + InStartIdx;
			const int32* SrcEnd = (const int32*)((uint8*)GPUReadBackInt + InSrcIntStride * CompIdx) + InStartIdx + InNumInstances;
			int32* Dst = GetInstancePtrInt32(CompIdx, 0);
			size_t Count = SrcEnd - SrcStart;
			FMemory::Memcpy(Dst, SrcStart, Count * sizeof(int32));

			if (Count > 0)
			{
				for (size_t i = 0; i < Count; i++)
				{
					check(SrcStart[i] == Dst[i]);
				}
			}
		}
	}
}

void FNiagaraDataBuffer::CopyTo(FNiagaraDataBuffer& DestBuffer)const
{
	CheckUsage(true);
	DestBuffer.CheckUsage(false);
	DestBuffer.FloatStride = FloatStride;
	DestBuffer.FloatData = FloatData;
	DestBuffer.Int32Stride = Int32Stride;
	DestBuffer.Int32Data = Int32Data;
	DestBuffer.NumInstancesAllocated = NumInstancesAllocated;
	DestBuffer.NumInstances = NumInstances;
	DestBuffer.IDToIndexTable = IDToIndexTable;
}

void FNiagaraDataBuffer::Dump(int32 StartIndex, int32 InNumInstances, const FString& Label)const
{
	TArray<FNiagaraVariable>& Variables = Owner->GetVariables();
	FNiagaraDataVariableIterator Itr(this, StartIndex);
	Itr.AddVariables(Variables);

	if (InNumInstances == INDEX_NONE)
	{
		InNumInstances = GetNumInstances();
		InNumInstances -= StartIndex;
	}

	int32 NumInstancesDumped = 0;
	TArray<FString> Lines;
	Lines.Reserve(GetNumInstances());
	while (Itr.IsValid() && NumInstancesDumped < InNumInstances)
	{
		Itr.Get();

		FString Line = TEXT("| ");
		for (FNiagaraVariable& Var : Owner->GetVariables())
		{
			Line += Var.ToString() + TEXT(" | ");
		}
		Lines.Add(Line);
		Itr.Advance();
		NumInstancesDumped++;
	}

	static FString Sep;
	if (Sep.Len() == 0)
	{
		for (int32 i = 0; i < 50; ++i)
		{
			Sep.AppendChar(TEXT('='));
		}
	}

	UE_LOG(LogNiagara, Log, TEXT("%s"), *Sep);
	UE_LOG(LogNiagara, Log, TEXT(" %s "), *Label);
	UE_LOG(LogNiagara, Log, TEXT("%s"), *Sep);
	// 	UE_LOG(LogNiagara, Log, TEXT("%s"), *HeaderStr);
	// 	UE_LOG(LogNiagara, Log, TEXT("%s"), *Sep);
	for (FString& Str : Lines)
	{
		UE_LOG(LogNiagara, Log, TEXT("%s"), *Str);
	}
	if (IDToIndexTable.Num() > 0)
	{
		UE_LOG(LogNiagara, Log, TEXT("== ID Table =="), *Sep);
		for (int32 i = 0; i < IDToIndexTable.Num(); ++i)
		{
			UE_LOG(LogNiagara, Log, TEXT("%d = %d"), i, IDToIndexTable[i]);
		}
	}
	UE_LOG(LogNiagara, Log, TEXT("%s"), *Sep);

}

bool FNiagaraDataBuffer::AppendToRegisterTable(uint8** Registers, int32& NumRegisters, int32 StartInstance)
{
	check(Owner && Owner->IsInitialized());
	check(Owner->GetSimTarget() == ENiagaraSimTarget::CPUSim);
	CheckUsage(true);

	for (FNiagaraVariableLayoutInfo& VarLayout : Owner->VariableLayouts)
	{
		int32 NumFloats = VarLayout.GetNumFloatComponents();
		int32 NumInts = VarLayout.GetNumInt32Components();
		for (int32 CompIdx = 0; CompIdx < NumFloats; ++CompIdx)
		{
			uint32 CompBufferOffset = VarLayout.FloatComponentStart + CompIdx;
			uint32 CompRegisterOffset = VarLayout.LayoutInfo.FloatComponentRegisterOffsets[CompIdx];
			Registers[NumRegisters + CompRegisterOffset] = (uint8*)GetInstancePtrFloat(CompBufferOffset, StartInstance);
		}
		for (int32 CompIdx = 0; CompIdx < NumInts; ++CompIdx)
		{
			uint32 CompBufferOffset = VarLayout.Int32ComponentStart + CompIdx;
			uint32 CompRegisterOffset = VarLayout.LayoutInfo.Int32ComponentRegisterOffsets[CompIdx];
			Registers[NumRegisters + CompRegisterOffset] = (uint8*)GetInstancePtrInt32(CompBufferOffset, StartInstance);
		}
		NumRegisters += NumFloats + NumInts;
	}
	return true;
}

/////////////////////////////////////////////////////////////////////////

template<bool bDoResourceTransitions>
void FNiagaraDataBuffer::SetShaderParams(FNiagaraShader *Shader, FRHICommandList &CommandList, bool bInput)
{
	check(IsInRenderingThread());
	if (bInput)
	{
		if (Shader->FloatInputBufferParam.IsBound())
		{
			if (bDoResourceTransitions)
			{
				CommandList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, GetGPUBufferFloat().UAV);
			}
			if (GetNumInstancesAllocated() > 0)
			{
				CommandList.SetShaderResourceViewParameter(Shader->GetComputeShader(), Shader->FloatInputBufferParam.GetBaseIndex(), GetGPUBufferFloat().SRV);
			}
			else
			{
				CommandList.SetShaderResourceViewParameter(Shader->GetComputeShader(), Shader->FloatInputBufferParam.GetBaseIndex(), FNiagaraRenderer::GetDummyFloatBuffer().SRV);
			}
		}

		if (Shader->IntInputBufferParam.IsBound())
		{
			if (bDoResourceTransitions)
			{
				CommandList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, GetGPUBufferInt().UAV);
			}
			if (GetNumInstancesAllocated() > 0)
			{
				CommandList.SetShaderResourceViewParameter(Shader->GetComputeShader(), Shader->IntInputBufferParam.GetBaseIndex(), GetGPUBufferInt().SRV);
			}
			else
			{
				CommandList.SetShaderResourceViewParameter(Shader->GetComputeShader(), Shader->IntInputBufferParam.GetBaseIndex(), FNiagaraRenderer::GetDummyIntBuffer().SRV);
			}
		}

		if (Shader->ComponentBufferSizeReadParam.IsBound())
		{
			uint32 SafeBufferSize = GetFloatStride() / sizeof(float);
			CommandList.SetShaderParameter(Shader->GetComputeShader(), Shader->ComponentBufferSizeReadParam.GetBufferIndex(), Shader->ComponentBufferSizeReadParam.GetBaseIndex(), Shader->ComponentBufferSizeReadParam.GetNumBytes(), &SafeBufferSize);
		}
	}
	else
	{
		if (Shader->FloatOutputBufferParam.IsUAVBound())
		{
			if (bDoResourceTransitions)
			{
				CommandList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EGfxToCompute, GetGPUBufferFloat().UAV);
			}
			CommandList.SetUAVParameter(Shader->GetComputeShader(), Shader->FloatOutputBufferParam.GetUAVIndex(), GetGPUBufferFloat().UAV);
		}

		if (Shader->IntOutputBufferParam.IsUAVBound())
		{
			if (bDoResourceTransitions)
			{
				CommandList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EGfxToCompute, GetGPUBufferInt().UAV);
			}
			CommandList.SetUAVParameter(Shader->GetComputeShader(), Shader->IntOutputBufferParam.GetUAVIndex(), GetGPUBufferInt().UAV);
		}

		if (Shader->ComponentBufferSizeWriteParam.IsBound())
		{
			uint32 SafeBufferSize = GetFloatStride() / sizeof(float);
			CommandList.SetShaderParameter(Shader->GetComputeShader(), Shader->ComponentBufferSizeWriteParam.GetBufferIndex(), Shader->ComponentBufferSizeWriteParam.GetBaseIndex(), Shader->ComponentBufferSizeWriteParam.GetNumBytes(), &SafeBufferSize);
		}
	}
}

template void FNiagaraDataBuffer::SetShaderParams<true>(FNiagaraShader*, FRHICommandList&, bool);
template void FNiagaraDataBuffer::SetShaderParams<false>(FNiagaraShader*, FRHICommandList&, bool);


void FNiagaraDataBuffer::UnsetShaderParams(FNiagaraShader *Shader, FRHICommandList &RHICmdList)
{
	check(IsInRenderingThread());

	if (Shader->FloatOutputBufferParam.IsUAVBound())
	{
#if !PLATFORM_PS4
		Shader->FloatOutputBufferParam.UnsetUAV(RHICmdList, Shader->GetComputeShader());
#endif
		//RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EGfxToGfx, CurrDataRender().GetGPUBufferFloat()->UAV);
	}

	if (Shader->IntOutputBufferParam.IsUAVBound())
	{
#if !PLATFORM_PS4
		Shader->IntOutputBufferParam.UnsetUAV(RHICmdList, Shader->GetComputeShader());
#endif
		//RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EGfxToGfx, CurrDataRender().GetGPUBufferInt()->UAV);
	}
}

void FNiagaraDataBuffer::ReleaseGPUInstanceCount(FNiagaraGPUInstanceCountManager& GPUInstanceCountManager)
{
	GPUInstanceCountManager.FreeEntry(GPUInstanceCountBufferOffset);
}


FScopedNiagaraDataSetGPUReadback::~FScopedNiagaraDataSetGPUReadback()
{
	if (DataBuffer != nullptr)
	{
		DataBuffer->FloatData.Empty();
		DataBuffer->Int32Data.Empty();
	}
}

void FScopedNiagaraDataSetGPUReadback::ReadbackData(NiagaraEmitterInstanceBatcher* InBatcher, FNiagaraDataSet* InDataSet)
{
	check(DataSet == nullptr);
	check(InDataSet != nullptr);

	Batcher = InBatcher && !InBatcher->IsPendingKill() ? Batcher : nullptr;
	DataSet = InDataSet;
	DataBuffer = DataSet->GetCurrentData();

	// These should be zero if we are GPU and aren't inside a readback scope already
	check((DataBuffer->FloatData.Num() == 0) && (DataBuffer->Int32Data.Num() == 0));

	// Readback data
	ENQUEUE_RENDER_COMMAND(ReadbackGPUBuffers)
	(
		[&](FRHICommandListImmediate& RHICmdList)
		{
			// Read DrawIndirect Params
			const uint32 BufferOffset = DataBuffer->GetGPUInstanceCountBufferOffset();
			if (Batcher && BufferOffset != INDEX_NONE)
			{
				FRHIVertexBuffer* InstanceCountBuffer = Batcher->GetGPUInstanceCounterManager().GetInstanceCountBuffer().Buffer;

				void* Data = RHICmdList.LockVertexBuffer(InstanceCountBuffer, 0, (BufferOffset + 1) * sizeof(int32), RLM_ReadOnly);
				NumInstances = reinterpret_cast<int32*>(Data)[BufferOffset];
				RHICmdList.UnlockVertexBuffer(InstanceCountBuffer);
			}
			else
			{
				NumInstances = DataBuffer->GetNumInstances();
			}

			// Read float data
			const FRWBuffer& GPUFloatBuffer = DataBuffer->GetGPUBufferFloat();
			if (GPUFloatBuffer.Buffer.IsValid())
			{
				DataBuffer->FloatData.AddUninitialized(GPUFloatBuffer.NumBytes);

				void* CPUFloatBuffer = RHICmdList.LockVertexBuffer(GPUFloatBuffer.Buffer, 0, GPUFloatBuffer.NumBytes, RLM_ReadOnly);
				FMemory::Memcpy(DataBuffer->FloatData.GetData(), CPUFloatBuffer, GPUFloatBuffer.NumBytes);
				RHICmdList.UnlockVertexBuffer(GPUFloatBuffer.Buffer);
			}

			// Read int data
			const FRWBuffer& GPUIntBuffer = DataBuffer->GetGPUBufferInt();
			if (GPUIntBuffer.Buffer.IsValid())
			{
				DataBuffer->Int32Data.AddUninitialized(GPUIntBuffer.NumBytes);

				void* CPUIntBuffer = RHICmdList.LockVertexBuffer(GPUIntBuffer.Buffer, 0, GPUIntBuffer.NumBytes, RLM_ReadOnly);
				FMemory::Memcpy(DataBuffer->Int32Data.GetData(), CPUIntBuffer, GPUIntBuffer.NumBytes);
				RHICmdList.UnlockVertexBuffer(GPUIntBuffer.Buffer);
			}
		}
	);
	FlushRenderingCommands();
}
