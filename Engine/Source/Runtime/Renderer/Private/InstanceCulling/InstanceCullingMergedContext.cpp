// Copyright Epic Games, Inc. All Rights Reserved.

#include "InstanceCulling/InstanceCullingMergedContext.h"
#include "InstanceCulling/InstanceCullingManager.h"

void FInstanceCullingMergedContext::MergeBatches()
{
	for (uint32 Mode = 0U; Mode < uint32(EBatchProcessingMode::Num); ++Mode)
	{
		LoadBalancers[Mode].ReserveStorage(TotalBatches[Mode], TotalItems[Mode]);
	}
	// Pre-size all arrays
	IndirectArgs.Empty(TotalIndirectArgs);
	DrawCommandDescs.Empty(TotalIndirectArgs);
	InstanceIdOffsets.Empty(TotalIndirectArgs);
	ViewIds.Empty(TotalViewIds);

	BatchInfos.AddDefaulted(Batches.Num());
	uint32 InstanceIdBufferOffset = 0U;
	uint32 InstanceDataByteOffset = 0U;
	const uint32 InstanceIdBufferStride = FInstanceCullingContext::GetInstanceIdBufferStride(FeatureLevel);

	// Index that maps from each command to the corresponding batch - maybe not the utmost efficiency
	for (int32 BatchIndex = 0; BatchIndex < Batches.Num(); ++BatchIndex)
	{
		const FBatchItem& BatchItem = Batches[BatchIndex];
		const FInstanceCullingContext& InstanceCullingContext = *BatchItem.Context;

		FContextBatchInfo& BatchInfo = BatchInfos[BatchIndex];

		BatchInfo.IndirectArgsOffset = IndirectArgs.Num();
		//BatchInfo.NumIndirectArgs = InstanceCullingContext.IndirectArgs.Num();
		IndirectArgs.Append(InstanceCullingContext.IndirectArgs);

		check(InstanceCullingContext.DrawCommandDescs.Num() == InstanceCullingContext.IndirectArgs.Num());
		DrawCommandDescs.Append(InstanceCullingContext.DrawCommandDescs);

		check(InstanceCullingContext.InstanceIdOffsets.Num() == InstanceCullingContext.IndirectArgs.Num());
		InstanceIdOffsets.AddDefaulted(InstanceCullingContext.InstanceIdOffsets.Num());
		// TODO: perform offset on GPU
		// InstanceIdOffsets.Append(InstanceCullingContext.InstanceIdOffsets);
		for (int32 Index = 0; Index < InstanceCullingContext.InstanceIdOffsets.Num(); ++Index)
		{
			InstanceIdOffsets[BatchInfo.IndirectArgsOffset + Index] = InstanceCullingContext.InstanceIdOffsets[Index] + InstanceIdBufferOffset;
		}

		BatchInfo.ViewIdsOffset = ViewIds.Num();
		BatchInfo.NumViewIds = InstanceCullingContext.ViewIds.Num();
		ViewIds.Append(InstanceCullingContext.ViewIds);

		BatchInfo.DynamicInstanceIdOffset = BatchItem.DynamicInstanceIdOffset;
		BatchInfo.DynamicInstanceIdMax = BatchItem.DynamicInstanceIdOffset + BatchItem.DynamicInstanceIdNum;

		for (uint32 Mode = 0U; Mode < uint32(EBatchProcessingMode::Num); ++Mode)
		{
			int32 StartIndex = BatchInds[Mode].Num();
			TInstanceCullingLoadBalancer<SceneRenderingAllocator>* MergedLoadBalancer = &LoadBalancers[Mode];

			BatchInfo.ItemDataOffset[Mode] = MergedLoadBalancer->GetItems().Num();
			FInstanceProcessingGPULoadBalancer* LoadBalancer = InstanceCullingContext.LoadBalancers[Mode];
			LoadBalancer->FinalizeBatches();

			// UnCulled bucket is used for a single instance mode
			check(EBatchProcessingMode(Mode) != EBatchProcessingMode::UnCulled || LoadBalancer->HasSingleInstanceItemsOnly());

			BatchInds[Mode].AddDefaulted(LoadBalancer->GetBatches().Num());

			MergedLoadBalancer->AppendData(*LoadBalancer);
			for (int32 Index = StartIndex; Index < BatchInds[Mode].Num(); ++Index)
			{
				BatchInds[Mode][Index] = BatchIndex;
			}
		}
		const uint32 BatchTotalInstances = InstanceCullingContext.TotalInstances * InstanceCullingContext.ViewIds.Num();
		const uint32 BatchTotalDraws = InstanceCullingContext.InstanceIdOffsets.Num();

		FInstanceCullingDrawParams& Result = *BatchItem.Result;
		Result.InstanceDataByteOffset = InstanceDataByteOffset;
		Result.IndirectArgsByteOffset = BatchInfo.IndirectArgsOffset * FInstanceCullingContext::IndirectArgsNumWords * sizeof(uint32);

		BatchInfo.InstanceDataWriteOffset = InstanceIdBufferOffset;
		InstanceIdBufferOffset += BatchTotalInstances;
		// Advance offset into per-instance buffer
		InstanceDataByteOffset += FInstanceCullingContext::StepInstanceDataOffset(FeatureLevel, BatchTotalInstances, BatchTotalDraws) * InstanceIdBufferStride;
	}
}

void FInstanceCullingMergedContext::AddBatch(FRDGBuilder& GraphBuilder, const FInstanceCullingContext* Context, int32 DynamicInstanceIdOffset,	int32 DynamicInstanceIdNum, FInstanceCullingDrawParams* InstanceCullingDrawParams)
{
	Batches.Add(FBatchItem{ Context, InstanceCullingDrawParams, DynamicInstanceIdOffset, DynamicInstanceIdNum });

	const bool bOcclusionCullInstances = Context->PrevHZB.IsValid() && FInstanceCullingContext::IsOcclusionCullingEnabled();

	// Set HZB texture for the merged batches
	if (bOcclusionCullInstances)
	{
		// Verify that each batch contains the same HZB if not null as we only support one
		check(PrevHZB == nullptr || PrevHZB == GraphBuilder.RegisterExternalTexture(Context->PrevHZB));

		if (PrevHZB == nullptr)
		{
			// Note: performing the registration here because the final merge of contexts may happen during RDG execute (in case of the deferred culling) which seems ill-defined
			PrevHZB = GraphBuilder.RegisterExternalTexture(Context->PrevHZB);
		}
	}

	// Accumulate the totals so the deferred processing can pre-size the arrays
	for (uint32 Mode = 0U; Mode < uint32(EBatchProcessingMode::Num); ++Mode)
	{
		Context->LoadBalancers[Mode]->FinalizeBatches();
		TotalBatches[Mode] += Context->LoadBalancers[Mode]->GetBatches().Max();
		TotalItems[Mode] += Context->LoadBalancers[Mode]->GetItems().Max();
	}
#if DO_CHECK
	for (int32 ViewId : Context->ViewIds)
	{
		checkf(NumCullingViews < 0 || ViewId < NumCullingViews, TEXT("Attempting to defer a culling context that references a view that has not been uploaded yet."));
	}
#endif 

	TotalIndirectArgs += Context->IndirectArgs.Num();
	TotalViewIds += Context->ViewIds.Num();
	InstanceIdBufferSize += Context->TotalInstances * Context->ViewIds.Num();
	TotalInstances += Context->TotalInstances;
}