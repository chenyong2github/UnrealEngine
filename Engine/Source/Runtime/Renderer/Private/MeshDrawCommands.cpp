// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
MeshDrawCommandSetup.cpp: Mesh draw command setup.
=============================================================================*/

#include "MeshDrawCommands.h"
#include "RendererModule.h"
#include "ScenePrivate.h"
#include "TranslucentRendering.h"

TGlobalResource<FPrimitiveIdVertexBufferPool> GPrimitiveIdVertexBufferPool;

static TAutoConsoleVariable<int32> CVarMeshDrawCommandsParallelPassSetup(
	TEXT("r.MeshDrawCommands.ParallelPassSetup"),
	1,
	TEXT("Whether to setup mesh draw command pass in parallel."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarMobileMeshSortingMethod(
	TEXT("r.Mobile.MeshSortingMethod"),
	0,
	TEXT("How to sort mesh commands on mobile:\n")
	TEXT("\t0: Sort by state, roughly front to back (Default).\n")
	TEXT("\t1: Strict front to back sorting.\n"),
	ECVF_RenderThreadSafe);

static int32 GAllowOnDemandShaderCreation = 1;
static FAutoConsoleVariableRef CVarAllowOnDemandShaderCreation(
	TEXT("r.MeshDrawCommands.AllowOnDemandShaderCreation"),
	GAllowOnDemandShaderCreation,
	TEXT("How to create RHI shaders:\n")
	TEXT("\t0: Always create them on a Rendering Thread, before executing other MDC tasks.\n")
	TEXT("\t1: If RHI supports multi-threaded shader creation, create them on demand on tasks threads, at the time of submitting the draws.\n"),
	ECVF_RenderThreadSafe);

FPrimitiveIdVertexBufferPool::FPrimitiveIdVertexBufferPool()
	: DiscardId(0)
{
}

FPrimitiveIdVertexBufferPool::~FPrimitiveIdVertexBufferPool()
{
	check(!Entries.Num());
}

FPrimitiveIdVertexBufferPoolEntry FPrimitiveIdVertexBufferPool::Allocate(int32 BufferSize)
{
	check(IsInRenderingThread());

	FScopeLock Lock(&AllocationCS);

	BufferSize = Align(BufferSize, 1024);

	// First look for a smallest unused one.
	int32 BestFitBufferIndex = -1;
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		// Unused and fits?
		if (Entries[Index].LastDiscardId != DiscardId && Entries[Index].BufferSize >= BufferSize)
		{
			// Is it a bet fit than current BestFitBufferIndex?
			if (BestFitBufferIndex == -1 || Entries[Index].BufferSize < Entries[BestFitBufferIndex].BufferSize)
			{
				BestFitBufferIndex = Index;
				
				if (Entries[BestFitBufferIndex].BufferSize == BufferSize)
				{
					break;
				}
			}
		}
	}
	
	if (BestFitBufferIndex >= 0)
	{
		// Reuse existing buffer.
		FPrimitiveIdVertexBufferPoolEntry ReusedEntry = MoveTemp(Entries[BestFitBufferIndex]);
		ReusedEntry.LastDiscardId = DiscardId;
		Entries.RemoveAt(BestFitBufferIndex);
		return ReusedEntry;
	}
	else
	{
		// Allocate new one.
		FPrimitiveIdVertexBufferPoolEntry NewEntry;
		NewEntry.LastDiscardId = DiscardId;
		NewEntry.BufferSize = BufferSize;
		FRHIResourceCreateInfo CreateInfo;
		NewEntry.BufferRHI = RHICreateVertexBuffer(NewEntry.BufferSize, BUF_Volatile, CreateInfo);

		return NewEntry;
	}
}

void FPrimitiveIdVertexBufferPool::ReturnToFreeList(FPrimitiveIdVertexBufferPoolEntry Entry)
{
	// Entries can be returned from RHIT or RT, depending on if FParallelMeshDrawCommandPass::DispatchDraw() takes the parallel path
	FScopeLock Lock(&AllocationCS);

	Entries.Add(MoveTemp(Entry));
}

void FPrimitiveIdVertexBufferPool::DiscardAll()
{
	FScopeLock Lock(&AllocationCS);

	++DiscardId;

	// Remove old unused pool entries.
	for (int32 Index = 0; Index < Entries.Num();)
	{
		if (DiscardId - Entries[Index].LastDiscardId > 1000u)
		{
			Entries.RemoveAtSwap(Index);
		}
		else
		{
			++Index;
		}
	}
}

void FPrimitiveIdVertexBufferPool::ReleaseDynamicRHI()
{
	Entries.Empty();
}


struct FCompareFMeshDrawCommands
{
	FORCEINLINE bool operator() (FVisibleMeshDrawCommand A, FVisibleMeshDrawCommand B) const
	{
		// First order by a sort key.
		if (A.SortKey != B.SortKey)
		{
			return A.SortKey < B.SortKey;
		}

		// Next order by instancing bucket.
		if (A.StateBucketId != B.StateBucketId)
		{
			return A.StateBucketId < B.StateBucketId;
		}

		return false;
	}
};

uint32 BitInvertIfNegativeFloat(uint32 f)
{
	unsigned mask = -int32(f >> 31) | 0x80000000;
	return f ^ mask;
}

/**
* Update mesh sort keys with view dependent data.
*/
void UpdateTranslucentMeshSortKeys(
	ETranslucentSortPolicy::Type TranslucentSortPolicy,
	const FVector& TranslucentSortAxis,
	const FVector& ViewOrigin,
	const FMatrix& ViewMatrix,
	const TArray<struct FPrimitiveBounds>& PrimitiveBounds,
	ETranslucencyPass::Type TranslucencyPass, 
	FMeshCommandOneFrameArray& VisibleMeshCommands
	)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_UpdateTranslucentMeshSortKeys);

	for (int32 CommandIndex = 0; CommandIndex < VisibleMeshCommands.Num(); ++CommandIndex)
	{
		FVisibleMeshDrawCommand& VisibleCommand = VisibleMeshCommands[CommandIndex];

		const int32 PrimitiveIndex = VisibleCommand.ScenePrimitiveId;
		const FVector BoundsOrigin = PrimitiveIndex >= 0 ? PrimitiveBounds[PrimitiveIndex].BoxSphereBounds.Origin : FVector::ZeroVector;

		float Distance = 0.0f;
		if (TranslucentSortPolicy == ETranslucentSortPolicy::SortByDistance)
		{
			//sort based on distance to the view position, view rotation is not a factor
			Distance = (BoundsOrigin - ViewOrigin).Size();
		}
		else if (TranslucentSortPolicy == ETranslucentSortPolicy::SortAlongAxis)
		{
			// Sort based on enforced orthogonal distance
			const FVector CameraToObject = BoundsOrigin - ViewOrigin;
			Distance = FVector::DotProduct(CameraToObject, TranslucentSortAxis);
		}
		else
		{
			// Sort based on projected Z distance
			check(TranslucentSortPolicy == ETranslucentSortPolicy::SortByProjectedZ);
			Distance = ViewMatrix.TransformPosition(BoundsOrigin).Z;
		}

		// Patch distance inside translucent mesh sort key.
		FMeshDrawCommandSortKey SortKey;
		SortKey.PackedData = VisibleCommand.SortKey.PackedData;
		SortKey.Translucent.Distance = (uint32)~BitInvertIfNegativeFloat(*((uint32*)&Distance));
		VisibleCommand.SortKey.PackedData = SortKey.PackedData;
	}
}

static uint64 GetMobileBasePassSortKey_FrontToBack(bool bMasked, bool bBackground, uint32 PipelineId, int32 StateBucketId, float PrimitiveDistance)
{
	union
	{
		uint64 PackedInt;
		struct
		{
			uint64 StateBucketId : 27; 		// Order by state bucket
			uint64 PipelineId : 20;			// Order by PSO
			uint64 DepthBits : 15;			// Order by primitive depth
			uint64 Background : 1;			// Non-background meshes first 
			uint64 Masked : 1;				// Non-masked first
		} Fields;
	} Key;

	union FFloatToInt { float F; uint32 I; };
	FFloatToInt F2I;

	Key.Fields.Masked = bMasked;
	Key.Fields.Background = bBackground;
	F2I.F = PrimitiveDistance;
	Key.Fields.DepthBits = ((-int32(F2I.I >> 31) | 0x80000000) ^ F2I.I) >> 17;
	Key.Fields.PipelineId = PipelineId;
	Key.Fields.StateBucketId = StateBucketId;
	
	return Key.PackedInt;
}

static uint64 GetMobileBasePassSortKey_ByState(bool bMasked, bool bBackground, int32 PipelineId, int32 StateBucketId, float PipelineDistance, float PrimitiveDistance)
{
	const float PrimitiveDepthQuantization = ((1 << 14) - 1);

	union
	{
		uint64 PackedInt;
		struct
		{
			uint64 DepthBits : 14;			// Order by primitive depth
			uint64 StateBucketId : 20; 		// Order by state bucket
			uint64 PipelineId : 20;			// Order by PSO
			uint64 PipelineDepthBits : 8;	// Order PSOs front to back
			uint64 Background : 1;			// Non-background meshes first 
			uint64 Masked : 1;				// Non-masked first
		} Fields;
	} Key;

	union FFloatToInt { float F; uint32 I; };
	FFloatToInt F2I;

	Key.PackedInt = 0;
	Key.Fields.Masked = bMasked;
	Key.Fields.Background = bBackground;
	F2I.F = PipelineDistance / HALF_WORLD_MAX;
	Key.Fields.PipelineDepthBits = (F2I.I >> 23) & 0xff; // 8 bit exponent
	Key.Fields.PipelineId = PipelineId;
	Key.Fields.StateBucketId = StateBucketId;
	Key.Fields.DepthBits = int32((FMath::Min<float>(PrimitiveDistance, HALF_WORLD_MAX) / HALF_WORLD_MAX) * PrimitiveDepthQuantization);

	return Key.PackedInt;
}

/**
* Merge mobile BasePass with BasePassCSM based on CSM visibility in order to select appropriate shader for given command.
*/
void MergeMobileBasePassMeshDrawCommands(
	const FMobileCSMVisibilityInfo& MobileCSMVisibilityInfo,
	int32 ScenePrimitiveNum,
	FMeshCommandOneFrameArray& MeshCommands,
	FMeshCommandOneFrameArray& MeshCommandsCSM
	)
{
	if (MobileCSMVisibilityInfo.bMobileDynamicCSMInUse)
	{
		// determine per view CSM visibility
		checkf(MeshCommands.Num() == MeshCommandsCSM.Num(), TEXT("VisibleMeshDrawCommands of BasePass and MobileBasePassCSM are expected to match."));
		for (int32 i = MeshCommands.Num() - 1; i >= 0; --i)
		{
			FVisibleMeshDrawCommand& MeshCommand = MeshCommands[i];
			FVisibleMeshDrawCommand& MeshCommandCSM = MeshCommandsCSM[i];

			if (MobileCSMVisibilityInfo.bAlwaysUseCSM 
				|| (MeshCommand.ScenePrimitiveId < ScenePrimitiveNum && MobileCSMVisibilityInfo.MobilePrimitiveCSMReceiverVisibilityMap[MeshCommand.ScenePrimitiveId]))
			{
				checkf(MeshCommand.ScenePrimitiveId == MeshCommandCSM.ScenePrimitiveId, TEXT("VisibleMeshDrawCommands of BasePass and MobileBasePassCSM are expected to match."));
				// Use CSM's VisibleMeshDrawCommand.
				MeshCommand = MeshCommandCSM;
			}
		}
		MeshCommandsCSM.Reset();
	}
}

/**
* Compute mesh sort keys for the mobile base pass
*/
void UpdateMobileBasePassMeshSortKeys(
	const FVector& ViewOrigin,
	const TArray<struct FPrimitiveBounds>& ScenePrimitiveBounds,
	FMeshCommandOneFrameArray& VisibleMeshCommands
)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_UpdateMobileBasePassMeshSortKeys);
	
	int32 NumCmds = VisibleMeshCommands.Num();
	int32 MeshSortingMethod = CVarMobileMeshSortingMethod.GetValueOnAnyThread();
	
	if (MeshSortingMethod == 1) //strict front to back sorting
	{
		// compute sort key for each mesh command
		for (int32 CmdIdx = 0; CmdIdx < NumCmds; ++CmdIdx)
		{
			FVisibleMeshDrawCommand& Cmd = VisibleMeshCommands[CmdIdx];
			// Set in MobileBasePass.cpp - GetBasePassStaticSortKey;
			bool bMasked = Cmd.SortKey.PackedData & 0x1 ? true : false; 
			bool bBackground = Cmd.SortKey.PackedData & 0x2 ? true : false;
			float PrimitiveDistance = 0;
			if (Cmd.ScenePrimitiveId < ScenePrimitiveBounds.Num())
			{
				const FPrimitiveBounds& PrimitiveBounds = ScenePrimitiveBounds[Cmd.ScenePrimitiveId];
				PrimitiveDistance = (PrimitiveBounds.BoxSphereBounds.Origin - ViewOrigin).Size();
				bBackground|= (PrimitiveBounds.BoxSphereBounds.SphereRadius > HALF_WORLD_MAX / 4.0f);
			}

			uint32 PipelineId = Cmd.MeshDrawCommand->CachedPipelineId.GetId();
			// use state bucket if dynamic instancing is enabled, otherwise identify same meshes by index buffer resource
			int32 StateBucketId = Cmd.StateBucketId >=0 ? Cmd.StateBucketId : PointerHash(Cmd.MeshDrawCommand->IndexBuffer);
			Cmd.SortKey.PackedData = GetMobileBasePassSortKey_FrontToBack(bMasked, bBackground, PipelineId, StateBucketId, PrimitiveDistance);
		}
	}
	else // prefer state then distance
	{
		TMap<uint32, float> PipelineDistances;
		PipelineDistances.Reserve(256);
				
		// pre-compute distance to a group of meshes that share same PSO
		for (int32 CmdIdx = 0; CmdIdx < NumCmds; ++CmdIdx)
		{
			FVisibleMeshDrawCommand& Cmd = VisibleMeshCommands[CmdIdx];
			float PrimitiveDistance = 0;
			if (Cmd.ScenePrimitiveId < ScenePrimitiveBounds.Num())
			{
				const FPrimitiveBounds& PrimitiveBounds = ScenePrimitiveBounds[Cmd.ScenePrimitiveId];
				PrimitiveDistance = (PrimitiveBounds.BoxSphereBounds.Origin - ViewOrigin).Size();
			}

			float& PipelineDistance = PipelineDistances.FindOrAdd(Cmd.MeshDrawCommand->CachedPipelineId.GetId());
			// not sure what could be better: average distance, max or min
			PipelineDistance = FMath::Max(PipelineDistance, PrimitiveDistance);
		}

		// compute sort key for each mesh command
		for (int32 CmdIdx = 0; CmdIdx < NumCmds; ++CmdIdx)
		{
			FVisibleMeshDrawCommand& Cmd = VisibleMeshCommands[CmdIdx];
			// Set in MobileBasePass.cpp - GetBasePassStaticSortKey;
			bool bMasked = Cmd.SortKey.PackedData & 0x1 ? true : false; 
			bool bBackground = Cmd.SortKey.PackedData & 0x2 ? true : false;
			float PrimitiveDistance = 0;
			if (Cmd.ScenePrimitiveId < ScenePrimitiveBounds.Num())
			{
				const FPrimitiveBounds& PrimitiveBounds = ScenePrimitiveBounds[Cmd.ScenePrimitiveId];
				PrimitiveDistance = (PrimitiveBounds.BoxSphereBounds.Origin - ViewOrigin).Size();
				bBackground|= (PrimitiveBounds.BoxSphereBounds.SphereRadius > HALF_WORLD_MAX / 4.0f);
			}
			
			int32 PipelineId = Cmd.MeshDrawCommand->CachedPipelineId.GetId();
			float PipelineDistance = PipelineDistances.FindRef(PipelineId);
			// use state bucket if dynamic instancing is enabled, otherwise identify same meshes by index buffer resource
			int32 StateBucketId = Cmd.StateBucketId >=0 ? Cmd.StateBucketId : PointerHash(Cmd.MeshDrawCommand->IndexBuffer);
			Cmd.SortKey.PackedData = GetMobileBasePassSortKey_ByState(bMasked, bBackground, PipelineId, StateBucketId, PipelineDistance, PrimitiveDistance);
		}
	}
}

void BatchInstancingMeshDrawCommands_Internal(
	FMeshCommandOneFrameArray& VisibleMeshDrawCommands,
	FDynamicMeshDrawCommandStorage& MeshDrawCommandStorage,
	FMeshCommandOneFrameArray& TempVisibleMeshDrawCommands,
	int32 BatchBeginIdx,
	int32 BatchEndIdx)
{
	check(BatchBeginIdx >= 0 && BatchBeginIdx < VisibleMeshDrawCommands.Num());
	check(BatchEndIdx >= 0 && BatchEndIdx < VisibleMeshDrawCommands.Num());

	const FVisibleMeshDrawCommand* RESTRICT PassVisibleMeshDrawCommands = VisibleMeshDrawCommands.GetData();
	const FVisibleMeshDrawCommand& RESTRICT VisibleMeshDrawCommand = PassVisibleMeshDrawCommands[BatchBeginIdx];

	// At least two elements, do a batch
	if (BatchEndIdx - BatchBeginIdx > 0) 
	{	
		int32 InstancingBatchVertexStreamIndex = VisibleMeshDrawCommand.MeshDrawCommand->InstancingBatchVertexStreamIndex;
		check(InstancingBatchVertexStreamIndex != 0xF && InstancingBatchVertexStreamIndex < VisibleMeshDrawCommand.MeshDrawCommand->VertexStreams.Num());

		uint32 BatchVertexStreamOffset = VisibleMeshDrawCommand.MeshDrawCommand->VertexStreams[InstancingBatchVertexStreamIndex].Offset;
		uint32 BatchNumInstances = 0;
		for (int Idx = BatchBeginIdx; Idx <= BatchEndIdx; Idx++)
		{
			const FVisibleMeshDrawCommand& RESTRICT PassVisibleMeshDrawCommand = PassVisibleMeshDrawCommands[Idx];

			int32 VertexStreamIndex = PassVisibleMeshDrawCommand.MeshDrawCommand->InstancingBatchVertexStreamIndex;
			if (VertexStreamIndex == InstancingBatchVertexStreamIndex)
			{
				BatchVertexStreamOffset = FMath::Min(BatchVertexStreamOffset, PassVisibleMeshDrawCommand.MeshDrawCommand->VertexStreams[VertexStreamIndex].Offset);
				BatchNumInstances += PassVisibleMeshDrawCommand.MeshDrawCommand->NumInstances;
			}
			else
			{
				//check(0);
				UE_LOG(LogRenderer, Warning, TEXT("Try to batch instancing MeshDrawCommands with different VertexStreamIndex."));
			}
		}

		const int32 Index = MeshDrawCommandStorage.MeshDrawCommands.AddElement(*VisibleMeshDrawCommand.MeshDrawCommand);
		FMeshDrawCommand& NewMeshDrawCommand = MeshDrawCommandStorage.MeshDrawCommands[Index];

		NewMeshDrawCommand.VertexStreams[InstancingBatchVertexStreamIndex].Offset = BatchVertexStreamOffset;
		NewMeshDrawCommand.NumInstances = BatchNumInstances;

		FVisibleMeshDrawCommand NewVisibleMeshDrawCommand;
		NewVisibleMeshDrawCommand.Setup(
			&NewMeshDrawCommand,
			VisibleMeshDrawCommand.DrawPrimitiveId,
			VisibleMeshDrawCommand.ScenePrimitiveId,
			VisibleMeshDrawCommand.StateBucketId,
			VisibleMeshDrawCommand.MeshFillMode,
			VisibleMeshDrawCommand.MeshCullMode,
			VisibleMeshDrawCommand.SortKey);

		TempVisibleMeshDrawCommands.Emplace(MoveTemp(NewVisibleMeshDrawCommand));
	}
	else
	{
		FVisibleMeshDrawCommand NewVisibleMeshDrawCommand = VisibleMeshDrawCommand;
		TempVisibleMeshDrawCommands.Emplace(MoveTemp(NewVisibleMeshDrawCommand));
	}
}

void BatchInstancingMeshDrawCommands(
	FMeshCommandOneFrameArray& VisibleMeshDrawCommands,
	FDynamicMeshDrawCommandStorage& MeshDrawCommandStorage,
	FMeshCommandOneFrameArray& TempVisibleMeshDrawCommands,
	int32& VisibleMeshDrawCommandsNum,
	int32& NewPassVisibleMeshDrawCommandsNum
	)
{
	const FVisibleMeshDrawCommand* RESTRICT PassVisibleMeshDrawCommands = VisibleMeshDrawCommands.GetData();
	const int32 NumDrawCommands = VisibleMeshDrawCommands.Num();
	check(VisibleMeshDrawCommands.Num() <= TempVisibleMeshDrawCommands.Max() && TempVisibleMeshDrawCommands.Num() == 0);

	uint32 CurrentInstancingBatchId = 0;
	int32 InstancingBatchBeginIdx = -1;

	for (int32 DrawCommandIndex = 0; DrawCommandIndex < NumDrawCommands; DrawCommandIndex++)
	{
		const FVisibleMeshDrawCommand& RESTRICT VisibleMeshDrawCommand = PassVisibleMeshDrawCommands[DrawCommandIndex];

		if (VisibleMeshDrawCommand.MeshDrawCommand->InstancingBatchId > 0)
		{
			if (VisibleMeshDrawCommand.MeshDrawCommand->InstancingBatchId != CurrentInstancingBatchId)
			{
				if (InstancingBatchBeginIdx != -1)
				{
					BatchInstancingMeshDrawCommands_Internal(VisibleMeshDrawCommands, MeshDrawCommandStorage, TempVisibleMeshDrawCommands, InstancingBatchBeginIdx, DrawCommandIndex - 1);
				}

				CurrentInstancingBatchId = VisibleMeshDrawCommand.MeshDrawCommand->InstancingBatchId;
				InstancingBatchBeginIdx = DrawCommandIndex;

				if (DrawCommandIndex >= NumDrawCommands - 1) //Tail array element
				{
					FVisibleMeshDrawCommand NewVisibleMeshDrawCommand = VisibleMeshDrawCommand;
					TempVisibleMeshDrawCommands.Emplace(MoveTemp(NewVisibleMeshDrawCommand));
				}
			}
			else
			{
				if (DrawCommandIndex >= NumDrawCommands - 1) //Tail array element
				{
					if (InstancingBatchBeginIdx != -1)
					{
						BatchInstancingMeshDrawCommands_Internal(VisibleMeshDrawCommands, MeshDrawCommandStorage, TempVisibleMeshDrawCommands, InstancingBatchBeginIdx, DrawCommandIndex);

						CurrentInstancingBatchId = 0;
						InstancingBatchBeginIdx = -1;
					}
				}
			}
		}
		else
		{
			if (InstancingBatchBeginIdx != -1)
			{
				BatchInstancingMeshDrawCommands_Internal(VisibleMeshDrawCommands, MeshDrawCommandStorage, TempVisibleMeshDrawCommands, InstancingBatchBeginIdx, DrawCommandIndex - 1);

				CurrentInstancingBatchId = 0;
				InstancingBatchBeginIdx = -1;
			}

			FVisibleMeshDrawCommand NewVisibleMeshDrawCommand = VisibleMeshDrawCommand;
			TempVisibleMeshDrawCommands.Emplace(MoveTemp(NewVisibleMeshDrawCommand));
		}
	}

	VisibleMeshDrawCommandsNum = VisibleMeshDrawCommands.Num();
	NewPassVisibleMeshDrawCommandsNum = TempVisibleMeshDrawCommands.Num();

	// Replace VisibleMeshDrawCommands
	FMemory::Memswap(&VisibleMeshDrawCommands, &TempVisibleMeshDrawCommands, sizeof(TempVisibleMeshDrawCommands));
	TempVisibleMeshDrawCommands.Reset();
}

/**
 * Build mesh draw command primitive Id buffer for instancing.
 * TempVisibleMeshDrawCommands must be presized for NewPassVisibleMeshDrawCommands.
 */
void BuildMeshDrawCommandPrimitiveIdBuffer(
	bool bDynamicInstancing,
	FMeshCommandOneFrameArray& VisibleMeshDrawCommands,
	FDynamicMeshDrawCommandStorage& MeshDrawCommandStorage,
	void* RESTRICT PrimitiveIdData,
	int32 PrimitiveIdDataSize,
	FMeshCommandOneFrameArray& TempVisibleMeshDrawCommands,
	int32& MaxInstances,
	int32& VisibleMeshDrawCommandsNum,
	int32& NewPassVisibleMeshDrawCommandsNum,
	EShaderPlatform ShaderPlatform,
	uint32 InstanceFactor = 1
	)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_BuildMeshDrawCommandPrimitiveIdBuffer);
	check(PrimitiveIdData && PrimitiveIdDataSize > 0);

	const FVisibleMeshDrawCommand* RESTRICT PassVisibleMeshDrawCommands = VisibleMeshDrawCommands.GetData();
	const int32 NumDrawCommands = VisibleMeshDrawCommands.Num();

	uint32 PrimitiveIdIndex = 0;
	int32* RESTRICT PrimitiveIds = (int32*)PrimitiveIdData;
	const uint32 MaxPrimitiveId = PrimitiveIdDataSize / sizeof(int32);

	if (bDynamicInstancing)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_DynamicInstancingOfVisibleMeshDrawCommands);
		check(VisibleMeshDrawCommands.Num() <= TempVisibleMeshDrawCommands.Max() && TempVisibleMeshDrawCommands.Num() == 0);

		int32 CurrentStateBucketId = -1;
		uint32* RESTRICT CurrentDynamicallyInstancedMeshCommandNumInstances = nullptr;
		MaxInstances = 1;

		for (int32 DrawCommandIndex = 0; DrawCommandIndex < NumDrawCommands; DrawCommandIndex++)
		{
			const FVisibleMeshDrawCommand& RESTRICT VisibleMeshDrawCommand = PassVisibleMeshDrawCommands[DrawCommandIndex];

			if (VisibleMeshDrawCommand.StateBucketId == CurrentStateBucketId && VisibleMeshDrawCommand.StateBucketId != -1)
			{
				if (CurrentDynamicallyInstancedMeshCommandNumInstances)
				{
					const int32 CurrentNumInstances = *CurrentDynamicallyInstancedMeshCommandNumInstances;
					*CurrentDynamicallyInstancedMeshCommandNumInstances = CurrentNumInstances + 1;
					MaxInstances = FMath::Max(MaxInstances, CurrentNumInstances + 1);
				}
				else
				{
					FVisibleMeshDrawCommand NewVisibleMeshDrawCommand = VisibleMeshDrawCommand;
					NewVisibleMeshDrawCommand.PrimitiveIdBufferOffset = PrimitiveIdIndex;
					TempVisibleMeshDrawCommands.Emplace(MoveTemp(NewVisibleMeshDrawCommand));
				}
			}
			else
			{
				// First time state bucket setup
				CurrentStateBucketId = VisibleMeshDrawCommand.StateBucketId;

				if (VisibleMeshDrawCommand.StateBucketId != INDEX_NONE
					&& VisibleMeshDrawCommand.MeshDrawCommand->PrimitiveIdStreamIndex >= 0
					&& VisibleMeshDrawCommand.MeshDrawCommand->NumInstances == 1
					// Don't create a new FMeshDrawCommand for the last command and make it safe for us to look at the next command
					&& DrawCommandIndex + 1 < NumDrawCommands
					// Only create a new FMeshDrawCommand if more than one draw in the state bucket
					&& CurrentStateBucketId == PassVisibleMeshDrawCommands[DrawCommandIndex + 1].StateBucketId)
				{
					const int32 Index = MeshDrawCommandStorage.MeshDrawCommands.AddElement(*VisibleMeshDrawCommand.MeshDrawCommand);
					FMeshDrawCommand& NewCommand = MeshDrawCommandStorage.MeshDrawCommands[Index];
					FVisibleMeshDrawCommand NewVisibleMeshDrawCommand;

					NewVisibleMeshDrawCommand.Setup(
						&NewCommand,
						VisibleMeshDrawCommand.DrawPrimitiveId,
						VisibleMeshDrawCommand.ScenePrimitiveId,
						VisibleMeshDrawCommand.StateBucketId,
						VisibleMeshDrawCommand.MeshFillMode,
						VisibleMeshDrawCommand.MeshCullMode,
						VisibleMeshDrawCommand.SortKey);

					NewVisibleMeshDrawCommand.PrimitiveIdBufferOffset = PrimitiveIdIndex;
					TempVisibleMeshDrawCommands.Emplace(MoveTemp(NewVisibleMeshDrawCommand));

					CurrentDynamicallyInstancedMeshCommandNumInstances = &NewCommand.NumInstances;
				}
				else
				{
					CurrentDynamicallyInstancedMeshCommandNumInstances = nullptr;
					FVisibleMeshDrawCommand NewVisibleMeshDrawCommand = VisibleMeshDrawCommand;
					NewVisibleMeshDrawCommand.PrimitiveIdBufferOffset = PrimitiveIdIndex;
					TempVisibleMeshDrawCommands.Emplace(MoveTemp(NewVisibleMeshDrawCommand));
				}
			}

			//@todo - refactor into instance step rate in the RHI
			for (uint32 InstanceFactorIndex = 0; InstanceFactorIndex < InstanceFactor; InstanceFactorIndex++, PrimitiveIdIndex++)
			{
				//@todo - refactor into memcpy
				checkSlow(PrimitiveIdIndex < MaxPrimitiveId);
				if (!GPUSceneUseTexture2D(ShaderPlatform))
				{
					PrimitiveIds[PrimitiveIdIndex] = VisibleMeshDrawCommand.DrawPrimitiveId;
				}
				else
				{
					//Packing for mobile texture2D GPUScene. Must be in sync with SceneData.ush
					uint16 PrimitivesPerTextureLine = FPrimitiveSceneShaderData::GetPrimitivesPerTextureLine();
					PrimitiveIds[PrimitiveIdIndex] = ((VisibleMeshDrawCommand.DrawPrimitiveId / PrimitivesPerTextureLine) << 16) | (VisibleMeshDrawCommand.DrawPrimitiveId % PrimitivesPerTextureLine);
				}
			}
		}

		// Setup instancing stats for logging.
		VisibleMeshDrawCommandsNum = VisibleMeshDrawCommands.Num();
		NewPassVisibleMeshDrawCommandsNum = TempVisibleMeshDrawCommands.Num();

		// Replace VisibleMeshDrawCommands
		FMemory::Memswap(&VisibleMeshDrawCommands, &TempVisibleMeshDrawCommands, sizeof(TempVisibleMeshDrawCommands));
		TempVisibleMeshDrawCommands.Reset();
	}
	else
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_BuildVisibleMeshDrawCommandPrimitiveIdBuffers);

		for (int32 DrawCommandIndex = 0; DrawCommandIndex < NumDrawCommands; DrawCommandIndex++)
		{
			const FVisibleMeshDrawCommand& VisibleMeshDrawCommand = VisibleMeshDrawCommands[DrawCommandIndex];
			for (uint32 InstanceFactorIndex = 0; InstanceFactorIndex < InstanceFactor; InstanceFactorIndex++, PrimitiveIdIndex++)
			{
				checkSlow(PrimitiveIdIndex < MaxPrimitiveId);
				PrimitiveIds[PrimitiveIdIndex] = VisibleMeshDrawCommand.DrawPrimitiveId;
			}
		}
	}
}

/**
 * Converts each FMeshBatch into a set of FMeshDrawCommands for a specific mesh pass type.
 */
void GenerateDynamicMeshDrawCommands(
	const FViewInfo& View,
	EShadingPath ShadingPath,
	EMeshPass::Type PassType,
	FMeshPassProcessor* PassMeshProcessor,
	const TArray<FMeshBatchAndRelevance, SceneRenderingAllocator>& DynamicMeshElements,
	const TArray<FMeshPassMask, SceneRenderingAllocator>* DynamicMeshElementsPassRelevance,
	int32 MaxNumDynamicMeshElements,
	const TArray<const FStaticMeshBatch*, SceneRenderingAllocator>& DynamicMeshCommandBuildRequests,
	int32 MaxNumBuildRequestElements,
	FMeshCommandOneFrameArray& VisibleCommands,
	FDynamicMeshDrawCommandStorage& MeshDrawCommandStorage,
	FGraphicsMinimalPipelineStateSet& MinimalPipelineStatePassSet,
	bool& NeedsShaderInitialisation
)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_GenerateDynamicMeshDrawCommands);
	check(PassMeshProcessor);
	check((PassType == EMeshPass::Num) == (DynamicMeshElementsPassRelevance == nullptr));

	FDynamicPassMeshDrawListContext DynamicPassMeshDrawListContext(
		MeshDrawCommandStorage,
		VisibleCommands,
		MinimalPipelineStatePassSet,
		NeedsShaderInitialisation
	);
	PassMeshProcessor->SetDrawListContext(&DynamicPassMeshDrawListContext);

	{
		const int32 NumCommandsBefore = VisibleCommands.Num();
		const int32 NumDynamicMeshBatches = DynamicMeshElements.Num();

		for (int32 MeshIndex = 0; MeshIndex < NumDynamicMeshBatches; MeshIndex++)
		{
			if (!DynamicMeshElementsPassRelevance || (*DynamicMeshElementsPassRelevance)[MeshIndex].Get(PassType))
			{
				const FMeshBatchAndRelevance& MeshAndRelevance = DynamicMeshElements[MeshIndex];
				const uint64 BatchElementMask = ~0ull;

				PassMeshProcessor->AddMeshBatch(*MeshAndRelevance.Mesh, BatchElementMask, MeshAndRelevance.PrimitiveSceneProxy);
			}
		}

		const int32 NumCommandsGenerated = VisibleCommands.Num() - NumCommandsBefore;
		checkf(NumCommandsGenerated <= MaxNumDynamicMeshElements,
			TEXT("Generated %d mesh draw commands for DynamicMeshElements, while preallocating resources only for %d of them."), NumCommandsGenerated, MaxNumDynamicMeshElements);
	}

	{
		const int32 NumCommandsBefore = VisibleCommands.Num();
		const int32 NumStaticMeshBatches = DynamicMeshCommandBuildRequests.Num();

		for (int32 MeshIndex = 0; MeshIndex < NumStaticMeshBatches; MeshIndex++)
		{
			const FStaticMeshBatch* StaticMeshBatch = DynamicMeshCommandBuildRequests[MeshIndex];
			const uint64 DefaultBatchElementMask = ~0ul;
			PassMeshProcessor->AddMeshBatch(*StaticMeshBatch, DefaultBatchElementMask, StaticMeshBatch->PrimitiveSceneInfo->Proxy, StaticMeshBatch->Id);
		}

		const int32 NumCommandsGenerated = VisibleCommands.Num() - NumCommandsBefore;
		checkf(NumCommandsGenerated <= MaxNumBuildRequestElements,
			TEXT("Generated %d mesh draw commands for DynamicMeshCommandBuildRequests, while preallocating resources only for %d of them."), NumCommandsGenerated, MaxNumBuildRequestElements);
	}
}

/**
 * Special version of GenerateDynamicMeshDrawCommands for the mobile base pass.
 * Based on CSM visibility it will generate mesh draw commands using either normal base pass processor or CSM base pass processor.
*/
void GenerateMobileBasePassDynamicMeshDrawCommands(
	const FViewInfo& View,
	EShadingPath ShadingPath,
	EMeshPass::Type PassType,
	FMeshPassProcessor* PassMeshProcessor,
	FMeshPassProcessor* MobilePassCSMPassMeshProcessor,
	const TArray<FMeshBatchAndRelevance, SceneRenderingAllocator>& DynamicMeshElements,
	const TArray<FMeshPassMask, SceneRenderingAllocator>* DynamicMeshElementsPassRelevance,
	int32 MaxNumDynamicMeshElements,
	const TArray<const FStaticMeshBatch*, SceneRenderingAllocator>& DynamicMeshCommandBuildRequests,
	int32 MaxNumBuildRequestElements,
	FMeshCommandOneFrameArray& VisibleCommands,
	FDynamicMeshDrawCommandStorage& MeshDrawCommandStorage,
	FGraphicsMinimalPipelineStateSet& GraphicsMinimalPipelineStateSet,
	bool& NeedsShaderInitialisation
)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_GenerateMobileBasePassDynamicMeshDrawCommands);
	check(PassMeshProcessor && MobilePassCSMPassMeshProcessor);
	check((PassType == EMeshPass::Num) == (DynamicMeshElementsPassRelevance == nullptr));

	FDynamicPassMeshDrawListContext DynamicPassMeshDrawListContext(
		MeshDrawCommandStorage,
		VisibleCommands,
		GraphicsMinimalPipelineStateSet,
		NeedsShaderInitialisation
	);
	PassMeshProcessor->SetDrawListContext(&DynamicPassMeshDrawListContext);
	MobilePassCSMPassMeshProcessor->SetDrawListContext(&DynamicPassMeshDrawListContext);

	const FMobileCSMVisibilityInfo& MobileCSMVisibilityInfo = View.MobileCSMVisibilityInfo;
	
	{
		const int32 NumCommandsBefore = VisibleCommands.Num();
		const int32 NumDynamicMeshBatches = DynamicMeshElements.Num();

		for (int32 MeshIndex = 0; MeshIndex < NumDynamicMeshBatches; MeshIndex++)
		{
			if (!DynamicMeshElementsPassRelevance || (*DynamicMeshElementsPassRelevance)[MeshIndex].Get(PassType))
			{
				const FMeshBatchAndRelevance& MeshAndRelevance = DynamicMeshElements[MeshIndex];
				const uint64 BatchElementMask = ~0ull;

				const int32 PrimitiveIndex = MeshAndRelevance.PrimitiveSceneProxy->GetPrimitiveSceneInfo()->GetIndex();
				if (MobileCSMVisibilityInfo.bMobileDynamicCSMInUse
					&& (MobileCSMVisibilityInfo.bAlwaysUseCSM || MobileCSMVisibilityInfo.MobilePrimitiveCSMReceiverVisibilityMap[PrimitiveIndex]))
				{
					MobilePassCSMPassMeshProcessor->AddMeshBatch(*MeshAndRelevance.Mesh, BatchElementMask, MeshAndRelevance.PrimitiveSceneProxy);
				}
				else
				{
					PassMeshProcessor->AddMeshBatch(*MeshAndRelevance.Mesh, BatchElementMask, MeshAndRelevance.PrimitiveSceneProxy);
				}
			}
		}

		const int32 NumCommandsGenerated = VisibleCommands.Num() - NumCommandsBefore;
		checkf(NumCommandsGenerated <= MaxNumDynamicMeshElements,
			TEXT("Generated %d mesh draw commands for DynamicMeshElements, while preallocating resources only for %d of them."), NumCommandsGenerated, MaxNumDynamicMeshElements);
	}

	{
		const int32 NumCommandsBefore = VisibleCommands.Num();
		const int32 NumStaticMeshBatches = DynamicMeshCommandBuildRequests.Num();

		for (int32 MeshIndex = 0; MeshIndex < NumStaticMeshBatches; MeshIndex++)
		{
			const FStaticMeshBatch* StaticMeshBatch = DynamicMeshCommandBuildRequests[MeshIndex];

			const int32 PrimitiveIndex = StaticMeshBatch->PrimitiveSceneInfo->Proxy->GetPrimitiveSceneInfo()->GetIndex();
			if (MobileCSMVisibilityInfo.bMobileDynamicCSMInUse
				&& (MobileCSMVisibilityInfo.bAlwaysUseCSM || MobileCSMVisibilityInfo.MobilePrimitiveCSMReceiverVisibilityMap[PrimitiveIndex]))
			{
				const uint64 DefaultBatchElementMask = ~0ul;
				MobilePassCSMPassMeshProcessor->AddMeshBatch(*StaticMeshBatch, DefaultBatchElementMask, StaticMeshBatch->PrimitiveSceneInfo->Proxy, StaticMeshBatch->Id);
			}
			else
			{
				const uint64 DefaultBatchElementMask = ~0ul;
				PassMeshProcessor->AddMeshBatch(*StaticMeshBatch, DefaultBatchElementMask, StaticMeshBatch->PrimitiveSceneInfo->Proxy, StaticMeshBatch->Id);
			}
		}

		const int32 NumCommandsGenerated = VisibleCommands.Num() - NumCommandsBefore;
		checkf(NumCommandsGenerated <= MaxNumBuildRequestElements,
			TEXT("Generated %d mesh draw commands for DynamicMeshCommandBuildRequests, while preallocating resources only for %d of them."), NumCommandsGenerated, MaxNumBuildRequestElements);
	}
}

/**
* Apply view overrides to existing mesh draw commands (e.g. reverse culling mode for rendering planar reflections).
* TempVisibleMeshDrawCommands must be presized for NewPassVisibleMeshDrawCommands.
*/
void ApplyViewOverridesToMeshDrawCommands(
	EShadingPath ShadingPath,
	EMeshPass::Type PassType,
	bool bReverseCulling,
	bool bRenderSceneTwoSided,
	FExclusiveDepthStencil::Type BasePassDepthStencilAccess,
	FExclusiveDepthStencil::Type DefaultBasePassDepthStencilAccess,
	FMeshCommandOneFrameArray& VisibleMeshDrawCommands,
	FDynamicMeshDrawCommandStorage& MeshDrawCommandStorage,
	FGraphicsMinimalPipelineStateSet& MinimalPipelineStatePassSet,
	bool& NeedsShaderInitialisation,
	FMeshCommandOneFrameArray& TempVisibleMeshDrawCommands
	)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_ApplyViewOverridesToMeshDrawCommands);
	check(VisibleMeshDrawCommands.Num() <= TempVisibleMeshDrawCommands.Max() && TempVisibleMeshDrawCommands.Num() == 0 && PassType != EMeshPass::Num);

	if ((FPassProcessorManager::GetPassFlags(ShadingPath, PassType) & EMeshPassFlags::MainView) != EMeshPassFlags::None)
	{
		if (bReverseCulling || bRenderSceneTwoSided || (BasePassDepthStencilAccess != DefaultBasePassDepthStencilAccess && PassType == EMeshPass::BasePass))
		{
			for (int32 MeshCommandIndex = 0; MeshCommandIndex < VisibleMeshDrawCommands.Num(); MeshCommandIndex++)
			{
				MeshDrawCommandStorage.MeshDrawCommands.Add(1);
				FMeshDrawCommand& NewMeshCommand = MeshDrawCommandStorage.MeshDrawCommands[MeshDrawCommandStorage.MeshDrawCommands.Num() - 1];

				const FVisibleMeshDrawCommand& VisibleMeshDrawCommand = VisibleMeshDrawCommands[MeshCommandIndex];
				const FMeshDrawCommand& MeshCommand = *VisibleMeshDrawCommand.MeshDrawCommand;
				NewMeshCommand = MeshCommand;

				const ERasterizerCullMode LocalCullMode = bRenderSceneTwoSided ? CM_None : bReverseCulling ? FMeshPassProcessor::InverseCullMode(VisibleMeshDrawCommand.MeshCullMode) : VisibleMeshDrawCommand.MeshCullMode;

				FGraphicsMinimalPipelineStateInitializer PipelineState = MeshCommand.CachedPipelineId.GetPipelineState(MinimalPipelineStatePassSet);
				PipelineState.RasterizerState = GetStaticRasterizerState<true>(VisibleMeshDrawCommand.MeshFillMode, LocalCullMode);

				if (BasePassDepthStencilAccess != DefaultBasePassDepthStencilAccess && PassType == EMeshPass::BasePass)
				{
					FMeshPassProcessorRenderState PassDrawRenderState;
					SetupBasePassState(BasePassDepthStencilAccess, false, PassDrawRenderState);
					PipelineState.DepthStencilState = PassDrawRenderState.GetDepthStencilState();
				}

				const FGraphicsMinimalPipelineStateId PipelineId = FGraphicsMinimalPipelineStateId::GetPipelineStateId(PipelineState, MinimalPipelineStatePassSet, NeedsShaderInitialisation);
				NewMeshCommand.Finalize(PipelineId, nullptr);

				FVisibleMeshDrawCommand NewVisibleMeshDrawCommand;

				NewVisibleMeshDrawCommand.Setup(
					&NewMeshCommand,
					VisibleMeshDrawCommand.DrawPrimitiveId,
					VisibleMeshDrawCommand.ScenePrimitiveId,
					VisibleMeshDrawCommand.StateBucketId,
					VisibleMeshDrawCommand.MeshFillMode,
					VisibleMeshDrawCommand.MeshCullMode,
					VisibleMeshDrawCommand.SortKey);

				TempVisibleMeshDrawCommands.Add(NewVisibleMeshDrawCommand);
			}

			// Replace VisibleMeshDrawCommands
			FMemory::Memswap(&VisibleMeshDrawCommands, &TempVisibleMeshDrawCommands, sizeof(TempVisibleMeshDrawCommands));
			TempVisibleMeshDrawCommands.Reset();
		}
	}
}

FAutoConsoleTaskPriority CPrio_FMeshDrawCommandPassSetupTask(
	TEXT("TaskGraph.TaskPriorities.FMeshDrawCommandPassSetupTask"),
	TEXT("Task and thread priority for FMeshDrawCommandPassSetupTask."),
	ENamedThreads::NormalThreadPriority,
	ENamedThreads::HighTaskPriority
);

/**
 * Task for a parallel setup of mesh draw commands. Includes generation of dynamic mesh draw commands, sorting, merging etc.
 */
class FMeshDrawCommandPassSetupTask
{
public:
	FMeshDrawCommandPassSetupTask(FMeshDrawCommandPassSetupTaskContext& InContext)
		: Context(InContext)
	{
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FMeshDrawCommandPassSetupTask, STATGROUP_TaskGraphTasks);
	}

	ENamedThreads::Type GetDesiredThread()
	{
		return CPrio_FMeshDrawCommandPassSetupTask.Get();
	}

	static ESubsequentsMode::Type GetSubsequentsMode() 
	{ 
		return ESubsequentsMode::TrackSubsequents; 
	}

	void AnyThreadTask()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MeshDrawCommandPassSetupTask);
		// Mobile base pass is a special case, as final lists is created from two mesh passes based on CSM visibility.
		const bool bMobileShadingBasePass = Context.ShadingPath == EShadingPath::Mobile && Context.PassType == EMeshPass::BasePass;
		// On SM5 Mobile platform, still want the same sorting
		const bool bMobileVulkanSM5BasePass = IsVulkanMobileSM5Platform(Context.ShaderPlatform) && Context.PassType == EMeshPass::BasePass;

		if (bMobileShadingBasePass)
		{
			MergeMobileBasePassMeshDrawCommands(
				Context.View->MobileCSMVisibilityInfo,
				Context.PrimitiveBounds->Num(),
				Context.MeshDrawCommands,
				Context.MobileBasePassCSMMeshDrawCommands
			);

			GenerateMobileBasePassDynamicMeshDrawCommands(
				*Context.View,
				Context.ShadingPath,
				Context.PassType,
				Context.MeshPassProcessor,
				Context.MobileBasePassCSMMeshPassProcessor,
				*Context.DynamicMeshElements,
				Context.DynamicMeshElementsPassRelevance,
				Context.NumDynamicMeshElements,
				Context.DynamicMeshCommandBuildRequests,
				Context.NumDynamicMeshCommandBuildRequestElements,
				Context.MeshDrawCommands,
				Context.MeshDrawCommandStorage,
				Context.MinimalPipelineStatePassSet,
				Context.NeedsShaderInitialisation
			);
		}
		else
		{
			GenerateDynamicMeshDrawCommands(
				*Context.View,
				Context.ShadingPath,
				Context.PassType,
				Context.MeshPassProcessor,
				*Context.DynamicMeshElements,
				Context.DynamicMeshElementsPassRelevance,
				Context.NumDynamicMeshElements,
				Context.DynamicMeshCommandBuildRequests,
				Context.NumDynamicMeshCommandBuildRequestElements,
				Context.MeshDrawCommands,
				Context.MeshDrawCommandStorage,
				Context.MinimalPipelineStatePassSet,
				Context.NeedsShaderInitialisation
			);
		}

		if (Context.MeshDrawCommands.Num() > 0)
		{
			if (Context.PassType != EMeshPass::Num)
			{
				ApplyViewOverridesToMeshDrawCommands(
					Context.ShadingPath,
					Context.PassType,
					Context.bReverseCulling,
					Context.bRenderSceneTwoSided,
					Context.BasePassDepthStencilAccess,
					Context.DefaultBasePassDepthStencilAccess,
					Context.MeshDrawCommands,
					Context.MeshDrawCommandStorage,
					Context.MinimalPipelineStatePassSet,
					Context.NeedsShaderInitialisation,
					Context.TempVisibleMeshDrawCommands
				);
			}

			// Update sort keys.
			if (bMobileShadingBasePass || bMobileVulkanSM5BasePass)
			{
				UpdateMobileBasePassMeshSortKeys(
					Context.ViewOrigin,
					*Context.PrimitiveBounds,
					Context.MeshDrawCommands
					);
			}
			else if (Context.TranslucencyPass != ETranslucencyPass::TPT_MAX)
			{
				UpdateTranslucentMeshSortKeys(
					Context.TranslucentSortPolicy,
					Context.TranslucentSortAxis,
					Context.ViewOrigin,
					Context.ViewMatrix,
					*Context.PrimitiveBounds,
					Context.TranslucencyPass,
					Context.MeshDrawCommands
				);
			}

			{
				QUICK_SCOPE_CYCLE_COUNTER(STAT_SortVisibleMeshDrawCommands);
				Context.MeshDrawCommands.Sort(FCompareFMeshDrawCommands());
			}

			{
				QUICK_SCOPE_CYCLE_COUNTER(STAT_BatchInstancingMeshDrawCommands);
				BatchInstancingMeshDrawCommands(
					Context.MeshDrawCommands,
					Context.MeshDrawCommandStorage,
					Context.TempVisibleMeshDrawCommands,
					Context.VisibleMeshDrawCommandsNum,
					Context.NewPassVisibleMeshDrawCommandsNum
				);
			}

			if (Context.bUseGPUScene)
			{
				BuildMeshDrawCommandPrimitiveIdBuffer(
					Context.bDynamicInstancing,
					Context.MeshDrawCommands,
					Context.MeshDrawCommandStorage,
					Context.PrimitiveIdBufferData,
					Context.PrimitiveIdBufferDataSize,
					Context.TempVisibleMeshDrawCommands,
					Context.MaxInstances,
					Context.VisibleMeshDrawCommandsNum,
					Context.NewPassVisibleMeshDrawCommandsNum,
					Context.ShaderPlatform,
					Context.InstanceFactor
				);
			}
		}
	}

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		AnyThreadTask();
	}


private:
	FMeshDrawCommandPassSetupTaskContext& Context;
};

/**
 * Task for shader initialization. This will run on the RenderThread after Commands have been generated.
 */
class FMeshDrawCommandInitResourcesTask
{
public:
	FMeshDrawCommandInitResourcesTask(FMeshDrawCommandPassSetupTaskContext& InContext)
		: Context(InContext)
	{
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FMeshDrawCommandInitResourcesTask, STATGROUP_TaskGraphTasks);
	}

	ENamedThreads::Type GetDesiredThread()
	{
		return ENamedThreads::GetRenderThread_Local();
	}

	static ESubsequentsMode::Type GetSubsequentsMode()
	{
		return ESubsequentsMode::TrackSubsequents;
	}

	void AnyThreadTask()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MeshDrawCommandInitResourcesTask);
		if (Context.NeedsShaderInitialisation)
		{
			for (const FGraphicsMinimalPipelineStateInitializer& Initializer : Context.MinimalPipelineStatePassSet)
			{
				Initializer.BoundShaderState.LazilyInitShaders();
			}
		}
	}

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		AnyThreadTask();
	}


private:
	FMeshDrawCommandPassSetupTaskContext& Context;
};

/*
 * Used by various dynamic passes to sort/merge mesh draw commands immediately on a rendering thread.
 */
void SortAndMergeDynamicPassMeshDrawCommands(
	ERHIFeatureLevel::Type FeatureLevel,
	FMeshCommandOneFrameArray& VisibleMeshDrawCommands,
	FDynamicMeshDrawCommandStorage& MeshDrawCommandStorage,
	FRHIVertexBuffer*& OutPrimitiveIdVertexBuffer,
	uint32 InstanceFactor)
{
	const bool bUseGPUScene = UseGPUScene(GMaxRHIShaderPlatform, FeatureLevel);

	const int32 NumDrawCommands = VisibleMeshDrawCommands.Num();
	if (NumDrawCommands > 0)
	{
		FMeshCommandOneFrameArray NewPassVisibleMeshDrawCommands;
		int32 MaxInstances = 1;
		int32 VisibleMeshDrawCommandsNum = 0;
		int32 NewPassVisibleMeshDrawCommandsNum = 0;

		VisibleMeshDrawCommands.Sort(FCompareFMeshDrawCommands());

		if (bUseGPUScene)
		{
			const bool bDynamicInstancing = IsDynamicInstancingEnabled(FeatureLevel);
			if (bDynamicInstancing)
			{
				NewPassVisibleMeshDrawCommands.Empty(NumDrawCommands);
			}

			const int32 PrimitiveIdBufferDataSize = InstanceFactor * NumDrawCommands * sizeof(int32);
			FPrimitiveIdVertexBufferPoolEntry Entry = GPrimitiveIdVertexBufferPool.Allocate(PrimitiveIdBufferDataSize);
			OutPrimitiveIdVertexBuffer = Entry.BufferRHI;
			void* PrimitiveIdBufferData = RHILockVertexBuffer(OutPrimitiveIdVertexBuffer, 0, PrimitiveIdBufferDataSize, RLM_WriteOnly);

			BuildMeshDrawCommandPrimitiveIdBuffer(
				bDynamicInstancing,
				VisibleMeshDrawCommands,
				MeshDrawCommandStorage,
				PrimitiveIdBufferData,
				PrimitiveIdBufferDataSize,
				NewPassVisibleMeshDrawCommands,
				MaxInstances,
				VisibleMeshDrawCommandsNum,
				NewPassVisibleMeshDrawCommandsNum,
				GShaderPlatformForFeatureLevel[FeatureLevel],
				InstanceFactor
			);

			RHIUnlockVertexBuffer(OutPrimitiveIdVertexBuffer);
			GPrimitiveIdVertexBufferPool.ReturnToFreeList(Entry);
		}
	}
}


void FParallelMeshDrawCommandPass::DispatchPassSetup(
	FScene* Scene,
	const FViewInfo& View,
	EMeshPass::Type PassType,
	FExclusiveDepthStencil::Type BasePassDepthStencilAccess,
	FMeshPassProcessor* MeshPassProcessor,
	const TArray<FMeshBatchAndRelevance, SceneRenderingAllocator>& DynamicMeshElements,
	const TArray<FMeshPassMask, SceneRenderingAllocator>* DynamicMeshElementsPassRelevance,
	int32 NumDynamicMeshElements,
	TArray<const FStaticMeshBatch*, SceneRenderingAllocator>& InOutDynamicMeshCommandBuildRequests,
	int32 NumDynamicMeshCommandBuildRequestElements,
	FMeshCommandOneFrameArray& InOutMeshDrawCommands,
	FMeshPassProcessor* MobileBasePassCSMMeshPassProcessor,
	FMeshCommandOneFrameArray* InOutMobileBasePassCSMMeshDrawCommands
)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ParallelMdcDispatchPassSetup);
	check(!TaskEventRef.IsValid() && MeshPassProcessor != nullptr && TaskContext.PrimitiveIdBufferData == nullptr);
	check((PassType == EMeshPass::Num) == (DynamicMeshElementsPassRelevance == nullptr));

	MaxNumDraws = InOutMeshDrawCommands.Num() + NumDynamicMeshElements + NumDynamicMeshCommandBuildRequestElements;

	TaskContext.MeshPassProcessor = MeshPassProcessor;
	TaskContext.MobileBasePassCSMMeshPassProcessor = MobileBasePassCSMMeshPassProcessor;
	TaskContext.DynamicMeshElements = &DynamicMeshElements;
	TaskContext.DynamicMeshElementsPassRelevance = DynamicMeshElementsPassRelevance;

	TaskContext.View = &View;
	TaskContext.ShadingPath = Scene->GetShadingPath();
	TaskContext.ShaderPlatform = Scene->GetShaderPlatform();
	TaskContext.PassType = PassType;
	TaskContext.bUseGPUScene = UseGPUScene(GMaxRHIShaderPlatform, View.GetFeatureLevel());
	TaskContext.bDynamicInstancing = IsDynamicInstancingEnabled(View.GetFeatureLevel());
	TaskContext.bReverseCulling = View.bReverseCulling;
	TaskContext.bRenderSceneTwoSided = View.bRenderSceneTwoSided;
	TaskContext.BasePassDepthStencilAccess = BasePassDepthStencilAccess;
	TaskContext.DefaultBasePassDepthStencilAccess = Scene->DefaultBasePassDepthStencilAccess;
	TaskContext.NumDynamicMeshElements = NumDynamicMeshElements;
	TaskContext.NumDynamicMeshCommandBuildRequestElements = NumDynamicMeshCommandBuildRequestElements;

	// Only apply instancing for ISR to main view passes
	const bool bIsMainViewPass = PassType != EMeshPass::Num && (FPassProcessorManager::GetPassFlags(TaskContext.ShadingPath, TaskContext.PassType) & EMeshPassFlags::MainView) != EMeshPassFlags::None;
	TaskContext.InstanceFactor = (bIsMainViewPass && View.IsInstancedStereoPass()) ? 2 : 1;

	// Setup translucency sort key update pass based on view.
	TaskContext.TranslucencyPass = ETranslucencyPass::TPT_MAX;
	TaskContext.TranslucentSortPolicy = View.TranslucentSortPolicy;
	TaskContext.TranslucentSortAxis = View.TranslucentSortAxis;
	TaskContext.ViewOrigin = View.ViewMatrices.GetViewOrigin();
	TaskContext.ViewMatrix = View.ViewMatrices.GetViewMatrix();
	TaskContext.PrimitiveBounds = &Scene->PrimitiveBounds;

	switch (PassType)
	{
		case EMeshPass::TranslucencyStandard: TaskContext.TranslucencyPass = ETranslucencyPass::TPT_StandardTranslucency; break;
		case EMeshPass::TranslucencyAfterDOF: TaskContext.TranslucencyPass = ETranslucencyPass::TPT_TranslucencyAfterDOF; break;
		case EMeshPass::TranslucencyAfterDOFModulate: TaskContext.TranslucencyPass = ETranslucencyPass::TPT_TranslucencyAfterDOFModulate; break;
		case EMeshPass::TranslucencyAll: TaskContext.TranslucencyPass = ETranslucencyPass::TPT_AllTranslucency; break;
		case EMeshPass::MobileInverseOpacity: TaskContext.TranslucencyPass = ETranslucencyPass::TPT_StandardTranslucency; break;
	}

	FMemory::Memswap(&TaskContext.MeshDrawCommands, &InOutMeshDrawCommands, sizeof(InOutMeshDrawCommands));
	FMemory::Memswap(&TaskContext.DynamicMeshCommandBuildRequests, &InOutDynamicMeshCommandBuildRequests, sizeof(InOutDynamicMeshCommandBuildRequests));

	if (TaskContext.ShadingPath == EShadingPath::Mobile && TaskContext.PassType == EMeshPass::BasePass)
	{
		FMemory::Memswap(&TaskContext.MobileBasePassCSMMeshDrawCommands, InOutMobileBasePassCSMMeshDrawCommands, sizeof(*InOutMobileBasePassCSMMeshDrawCommands));
	}
	else
	{
		check(MobileBasePassCSMMeshPassProcessor == nullptr && InOutMobileBasePassCSMMeshDrawCommands == nullptr);
	}

	if (MaxNumDraws > 0)
	{
		// Preallocate resources on rendering thread based on MaxNumDraws.
		bPrimitiveIdBufferDataOwnedByRHIThread = false;
		TaskContext.PrimitiveIdBufferDataSize = TaskContext.InstanceFactor * MaxNumDraws * sizeof(int32);
		TaskContext.PrimitiveIdBufferData = FMemory::Malloc(TaskContext.PrimitiveIdBufferDataSize);
		PrimitiveIdVertexBufferPoolEntry = GPrimitiveIdVertexBufferPool.Allocate(TaskContext.PrimitiveIdBufferDataSize);
		TaskContext.MeshDrawCommands.Reserve(MaxNumDraws);
		TaskContext.TempVisibleMeshDrawCommands.Reserve(MaxNumDraws);

		const bool bExecuteInParallel = FApp::ShouldUseThreadingForPerformance()
			&& CVarMeshDrawCommandsParallelPassSetup.GetValueOnRenderThread() > 0
			&& GIsThreadedRendering; // Rendering thread is required to safely use rendering resources in parallel.

		if (bExecuteInParallel)
		{
			if (GAllowOnDemandShaderCreation && RHISupportsMultithreadedShaderCreation(GMaxRHIShaderPlatform))
			{
				TaskEventRef = TGraphTask<FMeshDrawCommandPassSetupTask>::CreateTask(nullptr, ENamedThreads::GetRenderThread()).ConstructAndDispatchWhenReady(TaskContext);
			}
			else
			{
				FGraphEventArray DependentGraphEvents;
				DependentGraphEvents.Add(TGraphTask<FMeshDrawCommandPassSetupTask>::CreateTask(nullptr, ENamedThreads::GetRenderThread()).ConstructAndDispatchWhenReady(TaskContext));
				TaskEventRef = TGraphTask<FMeshDrawCommandInitResourcesTask>::CreateTask(&DependentGraphEvents, ENamedThreads::GetRenderThread()).ConstructAndDispatchWhenReady(TaskContext);
			}
		}
		else
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_MeshPassSetupImmediate);
			FMeshDrawCommandPassSetupTask Task(TaskContext);
			Task.AnyThreadTask();
			if (!GAllowOnDemandShaderCreation || !RHISupportsMultithreadedShaderCreation(GMaxRHIShaderPlatform))
			{
				FMeshDrawCommandInitResourcesTask DependentTask(TaskContext);
				DependentTask.AnyThreadTask();
			}
		}
	}
}

void FParallelMeshDrawCommandPass::WaitForMeshPassSetupTask() const
{
	if (TaskEventRef.IsValid())
	{
		// Need to wait on GetRenderThread_Local, as mesh pass setup task can wait on rendering thread inside InitResourceFromPossiblyParallelRendering().
		QUICK_SCOPE_CYCLE_COUNTER(STAT_WaitForMeshPassSetupTask);
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(TaskEventRef, ENamedThreads::GetRenderThread_Local());
	}
}

void FParallelMeshDrawCommandPass::WaitForTasksAndEmpty()
{
	// Need to wait in case if someone dispatched sort and draw merge task, but didn't draw it.
	WaitForMeshPassSetupTask();
	TaskEventRef = nullptr;

	DumpInstancingStats();

	if (TaskContext.MeshPassProcessor)
	{
		TaskContext.MeshPassProcessor->~FMeshPassProcessor();
		TaskContext.MeshPassProcessor = nullptr;
	}
	if (TaskContext.MobileBasePassCSMMeshPassProcessor)
	{
		TaskContext.MobileBasePassCSMMeshPassProcessor->~FMeshPassProcessor();
		TaskContext.MobileBasePassCSMMeshPassProcessor = nullptr;
	}

	if (MaxNumDraws > 0)
	{
		if (bPrimitiveIdBufferDataOwnedByRHIThread)
		{
			FRHICommandListExecutor::GetImmediateCommandList().EnqueueLambda([PrimitiveIdVertexBufferPoolEntry = PrimitiveIdVertexBufferPoolEntry](FRHICommandListImmediate&) {
				GPrimitiveIdVertexBufferPool.ReturnToFreeList(PrimitiveIdVertexBufferPoolEntry);
			});
		}
		else
		{
			GPrimitiveIdVertexBufferPool.ReturnToFreeList(PrimitiveIdVertexBufferPoolEntry);
		}
	}

	if (!bPrimitiveIdBufferDataOwnedByRHIThread)
	{
		FMemory::Free(TaskContext.PrimitiveIdBufferData);
	}

	bPrimitiveIdBufferDataOwnedByRHIThread = false;
	MaxNumDraws = 0;
	PassNameForStats.Empty();

	TaskContext.DynamicMeshElements = nullptr;
	TaskContext.DynamicMeshElementsPassRelevance = nullptr;
	TaskContext.MeshDrawCommands.Empty();
	TaskContext.MeshDrawCommandStorage.MeshDrawCommands.Empty();
	FGraphicsMinimalPipelineStateId::AddSizeToLocalPipelineIdTableSize(TaskContext.MinimalPipelineStatePassSet.GetAllocatedSize());
	TaskContext.MinimalPipelineStatePassSet.Empty();
	TaskContext.MobileBasePassCSMMeshDrawCommands.Empty();
	TaskContext.DynamicMeshCommandBuildRequests.Empty();
	TaskContext.TempVisibleMeshDrawCommands.Empty();
	TaskContext.PrimitiveIdBufferData = nullptr;
	TaskContext.PrimitiveIdBufferDataSize = 0;
}

FParallelMeshDrawCommandPass::~FParallelMeshDrawCommandPass()
{
	check(TaskEventRef == nullptr);
}

class FDrawVisibleMeshCommandsAnyThreadTask : public FRenderTask
{
	FRHICommandList& RHICmdList;
	const FMeshCommandOneFrameArray& VisibleMeshDrawCommands;
	const FGraphicsMinimalPipelineStateSet& GraphicsMinimalPipelineStateSet;
	FRHIVertexBuffer* PrimitiveIdsBuffer;
	int32 BasePrimitiveIdsOffset;
	bool bDynamicInstancing;
	uint32 InstanceFactor;
	int32 TaskIndex;
	int32 TaskNum;

public:

	FDrawVisibleMeshCommandsAnyThreadTask(
		FRHICommandList& InRHICmdList,
		const FMeshCommandOneFrameArray& InVisibleMeshDrawCommands,
		const FGraphicsMinimalPipelineStateSet& InGraphicsMinimalPipelineStateSet,
		FRHIVertexBuffer* InPrimitiveIdsBuffer,
		int32 InBasePrimitiveIdsOffset,
		bool bInDynamicInstancing,
		uint32 InInstanceFactor,
		int32 InTaskIndex,
		int32 InTaskNum
	)
		: RHICmdList(InRHICmdList)
		, VisibleMeshDrawCommands(InVisibleMeshDrawCommands)
		, GraphicsMinimalPipelineStateSet(InGraphicsMinimalPipelineStateSet)
		, PrimitiveIdsBuffer(InPrimitiveIdsBuffer)
		, BasePrimitiveIdsOffset(InBasePrimitiveIdsOffset)
		, bDynamicInstancing(bInDynamicInstancing)
		, InstanceFactor(InInstanceFactor)
		, TaskIndex(InTaskIndex)
		, TaskNum(InTaskNum)
	{}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FDrawVisibleMeshCommandsAnyThreadTask, STATGROUP_TaskGraphTasks);
	}

	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DrawVisibleMeshCommandsAnyThreadTask);
		checkSlow(RHICmdList.IsInsideRenderPass());

		// Recompute draw range.
		const int32 DrawNum = VisibleMeshDrawCommands.Num();
		const int32 NumDrawsPerTask = TaskIndex < DrawNum ? FMath::DivideAndRoundUp(DrawNum, TaskNum) : 0;
		const int32 StartIndex = TaskIndex * NumDrawsPerTask;
		const int32 NumDraws = FMath::Min(NumDrawsPerTask, DrawNum - StartIndex);

		SubmitMeshDrawCommandsRange(VisibleMeshDrawCommands, GraphicsMinimalPipelineStateSet, PrimitiveIdsBuffer, BasePrimitiveIdsOffset, bDynamicInstancing, StartIndex, NumDraws, InstanceFactor, RHICmdList);

		RHICmdList.EndRenderPass();
		RHICmdList.HandleRTThreadTaskCompletion(MyCompletionGraphEvent);
	}
};

void FParallelMeshDrawCommandPass::DispatchDraw(FParallelCommandListSet* ParallelCommandListSet, FRHICommandList& RHICmdList) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ParallelMdcDispatchDraw);
	if (MaxNumDraws <= 0)
	{
		return;
	}

	FRHIVertexBuffer* PrimitiveIdsBuffer = PrimitiveIdVertexBufferPoolEntry.BufferRHI;
	const int32 BasePrimitiveIdsOffset = 0;

	if (ParallelCommandListSet)
	{
		if (TaskContext.bUseGPUScene)
		{
			// Queue a command on the RHI thread which will upload PrimitiveIdVertexBuffer after finishing FMeshDrawCommandPassSetupTask.
			FRHICommandListImmediate &RHICommandList = GetImmediateCommandList_ForRenderCommand();

			if (TaskEventRef.IsValid())
			{
				RHICommandList.AddDispatchPrerequisite(TaskEventRef);
			}

			RHICommandList.EnqueueLambda([
				VertexBuffer = PrimitiveIdsBuffer,
				VertexBufferData = TaskContext.PrimitiveIdBufferData, 
				VertexBufferDataSize = TaskContext.PrimitiveIdBufferDataSize,
				PrimitiveIdVertexBufferPoolEntry = PrimitiveIdVertexBufferPoolEntry](FRHICommandListImmediate& CmdList)
			{
				// Upload vertex buffer data.
				void* RESTRICT Data = (void* RESTRICT)CmdList.LockVertexBuffer(VertexBuffer, 0, VertexBufferDataSize, RLM_WriteOnly);
				FMemory::Memcpy(Data, VertexBufferData, VertexBufferDataSize);
				CmdList.UnlockVertexBuffer(VertexBuffer);

				FMemory::Free(VertexBufferData);
			});

			RHICommandList.RHIThreadFence(true);

			bPrimitiveIdBufferDataOwnedByRHIThread = true;
		}

		const ENamedThreads::Type RenderThread = ENamedThreads::GetRenderThread();

		FGraphEventArray Prereqs;
		if (ParallelCommandListSet->GetPrereqs())
		{
			Prereqs.Append(*ParallelCommandListSet->GetPrereqs());
		}
		if (TaskEventRef.IsValid())
		{
			Prereqs.Add(TaskEventRef);
		}

		// Distribute work evenly to the available task graph workers based on NumEstimatedDraws.
		// Every task will then adjust it's working range based on FVisibleMeshDrawCommandProcessTask results.
		const int32 NumThreads = FMath::Min<int32>(FTaskGraphInterface::Get().GetNumWorkerThreads(), ParallelCommandListSet->Width);
		const int32 NumTasks = FMath::Min<int32>(NumThreads, FMath::DivideAndRoundUp(MaxNumDraws, ParallelCommandListSet->MinDrawsPerCommandList));
		const int32 NumDrawsPerTask = FMath::DivideAndRoundUp(MaxNumDraws, NumTasks);

		for (int32 TaskIndex = 0; TaskIndex < NumTasks; TaskIndex++)
		{
			const int32 StartIndex = TaskIndex * NumDrawsPerTask;
			const int32 NumDraws = FMath::Min(NumDrawsPerTask, MaxNumDraws - StartIndex);
			checkSlow(NumDraws > 0);

			FRHICommandList* CmdList = ParallelCommandListSet->NewParallelCommandList();

			FGraphEventRef AnyThreadCompletionEvent = TGraphTask<FDrawVisibleMeshCommandsAnyThreadTask>::CreateTask(&Prereqs, RenderThread)
				.ConstructAndDispatchWhenReady(*CmdList, TaskContext.MeshDrawCommands, TaskContext.MinimalPipelineStatePassSet, PrimitiveIdsBuffer, BasePrimitiveIdsOffset, TaskContext.bDynamicInstancing, TaskContext.InstanceFactor, TaskIndex, NumTasks);
			ParallelCommandListSet->AddParallelCommandList(CmdList, AnyThreadCompletionEvent, NumDraws);
		}
	}
	else
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_MeshPassDrawImmediate);

		WaitForMeshPassSetupTask();

		if (TaskContext.bUseGPUScene)
		{
			// Can immediately upload vertex buffer data, as there is no parallel draw task.
			void* RESTRICT Data = RHILockVertexBuffer(PrimitiveIdVertexBufferPoolEntry.BufferRHI, 0, TaskContext.PrimitiveIdBufferDataSize, RLM_WriteOnly);
			FMemory::Memcpy(Data, TaskContext.PrimitiveIdBufferData, TaskContext.PrimitiveIdBufferDataSize);
			RHIUnlockVertexBuffer(PrimitiveIdVertexBufferPoolEntry.BufferRHI);
		}

		SubmitMeshDrawCommandsRange(TaskContext.MeshDrawCommands, TaskContext.MinimalPipelineStatePassSet, PrimitiveIdsBuffer, BasePrimitiveIdsOffset, TaskContext.bDynamicInstancing, 0, TaskContext.MeshDrawCommands.Num(), TaskContext.InstanceFactor, RHICmdList);
	}
}

void FParallelMeshDrawCommandPass::DumpInstancingStats() const
{
	if (!PassNameForStats.IsEmpty() && TaskContext.VisibleMeshDrawCommandsNum > 0)
	{
		UE_LOG(LogRenderer, Log, TEXT("Instancing stats for %s"), *PassNameForStats);
		UE_LOG(LogRenderer, Log, TEXT("   %i Mesh Draw Commands in %i instancing state buckets"), TaskContext.VisibleMeshDrawCommandsNum, TaskContext.NewPassVisibleMeshDrawCommandsNum);
		UE_LOG(LogRenderer, Log, TEXT("   Largest %i"), TaskContext.MaxInstances);
		UE_LOG(LogRenderer, Log, TEXT("   %.1f Dynamic Instancing draw call reduction factor"), TaskContext.VisibleMeshDrawCommandsNum / (float)TaskContext.NewPassVisibleMeshDrawCommandsNum);
	}
}

void FParallelMeshDrawCommandPass::SetDumpInstancingStats(const FString& InPassNameForStats)
{
	PassNameForStats = InPassNameForStats;
}