// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenScene.cpp
=============================================================================*/

#include "LumenMeshCards.h"
#include "RendererPrivate.h"
#include "Lumen.h"

extern void BuildMeshCardsDataForMergedInstances(const FPrimitiveSceneInfo* PrimitiveSceneInfo, FMeshCardsBuildData& MeshCardsBuildData);

int32 GLumenSceneCardAtlasSize = 4096;
FAutoConsoleVariableRef CVarLumenSceneCardAtlasSize(
	TEXT("r.LumenScene.CardAtlasSize"),
	GLumenSceneCardAtlasSize,
	TEXT("Physical card atlas size."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneUploadEveryFrame = 0;
FAutoConsoleVariableRef CVarLumenSceneUploadEveryFrame(
	TEXT("r.LumenScene.UploadEveryFrame"),
	GLumenSceneUploadEveryFrame,
	TEXT("Whether to upload the entire Lumen Scene's data every frame. Useful for debugging."),
	ECVF_RenderThreadSafe
);

class FLumenCardPageGPUData
{
public:
	// Must match usf
	enum { DataStrideInFloat4s = 3 };
	enum { DataStrideInBytes = DataStrideInFloat4s * sizeof(FVector4) };

	static void FillData(const FLumenPageTableEntry& RESTRICT PageTableEntry, FVector2D InvPhysicalAtlasSize, FVector4* RESTRICT OutData)
	{
		// Layout must match GetLumenCardPageData in usf

		OutData[0] = FVector4(*((float*)&PageTableEntry.CardIndex), 0.0f, 0.0f, 0.0f);
		OutData[1] = PageTableEntry.CardUVRect;

		OutData[2].X = PageTableEntry.PhysicalAtlasRect.Min.X * InvPhysicalAtlasSize.X;
		OutData[2].Y = PageTableEntry.PhysicalAtlasRect.Min.Y * InvPhysicalAtlasSize.Y;
		OutData[2].Z = PageTableEntry.PhysicalAtlasRect.Max.X * InvPhysicalAtlasSize.X;
		OutData[2].W = PageTableEntry.PhysicalAtlasRect.Max.Y * InvPhysicalAtlasSize.Y;

		static_assert(DataStrideInFloat4s == 3, "Data stride doesn't match");
	}
};

FIntPoint GetDesiredPhysicalAtlasSizeInPages()
{
	int32 AtlasSizeInPages = FMath::DivideAndRoundUp<uint32>(GLumenSceneCardAtlasSize, Lumen::PhysicalPageSize);
	AtlasSizeInPages = FMath::Clamp(AtlasSizeInPages, 1, 64);
	return FIntPoint(AtlasSizeInPages, AtlasSizeInPages);
}

FIntPoint GetDesiredPhysicalAtlasSize()
{
	return GetDesiredPhysicalAtlasSizeInPages() * Lumen::PhysicalPageSize;
}

FLumenSurfaceCacheAllocator::FPageBin::FPageBin(FIntPoint InElementSize)
{
	ensure(InElementSize.GetMax() <= Lumen::PhysicalPageSize);
	ElementSize = InElementSize;
	PageSizeInElements = FIntPoint(Lumen::PhysicalPageSize) / InElementSize;
}

void FLumenSurfaceCacheAllocator::Init(FIntPoint PageAtlasSizeInPages)
{
	PhysicalPageFreeList.SetNum(PageAtlasSizeInPages.X * PageAtlasSizeInPages.Y);
	for (int32 CoordY = 0; CoordY < PageAtlasSizeInPages.Y; ++CoordY)
	{
		for (int32 CoordX = 0; CoordX < PageAtlasSizeInPages.X; ++CoordX)
		{
			const int32 PageFreeListIndex = PageAtlasSizeInPages.X * PageAtlasSizeInPages.Y - 1 - (CoordX + PageAtlasSizeInPages.X * CoordY);
			PhysicalPageFreeList[PageFreeListIndex].X = CoordX;
			PhysicalPageFreeList[PageFreeListIndex].Y = CoordY;
		}
	}
}

FIntPoint FLumenSurfaceCacheAllocator::AllocatePhysicalAtlasPage()
{
	FIntPoint NewPageCoord = FIntPoint(-1, -1);

	if (PhysicalPageFreeList.Num() > 0)
	{
		NewPageCoord = PhysicalPageFreeList.Last();
		PhysicalPageFreeList.Pop();
	}

	return NewPageCoord;
}

void FLumenSurfaceCacheAllocator::FreePhysicalAtlasPage(FIntPoint PageCoord)
{
	if (PageCoord.X >= 0 && PageCoord.Y >= 0)
	{
		PhysicalPageFreeList.Add(PageCoord);
	}
}

void FLumenSurfaceCacheAllocator::Allocate(const FLumenPageTableEntry& Page, FAllocation& Allocation)
{
	if (Page.IsSubAllocation())
	{
		FPageBin* MatchingBin = nullptr;

		for (FPageBin& Bin : PageBins)
		{
			if (Bin.ElementSize == Page.SubAllocationSize)
			{
				MatchingBin = &Bin;
				break;
			}
		}

		if (!MatchingBin)
		{
			PageBins.Add(FPageBin(Page.SubAllocationSize));
			MatchingBin = &PageBins.Last();
		}

		FPageBinAllocation* MatchingBinAllocation = nullptr;

		for (FPageBinAllocation& BinAllocation : MatchingBin->BinAllocations)
		{
			if (BinAllocation.FreeList.Num() > 0)
			{
				MatchingBinAllocation = &BinAllocation;
				break;
			}
		}

		if (!MatchingBinAllocation)
		{
			const FIntPoint PageCoord = AllocatePhysicalAtlasPage();

			if (PageCoord.X >= 0 && PageCoord.Y >= 0)
			{
				MatchingBin->BinAllocations.AddDefaulted(1);

				FPageBinAllocation& NewBinAllocation = MatchingBin->BinAllocations.Last();
				NewBinAllocation.PageCoord = PageCoord;

				NewBinAllocation.FreeList.SetNum(MatchingBin->PageSizeInElements.X * MatchingBin->PageSizeInElements.Y);
				for (int32 ElementsY = 0; ElementsY < MatchingBin->PageSizeInElements.Y; ++ElementsY)
				{
					for (int32 ElementsX = 0; ElementsX < MatchingBin->PageSizeInElements.X; ++ElementsX)
					{
						NewBinAllocation.FreeList[ElementsX + ElementsY * MatchingBin->PageSizeInElements.X] = FIntPoint(ElementsX, ElementsY);
					}
				}

				MatchingBinAllocation = &NewBinAllocation;
			}
		}

		if (MatchingBinAllocation)
		{
			const FIntPoint ElementCoord = MatchingBinAllocation->FreeList.Last();
			MatchingBinAllocation->FreeList.Pop();

			const FIntPoint ElementOffset = MatchingBinAllocation->PageCoord * Lumen::PhysicalPageSize + ElementCoord * MatchingBin->ElementSize;

			Allocation.PhysicalPageCoord = MatchingBinAllocation->PageCoord;
			Allocation.PhysicalAtlasRect.Min = ElementOffset;
			Allocation.PhysicalAtlasRect.Max = ElementOffset + MatchingBin->ElementSize;
		}
	}
	else
	{
		Allocation.PhysicalPageCoord = AllocatePhysicalAtlasPage();
		Allocation.PhysicalAtlasRect.Min = (Allocation.PhysicalPageCoord + 0) * Lumen::PhysicalPageSize;
		Allocation.PhysicalAtlasRect.Max = (Allocation.PhysicalPageCoord + 1) * Lumen::PhysicalPageSize;
	}
}

void FLumenSurfaceCacheAllocator::Free(const FLumenPageTableEntry& Page)
{
	if (Page.IsSubAllocation())
	{
		FPageBin* MatchingBin = nullptr;
		for (FPageBin& Bin : PageBins)
		{
			if (Bin.ElementSize == Page.SubAllocationSize)
			{
				MatchingBin = &Bin;
				break;
			}
		}

		check(MatchingBin);
		bool bRemoved = false;

		for (int32 AllocationIndex = 0; AllocationIndex < MatchingBin->BinAllocations.Num(); AllocationIndex++)
		{
			FPageBinAllocation& BinAllocation = MatchingBin->BinAllocations[AllocationIndex];

			const FIntPoint ElementCoord = (Page.PhysicalAtlasRect.Min - BinAllocation.PageCoord * Lumen::PhysicalPageSize) / MatchingBin->ElementSize;

			if (ElementCoord.X >= 0
				&& ElementCoord.Y >= 0
				&& ElementCoord.X < MatchingBin->PageSizeInElements.X
				&& ElementCoord.Y < MatchingBin->PageSizeInElements.Y)
			{
				BinAllocation.FreeList.Add(ElementCoord);

				if (BinAllocation.FreeList.Num() == MatchingBin->GetNumElements())
				{
					FreePhysicalAtlasPage(BinAllocation.PageCoord);
					MatchingBin->BinAllocations.RemoveAt(AllocationIndex);
				}

				bRemoved = true;
				break;
			}
		}

		check(bRemoved);
	}
	else
	{
		FreePhysicalAtlasPage(Page.PhysicalPageCoord);
	}
}

/**
 * Checks if there's enough free memory in the surface cache to allocate entire mip map level of a card (or a single page)
 */
bool FLumenSurfaceCacheAllocator::IsSpaceAvailable(const FLumenCard& Card, int32 ResLevel, bool bSinglePage) const
{
	FLumenMipMapDesc MipMapDesc;

	Card.GetMipMapDesc(ResLevel, MipMapDesc);

	const int32 ReqSizeInPages = bSinglePage ? 1 : (MipMapDesc.SizeInPages.X * MipMapDesc.SizeInPages.Y);

	if (PhysicalPageFreeList.Num() >= ReqSizeInPages)
	{
		return true;
	}

	// No free pages, but maybe there's some space in one of the existing bins
	if (MipMapDesc.bSubAllocation)
	{
		const FPageBin* MatchingBin = nullptr;

		for (const FPageBin& Bin : PageBins)
		{
			if (Bin.ElementSize == MipMapDesc.Resolution)
			{
				for (const FPageBinAllocation& BinAllocation : Bin.BinAllocations)
				{
					if (BinAllocation.FreeList.Num() > 0)
					{
						return true;
					}
				}

				break;
			}
		}
	}

	return false;
}

void FLumenSurfaceCacheAllocator::GetStats(FStats& Stats) const
{
	Stats.NumFreePages = PhysicalPageFreeList.Num();

	for (const FPageBin& Bin : PageBins)
	{
		uint32 NumFreeElements = 0;

		for (const FPageBinAllocation& BinAllocation : Bin.BinAllocations)
		{
			NumFreeElements += BinAllocation.FreeList.Num();
		}

		const uint32 NumElementsPerPage = Bin.PageSizeInElements.X * Bin.PageSizeInElements.Y;
		const uint32 NumElements = Bin.BinAllocations.Num() * NumElementsPerPage - NumFreeElements;

		Stats.BinNumPages += Bin.BinAllocations.Num();
		Stats.BinNumWastedPages += Bin.BinAllocations.Num() - FMath::DivideAndRoundUp(NumElements, NumElementsPerPage);
		Stats.BinPageFreeTexels += NumFreeElements * Bin.ElementSize.X * Bin.ElementSize.Y;
	}
}

void FLumenSceneData::UploadPageTable(FRDGBuilder& GraphBuilder)
{
	SCOPED_DRAW_EVENT(GraphBuilder.RHICmdList, LumenUploadPageTable);

	if (GLumenSceneUploadEveryFrame != 0)
	{
		PageTableIndicesToUpdateInBuffer.SetNum(PageTable.Num());

		for (int32 PageIndex = 0; PageIndex < PageTable.Num(); ++PageIndex)
		{
			PageTableIndicesToUpdateInBuffer[PageIndex] = PageIndex;
		}
	}

	const uint32 NumElements = FMath::Max(1024, PageTable.Num());
	const int32 NumElementsToUpload = PageTableIndicesToUpdateInBuffer.Num();

	// PageTableBuffer
	{
		const int32 NumBytesPerElement = sizeof(uint32);
		bool bResourceResized = ResizeResourceIfNeeded(GraphBuilder.RHICmdList, PageTableBuffer, FMath::RoundUpToPowerOfTwo(NumElements) * NumBytesPerElement, TEXT("Lumen.PageTable"));

		if (NumElementsToUpload > 0)
		{
			ByteBufferUploadBuffer.Init(NumElementsToUpload, NumBytesPerElement, false, TEXT("Lumen.ByteBufferUploadBuffer"));

			for (int32 PageIndex : PageTableIndicesToUpdateInBuffer)
			{
				if (PageIndex < PageTable.Num())
				{
					uint32 PackedData = 0;

					if (PageTable.IsAllocated(PageIndex))
					{
						const FLumenPageTableEntry& Page = PageTable[PageIndex];

						PackedData |= ((Page.SampleAtlasBiasX & 0xFFF) << 0);
						PackedData |= ((Page.SampleAtlasBiasY & 0xFFF) << 12);
						PackedData |= ((Page.SampleCardResLevelX & 0xF) << 24);
						PackedData |= ((Page.SampleCardResLevelY & 0xF) << 28);
					}

					ByteBufferUploadBuffer.Add(PageIndex, &PackedData);
				}
			}

			GraphBuilder.RHICmdList.Transition(FRHITransitionInfo(PageTableBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
			ByteBufferUploadBuffer.ResourceUploadTo(GraphBuilder.RHICmdList, PageTableBuffer, false);
			GraphBuilder.RHICmdList.Transition(FRHITransitionInfo(PageTableBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
		}
		else if (bResourceResized)
		{
			GraphBuilder.RHICmdList.Transition(FRHITransitionInfo(PageTableBuffer.UAV, ERHIAccess::UAVCompute | ERHIAccess::UAVGraphics, ERHIAccess::SRVMask));
		}
	}

	// CardPageBuffer
	{
		const FVector2D InvPhysicalAtlasSize = FVector2D(1.0f) / GetPhysicalAtlasSize();

		const int32 NumBytesPerElement = FLumenCardPageGPUData::DataStrideInFloat4s * sizeof(FVector4);
		bool bResourceResized = ResizeResourceIfNeeded(GraphBuilder.RHICmdList, CardPageBuffer, FMath::RoundUpToPowerOfTwo(NumElements) * NumBytesPerElement, TEXT("Lumen.PageBuffer"));

		if (NumElementsToUpload > 0)
		{
			FLumenPageTableEntry NullPageTableEntry;

			UploadBuffer.Init(NumElementsToUpload, FLumenCardPageGPUData::DataStrideInBytes, true, TEXT("Lumen.UploadBuffer"));

			for (int32 PageIndex : PageTableIndicesToUpdateInBuffer)
			{
				if (PageIndex < PageTable.Num())
				{
					FVector4* Data = (FVector4*)UploadBuffer.Add_GetRef(PageIndex);

					if (PageTable.IsAllocated(PageIndex) && PageTable[PageIndex].IsMapped())
					{
						FLumenCardPageGPUData::FillData(PageTable[PageIndex], InvPhysicalAtlasSize, Data);
					}
					else
					{
						FLumenCardPageGPUData::FillData(NullPageTableEntry, InvPhysicalAtlasSize, Data);
					}
				}
			}

			GraphBuilder.RHICmdList.Transition(FRHITransitionInfo(CardPageBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
			UploadBuffer.ResourceUploadTo(GraphBuilder.RHICmdList, CardPageBuffer, false);
			GraphBuilder.RHICmdList.Transition(FRHITransitionInfo(CardPageBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
		}
		else if (bResourceResized)
		{
			GraphBuilder.RHICmdList.Transition(FRHITransitionInfo(CardPageBuffer.UAV, ERHIAccess::UAVCompute | ERHIAccess::UAVGraphics, ERHIAccess::SRVMask));
		}
	}

	// Reset arrays, but keep allocated memory for 1024 elements
	PageTableIndicesToUpdateInBuffer.Empty(1024);
}

FLumenSceneData::FLumenSceneData(EShaderPlatform ShaderPlatform, EWorldType::Type WorldType) :
	bFinalLightingAtlasContentsValid(false)
{
	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MeshCardRepresentation"));

	bTrackAllPrimitives = (DoesPlatformSupportLumenGI(ShaderPlatform)) && CVar->GetValueOnGameThread() != 0 && WorldType != EWorldType::EditorPreview;
}

FLumenSceneData::~FLumenSceneData()
{
	LLM_SCOPE_BYTAG(Lumen);

	for (int32 CardIndex = 0; CardIndex < Cards.Num(); ++CardIndex)
	{
		if (Cards.IsAllocated(CardIndex))
		{
			RemoveCardFromAtlas(CardIndex);
		}
	}

	Cards.Reset();
	MeshCards.Reset();
}

bool TrackPrimitiveForLumenScene(const FPrimitiveSceneProxy* Proxy)
{
	bool bTrack = Proxy->AffectsDynamicIndirectLighting()
		&& Proxy->SupportsMeshCardRepresentation()
		// For now Lumen depends on the distance field representation. 
		// This also makes sure that non opaque things won't get included in Lumen Scene
		&& Proxy->SupportsDistanceFieldRepresentation()
		&& (Proxy->IsDrawnInGame() || Proxy->CastsHiddenShadow());

	return bTrack;
}

bool TrackPrimitiveInstanceForLumenScene(const FMatrix& LocalToWorld, const FBox& LocalBoundingBox)
{
	const FVector LocalToWorldScale = LocalToWorld.GetScaleVector();
	const FVector ScaledBoundSize = LocalBoundingBox.GetSize() * LocalToWorldScale;
	const FVector FaceSurfaceArea(ScaledBoundSize.Y * ScaledBoundSize.Z, ScaledBoundSize.X * ScaledBoundSize.Z, ScaledBoundSize.Y * ScaledBoundSize.X);
	const float LargestFaceArea = FaceSurfaceArea.GetMax();

	extern float GLumenMeshCardsMinSize;
	const float MinFaceSurfaceArea = GLumenMeshCardsMinSize * GLumenMeshCardsMinSize;

	return LargestFaceArea > MinFaceSurfaceArea;
}

void FLumenSceneData::AddPrimitiveToUpdate(int32 PrimitiveIndex)
{
	if (bTrackAllPrimitives)
	{
		PrimitivesToUpdate.Add(PrimitiveIndex);
	}
}

void FLumenSceneData::AddPrimitive(FPrimitiveSceneInfo* InPrimitive)
{
	LLM_SCOPE_BYTAG(Lumen);

	const FPrimitiveSceneProxy* Proxy = InPrimitive->Proxy;

	if (bTrackAllPrimitives && TrackPrimitiveForLumenScene(Proxy))
	{
		ensure(!PendingAddOperations.Contains(InPrimitive));
		ensure(!PendingUpdateOperations.Contains(InPrimitive));
		PendingAddOperations.Add(InPrimitive);
	}
}

void FLumenSceneData::UpdatePrimitive(FPrimitiveSceneInfo* InPrimitive)
{
	LLM_SCOPE_BYTAG(Lumen);

	const FPrimitiveSceneProxy* Proxy = InPrimitive->Proxy;

	if (bTrackAllPrimitives
		&& TrackPrimitiveForLumenScene(Proxy)
		&& !PendingUpdateOperations.Contains(InPrimitive))
	{
		bool bPendingAdd = false;
		for (FPrimitiveSceneInfo* AddPrimitive : PendingAddOperations)
		{
			if (AddPrimitive == InPrimitive)
			{
				bPendingAdd = true;
				break;
			}
		}

		if (!bPendingAdd)
		{
			PendingUpdateOperations.Add(InPrimitive);
		}
	}
}

void FLumenSceneData::RemovePrimitive(FPrimitiveSceneInfo* InPrimitive, int32 PrimitiveIndex)
{
	LLM_SCOPE_BYTAG(Lumen);

	const FPrimitiveSceneProxy* Proxy = InPrimitive->Proxy;

	if (bTrackAllPrimitives
		&& TrackPrimitiveForLumenScene(Proxy))
	{
		PendingAddOperations.Remove(InPrimitive);
		PendingUpdateOperations.Remove(InPrimitive);
		PendingRemoveOperations.Add(FLumenPrimitiveRemoveInfo(InPrimitive, PrimitiveIndex));

		InPrimitive->LumenPrimitiveIndex = -1;
	}
}

bool Lumen::IsPrimitiveToDFObjectMappingRequired()
{
	return IsRayTracingEnabled();
}

double BoxSurfaceArea(FVector Extent)
{
	return 2.0 * (Extent.X * Extent.Y + Extent.Y * Extent.Z + Extent.Z * Extent.X);
}

void UpdateLumenScenePrimitives(FScene* Scene)
{
	LLM_SCOPE_BYTAG(Lumen);
	TRACE_CPUPROFILER_EVENT_SCOPE(UpdateLumenScenePrimitives);
	QUICK_SCOPE_CYCLE_COUNTER(UpdateLumenScenePrimitives);

	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	// Remove LumenPrimitives
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RemoveLumenPrimitives);
		QUICK_SCOPE_CYCLE_COUNTER(RemoveLumenPrimitives);

		struct FGreaterLumenPrimitiveIndex
		{
			FORCEINLINE bool operator()(const FLumenPrimitiveRemoveInfo& A, const FLumenPrimitiveRemoveInfo& B) const
			{
				return A.LumenPrimitiveIndex > B.LumenPrimitiveIndex;
			}
		};

		// Sort from largest to smallest so we can safely RemoveAtSwap without invalidating indices in this array
		LumenSceneData.PendingRemoveOperations.Sort(FGreaterLumenPrimitiveIndex());

		for (const FLumenPrimitiveRemoveInfo& RemoveInfo : LumenSceneData.PendingRemoveOperations)
		{
			if (RemoveInfo.LumenPrimitiveIndex >= 0)
			{
				// Deallocate resources
				{
					FLumenPrimitive& LumenPrimitive = LumenSceneData.LumenPrimitives[RemoveInfo.LumenPrimitiveIndex];

					if (LumenPrimitive.LumenNumDFInstances > 0)
					{
						LumenSceneData.LumenDFInstanceToDFObjectIndex.RemoveSpan(LumenPrimitive.LumenDFInstanceOffset, LumenPrimitive.LumenNumDFInstances);
						LumenSceneData.AddPrimitiveToUpdate(RemoveInfo.PrimitiveIndex);
					}

					for (FLumenPrimitiveInstance& LumenPrimitiveInstance : LumenPrimitive.Instances)
					{
						LumenSceneData.RemoveMeshCards(LumenPrimitive, LumenPrimitiveInstance);
					}

					ensure(LumenPrimitive.NumMeshCards == 0);
					LumenPrimitive.Instances.Empty(1);
				}

				// Swap, remove and fixup indices to the swapped element
				{
					LumenSceneData.LumenPrimitives.RemoveAtSwap(RemoveInfo.LumenPrimitiveIndex, 1, false);

					if (RemoveInfo.LumenPrimitiveIndex < LumenSceneData.LumenPrimitives.Num())
					{
						const FLumenPrimitive& SwappedLumenPrimitive = LumenSceneData.LumenPrimitives[RemoveInfo.LumenPrimitiveIndex];
						SwappedLumenPrimitive.Primitive->LumenPrimitiveIndex = RemoveInfo.LumenPrimitiveIndex;
					}
				}
			}
		}
	}

	// AddLumenPrimitives
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(AddLumenPrimitives);
		QUICK_SCOPE_CYCLE_COUNTER(AddLumenPrimitives);

		for (FPrimitiveSceneInfo* PrimitiveSceneInfo : LumenSceneData.PendingAddOperations)
		{
			const TArray<FPrimitiveInstance>* PrimitiveInstances = PrimitiveSceneInfo->Proxy->GetPrimitiveInstances();

			// #lumen_todo: Remove after non-Nanite per instance ISM capture is fixed (now every instance draws entire ISM)
			int32 NumInstances = PrimitiveInstances ? PrimitiveInstances->Num() : 1;
			if (!PrimitiveSceneInfo->Proxy->IsNaniteMesh())
			{
				NumInstances = 1;
			}

			bool bAnyInstanceValid = false;
			{
				const FMatrix& LocalToWorld = PrimitiveSceneInfo->Proxy->GetLocalToWorld();

				for (int32 InstanceIndex = 0; InstanceIndex < NumInstances; ++InstanceIndex)
				{
					FBox LocalBoundingBox;
					if (PrimitiveInstances && InstanceIndex < PrimitiveInstances->Num())
					{
						LocalBoundingBox = (*PrimitiveInstances)[InstanceIndex].RenderBounds.GetBox();
					}
					else
					{
						LocalBoundingBox = PrimitiveSceneInfo->Proxy->GetLocalBounds().GetBox();
					}

					if (TrackPrimitiveInstanceForLumenScene(LocalToWorld, LocalBoundingBox))
					{
						bAnyInstanceValid = true;
						break;
					}
				}
			}

			if (bAnyInstanceValid)
			{
				const int32 LumenPrimitiveIndex = LumenSceneData.LumenPrimitives.AddDefaulted(1);
				FLumenPrimitive& LumenPrimitive = LumenSceneData.LumenPrimitives[LumenPrimitiveIndex];

				LumenPrimitive.Primitive = PrimitiveSceneInfo;
				LumenPrimitive.Primitive->LumenPrimitiveIndex = LumenPrimitiveIndex;
				LumenPrimitive.WorldSpaceBoundingBox = PrimitiveSceneInfo->Proxy->GetBounds().GetBox();
				LumenPrimitive.MaxCardExtent = LumenPrimitive.WorldSpaceBoundingBox.GetExtent().GetMax();

				const FMatrix& LocalToWorld = PrimitiveSceneInfo->Proxy->GetLocalToWorld();

				if (PrimitiveInstances && NumInstances > 1)
				{
					// Check if we can merge all instances into one MeshCards
					extern int32 GLumenMeshCardsMergeInstances;
					extern float GLumenMeshCardsMergedMaxWorldSize;
					if (GLumenMeshCardsMergeInstances
						&& NumInstances > 1
						&& LumenPrimitive.WorldSpaceBoundingBox.GetSize().GetMax() < GLumenMeshCardsMergedMaxWorldSize)
					{
						FBox LocalBounds;
						LocalBounds.Init();
						double TotalInstanceSurfaceArea = 0;

						for (int32 InstanceIndex = 0; InstanceIndex < NumInstances; ++InstanceIndex)
						{
							const FPrimitiveInstance& Instance = (*PrimitiveInstances)[InstanceIndex];
							const FBox InstanceLocalBounds = Instance.RenderBounds.GetBox().TransformBy(Instance.InstanceToLocal);
							LocalBounds += InstanceLocalBounds;
							const double InstanceSurfaceArea = BoxSurfaceArea(InstanceLocalBounds.GetExtent());
							TotalInstanceSurfaceArea += InstanceSurfaceArea;
						}

						const double BoundsSurfaceArea = BoxSurfaceArea(LocalBounds.GetExtent());
						const float SurfaceAreaRatio = BoundsSurfaceArea / TotalInstanceSurfaceArea;

						extern float GLumenMeshCardsMergeInstancesMaxSurfaceAreaRatio;
						extern float GLumenMeshCardsMergedResolutionScale;

						if (SurfaceAreaRatio < GLumenMeshCardsMergeInstancesMaxSurfaceAreaRatio)
						{
							LumenPrimitive.bMergedInstances = true;
							LumenPrimitive.CardResolutionScale = FMath::Sqrt(1.0f / SurfaceAreaRatio) * GLumenMeshCardsMergedResolutionScale;

							LumenPrimitive.Instances.SetNum(1);
							LumenPrimitive.Instances[0].WorldSpaceBoundingBox = LocalBounds.TransformBy(LocalToWorld);
							LumenPrimitive.Instances[0].MeshCardsIndex = -1;
							LumenPrimitive.Instances[0].bValidMeshCards = true;

							LumenPrimitive.MaxCardExtent = LumenPrimitive.Instances[0].WorldSpaceBoundingBox.GetExtent().GetMax();
						}

#define LOG_LUMEN_PRIMITIVE_ADDS 0
#if LOG_LUMEN_PRIMITIVE_ADDS
						{
							UE_LOG(LogRenderer, Log, TEXT("AddLumenPrimitive %s: Instances: %u, Merged: %u, SurfaceAreaRatio: %.1f"),
								*LumenPrimitive.Primitive->Proxy->GetOwnerName().ToString(),
								NumInstances,
								LumenPrimitive.bMergedInstances ? 1 : 0,
								SurfaceAreaRatio);
						}
#endif
					}

					if (!LumenPrimitive.bMergedInstances)
					{
						LumenPrimitive.Instances.SetNum(NumInstances);

						LumenPrimitive.MaxCardExtent = 0.0f;

						for (int32 InstanceIndex = 0; InstanceIndex < NumInstances; ++InstanceIndex)
						{
							FLumenPrimitiveInstance& LumenInstance = LumenPrimitive.Instances[InstanceIndex];
							const FPrimitiveInstance& PrimitiveInstance = (*PrimitiveInstances)[InstanceIndex];

							const FBox& RenderBoundingBox = PrimitiveInstance.RenderBounds.GetBox();

							LumenInstance.WorldSpaceBoundingBox = RenderBoundingBox.TransformBy(PrimitiveInstance.InstanceToLocal * LocalToWorld);
							LumenInstance.MeshCardsIndex = -1;
							LumenInstance.bValidMeshCards = true;

							LumenPrimitive.MaxCardExtent = FMath::Max(LumenPrimitive.MaxCardExtent, LumenInstance.WorldSpaceBoundingBox.GetExtent().GetMax());
						}
					}
				}
				else
				{
					LumenPrimitive.Instances.SetNum(1);
					LumenPrimitive.Instances[0].WorldSpaceBoundingBox = LumenPrimitive.WorldSpaceBoundingBox;
					LumenPrimitive.Instances[0].MeshCardsIndex = -1;
					LumenPrimitive.Instances[0].bValidMeshCards = true;
				}

				if (Lumen::IsPrimitiveToDFObjectMappingRequired())
				{
					TArray<FMatrix> ObjectLocalToWorldTransforms;
					PrimitiveSceneInfo->Proxy->GetDistancefieldInstanceData(ObjectLocalToWorldTransforms);

					LumenPrimitive.LumenNumDFInstances = FMath::Max(ObjectLocalToWorldTransforms.Num(), 1);
					LumenPrimitive.LumenDFInstanceOffset = LumenSceneData.LumenDFInstanceToDFObjectIndex.AddSpan(LumenPrimitive.LumenNumDFInstances);

					for (int32 InstanceIndex = 0; InstanceIndex < LumenPrimitive.LumenNumDFInstances; ++InstanceIndex)
					{
						int32 DistanceFieldObjectIndex = -1;
						if (InstanceIndex < PrimitiveSceneInfo->DistanceFieldInstanceIndices.Num())
						{
							DistanceFieldObjectIndex = PrimitiveSceneInfo->DistanceFieldInstanceIndices[InstanceIndex];
						}

						const int32 LumenDFInstanceIndex = LumenPrimitive.LumenDFInstanceOffset + InstanceIndex;
						LumenSceneData.LumenDFInstanceToDFObjectIndex[LumenDFInstanceIndex] = DistanceFieldObjectIndex;
						LumenSceneData.LumenDFInstancesToUpdate.Add(LumenDFInstanceIndex);
					}

					LumenSceneData.AddPrimitiveToUpdate(PrimitiveSceneInfo->GetIndex());
				}
			}
		}
	}

	// UpdateLumenPrimitives
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UpdateLumenPrimitives);
		QUICK_SCOPE_CYCLE_COUNTER(UpdateLumenPrimitives);

		for (TSet<FPrimitiveSceneInfo*>::TIterator It(LumenSceneData.PendingUpdateOperations); It; ++It)
		{
			FPrimitiveSceneInfo* PrimitiveSceneInfo = *It;

			if (PrimitiveSceneInfo->LumenPrimitiveIndex >= 0)
			{
				FLumenPrimitive& LumenPrimitive = LumenSceneData.LumenPrimitives[PrimitiveSceneInfo->LumenPrimitiveIndex];
				LumenPrimitive.WorldSpaceBoundingBox = PrimitiveSceneInfo->Proxy->GetBounds().GetBox();
				LumenPrimitive.MaxCardExtent = LumenPrimitive.WorldSpaceBoundingBox.GetExtent().GetMax();

				const TArray<FPrimitiveInstance>* PrimitiveInstances = LumenPrimitive.Primitive->Proxy->GetPrimitiveInstances();

				if (LumenPrimitive.bMergedInstances)
				{
					FMatrix LocalToWorld = PrimitiveSceneInfo->Proxy->GetLocalToWorld();

					FMeshCardsBuildData MeshCardsBuildData;
					BuildMeshCardsDataForMergedInstances(PrimitiveSceneInfo, MeshCardsBuildData);
					LumenSceneData.UpdateMeshCards(LocalToWorld, LumenPrimitive.Instances[0].MeshCardsIndex, MeshCardsBuildData);
				}
				else
				{
					LumenPrimitive.MaxCardExtent = 0.0f;

					for (int32 LumenInstanceIndex = 0; LumenInstanceIndex < LumenPrimitive.Instances.Num(); ++LumenInstanceIndex)
					{
						FLumenPrimitiveInstance& LumenInstance = LumenPrimitive.Instances[LumenInstanceIndex];
						FBox WorldSpaceBoundingBox = LumenPrimitive.WorldSpaceBoundingBox;
						const FMatrix& LocalToWorld = PrimitiveSceneInfo->Proxy->GetLocalToWorld();

						if (PrimitiveInstances && LumenInstanceIndex < PrimitiveInstances->Num())
						{
							const FPrimitiveInstance& PrimitiveInstance = (*PrimitiveInstances)[LumenInstanceIndex];
							WorldSpaceBoundingBox = PrimitiveInstance.RenderBounds.GetBox().TransformBy(PrimitiveInstance.InstanceToLocal * LocalToWorld);
						}

						const FCardRepresentationData* CardRepresentationData = PrimitiveSceneInfo->Proxy->GetMeshCardRepresentation();

						LumenInstance.WorldSpaceBoundingBox = WorldSpaceBoundingBox;
						LumenSceneData.UpdateMeshCards(LocalToWorld, LumenInstance.MeshCardsIndex, CardRepresentationData->MeshCardsBuildData);

						LumenPrimitive.MaxCardExtent = FMath::Max(LumenPrimitive.MaxCardExtent, LumenInstance.WorldSpaceBoundingBox.GetExtent().GetMax());
					}
				}
			}
		}
	}

	// Reset arrays, but keep allocated memory for 1024 elements
	LumenSceneData.PendingAddOperations.Reset();
	LumenSceneData.PendingRemoveOperations.Reset(1024);
	LumenSceneData.PendingUpdateOperations.Reset();
	LumenSceneData.PendingUpdateOperations.Reserve(1024);
}

void FLumenSceneData::RemoveAllMeshCards()
{
	LLM_SCOPE_BYTAG(Lumen);
	TRACE_CPUPROFILER_EVENT_SCOPE(RemoveAllCards);
	QUICK_SCOPE_CYCLE_COUNTER(RemoveAllCards);

	for (int32 LumenPrimitiveIndex = 0; LumenPrimitiveIndex < LumenPrimitives.Num(); ++LumenPrimitiveIndex)
	{
		FLumenPrimitive& LumenPrimitive = LumenPrimitives[LumenPrimitiveIndex];
		for (FLumenPrimitiveInstance& Instance : LumenPrimitive.Instances)
		{
			RemoveMeshCards(LumenPrimitive, Instance);
		}
	}
}

bool FLumenSceneData::UpdateAtlasSize()
{
	extern int32 GLumenSurfaceCacheCompress;

	if (PhysicalAtlasSize != GetDesiredPhysicalAtlasSize()
		|| bCompressPhysicalAtlas != (GLumenSurfaceCacheCompress != 0))
	{
		RemoveAllMeshCards();

		PhysicalAtlasSize = GetDesiredPhysicalAtlasSize();
		SurfaceCacheAllocator.Init(GetDesiredPhysicalAtlasSizeInPages());
		UnlockedAllocationHeap.Clear();

		bCompressPhysicalAtlas = (GLumenSurfaceCacheCompress != 0);
		return true;
	}

	return false;
}

void FLumenCard::UpdateMinMaxAllocatedLevel()
{
	MinAllocatedResLevel = UINT8_MAX;
	MaxAllocatedResLevel = 0;

	for (int32 ResLevelIndex = Lumen::MinResLevel; ResLevelIndex <= Lumen::MaxResLevel; ++ResLevelIndex)
	{
		if (GetMipMap(ResLevelIndex).IsAllocated())
		{
			MinAllocatedResLevel = FMath::Min<int32>(MinAllocatedResLevel, ResLevelIndex);
			MaxAllocatedResLevel = FMath::Max<int32>(MaxAllocatedResLevel, ResLevelIndex);
		}
	}
}

FIntPoint FLumenCard::ResLevelToResLevelXYBias() const
{
	FIntPoint Bias(0, 0);

	// ResLevel bias to account for card's aspect
	if (LocalExtent.X >= LocalExtent.Y)
	{
		Bias.Y = FMath::FloorLog2(FMath::RoundToInt(LocalExtent.X / LocalExtent.Y));
	}
	else
	{
		Bias.X = FMath::FloorLog2(FMath::RoundToInt(LocalExtent.Y / LocalExtent.X));
	}

	return Bias;
}

void FLumenCard::GetMipMapDesc(int32 ResLevel, FLumenMipMapDesc& Desc) const
{
	const FIntPoint ResLevelBias = ResLevelToResLevelXYBias();
	Desc.ResLevelX = FMath::Max<int32>(ResLevel - ResLevelBias.X, Lumen::MinResLevel);
	Desc.ResLevelY = FMath::Max<int32>(ResLevel - ResLevelBias.Y, Lumen::MinResLevel);

	// Allocations which exceed a physical page are aligned to multiples of a virtual page to maximize atlas usage
	if (Desc.ResLevelX > Lumen::SubAllocationResLevel || Desc.ResLevelY > Lumen::SubAllocationResLevel)
	{
		// Clamp res level to page size
		Desc.ResLevelX = FMath::Max<int32>(Desc.ResLevelX, Lumen::SubAllocationResLevel);
		Desc.ResLevelY = FMath::Max<int32>(Desc.ResLevelY, Lumen::SubAllocationResLevel);

		Desc.bSubAllocation = false;
		Desc.SizeInPages.X = 1u << (Desc.ResLevelX - Lumen::SubAllocationResLevel);
		Desc.SizeInPages.Y = 1u << (Desc.ResLevelY - Lumen::SubAllocationResLevel);
		Desc.Resolution.X = Desc.SizeInPages.X * Lumen::VirtualPageSize;
		Desc.Resolution.Y = Desc.SizeInPages.Y * Lumen::VirtualPageSize;
	}
	else
	{
		Desc.bSubAllocation = true;
		Desc.SizeInPages.X = 1;
		Desc.SizeInPages.Y = 1;
		Desc.Resolution.X = 1 << Desc.ResLevelX;
		Desc.Resolution.Y = 1 << Desc.ResLevelY;
	}
}

void FLumenCard::GetSurfaceStats(const TSparseSpanArray<FLumenPageTableEntry>& PageTable, FSurfaceStats& Stats) const
{
	if (IsAllocated())
	{
		for (int32 ResLevelIndex = MinAllocatedResLevel; ResLevelIndex <= MaxAllocatedResLevel; ++ResLevelIndex)
		{
			const FLumenSurfaceMipMap& MipMap = GetMipMap(ResLevelIndex);

			if (MipMap.IsAllocated())
			{
				uint32 NumVirtualTexels = 0;
				uint32 NumPhysicalTexels = 0;

				for (int32 LocalPageIndex = 0; LocalPageIndex < MipMap.SizeInPagesX * MipMap.SizeInPagesY; ++LocalPageIndex)
				{
					const int32 PageTableIndex = MipMap.GetPageTableIndex(LocalPageIndex);
					const FLumenPageTableEntry& PageTableEntry = PageTable[PageTableIndex];

					NumVirtualTexels += PageTableEntry.GetNumVirtualTexels();
					NumPhysicalTexels += PageTableEntry.GetNumPhysicalTexels();
				}

				Stats.NumVirtualTexels += NumVirtualTexels;
				Stats.NumPhysicalTexels += NumPhysicalTexels;
				
				if (MipMap.bLocked)
				{
					Stats.NumLockedVirtualTexels += NumVirtualTexels;
					Stats.NumLockedPhysicalTexels += NumPhysicalTexels;
				}
			}
		}

		if (DesiredLockedResLevel > MinAllocatedResLevel)
		{
			Stats.DroppedResLevels += DesiredLockedResLevel - MinAllocatedResLevel;
		}
	}
}

FLumenPageTableEntry& FLumenSceneData::MapSurfaceCachePage(FLumenSurfaceMipMap& MipMap, int32 PageTableIndex)
{
	FLumenPageTableEntry& PageTableEntry = PageTable[PageTableIndex];

	if (!PageTableEntry.IsMapped())
	{
		FLumenSurfaceCacheAllocator::FAllocation Allocation;
		SurfaceCacheAllocator.Allocate(PageTableEntry, Allocation);

		PageTableEntry.PhysicalPageCoord = Allocation.PhysicalPageCoord;
		PageTableEntry.PhysicalAtlasRect = Allocation.PhysicalAtlasRect;

		if (PageTableEntry.IsMapped())
		{
			PageTableEntry.SampleAtlasBiasX = PageTableEntry.PhysicalAtlasRect.Min.X / Lumen::MinCardResolution;
			PageTableEntry.SampleAtlasBiasY = PageTableEntry.PhysicalAtlasRect.Min.Y / Lumen::MinCardResolution;
			PageTableEntry.SampleCardResLevelX = MipMap.ResLevelX;
			PageTableEntry.SampleCardResLevelY = MipMap.ResLevelY;

			if (!MipMap.bLocked)
			{
				UnlockedAllocationHeap.Add(SurfaceCacheFeedback.GetFrameIndex(), PageTableIndex);
			}
		}

		PageTableIndicesToUpdateInBuffer.Add(PageTableIndex);
	}

	return PageTableEntry;
}

void FLumenSceneData::UnmapSurfaceCachePage(bool bLocked, FLumenPageTableEntry& Page, int32 PageIndex)
{
	if (Page.IsMapped())
	{
		if (!bLocked)
		{
			UnlockedAllocationHeap.Remove(PageIndex);
		}

		SurfaceCacheAllocator.Free(Page);

		Page.PhysicalPageCoord.X = -1;
		Page.PhysicalPageCoord.Y = -1;
		Page.SampleAtlasBiasX = 0;
		Page.SampleAtlasBiasY = 0;
		Page.SampleCardResLevelX = 0;
		Page.SampleCardResLevelY = 0;
	}
}

void FLumenSceneData::ReallocVirtualSurface(FLumenCard& Card, int32 CardIndex, int32 ResLevel, bool bLockPages)
{
	FLumenSurfaceMipMap& MipMap = Card.GetMipMap(ResLevel);

	if (MipMap.PageTableSpanSize > 0 && MipMap.bLocked != bLockPages)
	{
		// Virtual memory is already allocated, but need to change the bLocked flag for any mapped pages
	
		if (MipMap.bLocked)
		{
			// Unlock all pages
			for (int32 LocalPageIndex = 0; LocalPageIndex < MipMap.SizeInPagesX * MipMap.SizeInPagesY; ++LocalPageIndex)
			{
				const int32 PageTableIndex = MipMap.GetPageTableIndex(LocalPageIndex);
				FLumenPageTableEntry& PageTableEntry = PageTable[PageTableIndex];
				if (PageTableEntry.IsMapped())
				{
					UnlockedAllocationHeap.Add(SurfaceCacheFeedback.GetFrameIndex(), PageTableIndex);
				}
			}

			MipMap.bLocked = false;
		}
		else
		{
			// Lock all pages
			for (int32 LocalPageIndex = 0; LocalPageIndex < MipMap.SizeInPagesX * MipMap.SizeInPagesY; ++LocalPageIndex)
			{
				const int32 PageTableIndex = MipMap.GetPageTableIndex(LocalPageIndex);
				FLumenPageTableEntry& PageTableEntry = PageTable[PageTableIndex];
				if (PageTableEntry.IsMapped())
				{
					UnlockedAllocationHeap.Remove(PageTableIndex);
				}
			}

			MipMap.bLocked = true;
		}
	}
	else if (MipMap.PageTableSpanSize == 0)
	{
		// Allocate virtual memory for the given mip map

		FLumenMipMapDesc MipMapDesc;
		Card.GetMipMapDesc(ResLevel, MipMapDesc);

		MipMap.bLocked = bLockPages;
		MipMap.SizeInPagesX = MipMapDesc.SizeInPages.X;
		MipMap.SizeInPagesY = MipMapDesc.SizeInPages.Y;
		MipMap.ResLevelX = MipMapDesc.ResLevelX;
		MipMap.ResLevelY = MipMapDesc.ResLevelY;
		MipMap.PageTableSpanSize += MipMapDesc.SizeInPages.X * MipMapDesc.SizeInPages.Y;
		MipMap.PageTableSpanOffset = PageTable.AddSpan(MipMap.PageTableSpanSize);

		for (int32 LocalPageIndex = 0; LocalPageIndex < MipMapDesc.SizeInPages.X * MipMapDesc.SizeInPages.Y; ++LocalPageIndex)
		{
			const int32 PageTableIndex = MipMap.GetPageTableIndex(LocalPageIndex);

			FLumenPageTableEntry& PageTableEntry = PageTable[PageTableIndex];
			PageTableEntry.CardIndex = CardIndex;
			PageTableEntry.SubAllocationSize = MipMapDesc.bSubAllocation ? MipMapDesc.Resolution : FIntPoint(-1, -1);
			PageTableEntry.SampleAtlasBiasX = 0;
			PageTableEntry.SampleAtlasBiasY = 0;
			PageTableEntry.SampleCardResLevelX = 0;
			PageTableEntry.SampleCardResLevelY = 0;

			const int32 LocalPageCoordX = LocalPageIndex % MipMapDesc.SizeInPages.X;
			const int32 LocalPageCoordY = LocalPageIndex / MipMapDesc.SizeInPages.X;

			FVector4 CardUVRect;
			CardUVRect.X = float(LocalPageCoordX + 0.0f) / MipMapDesc.SizeInPages.X;
			CardUVRect.Y = float(LocalPageCoordY + 0.0f) / MipMapDesc.SizeInPages.Y;
			CardUVRect.Z = float(LocalPageCoordX + 1.0f) / MipMapDesc.SizeInPages.X;
			CardUVRect.W = float(LocalPageCoordY + 1.0f) / MipMapDesc.SizeInPages.Y;
			PageTableEntry.CardUVRect = CardUVRect;

			PageTableIndicesToUpdateInBuffer.Add(PageTableIndex);
		}

		Card.UpdateMinMaxAllocatedLevel();
		CardIndicesToUpdateInBuffer.Add(CardIndex);
	}
}

void FLumenSceneData::FreeVirtualSurface(FLumenCard& Card, uint8 FromResLevel, uint8 ToResLevel)
{
	if (Card.IsAllocated())
	{
		for (uint8 ResLevel = FromResLevel; ResLevel <= ToResLevel; ++ResLevel)
		{
			FLumenSurfaceMipMap& MipMap = Card.GetMipMap(ResLevel);

			if (MipMap.IsAllocated())
			{
				// Unmap pages
				for (int32 LocalPageIndex = 0; LocalPageIndex < MipMap.SizeInPagesX * MipMap.SizeInPagesY; ++LocalPageIndex)
				{
					const int32 PageTableIndex = MipMap.GetPageTableIndex(LocalPageIndex);

					FLumenPageTableEntry& PageTableEntry = PageTable[PageTableIndex];
					UnmapSurfaceCachePage(MipMap.bLocked, PageTableEntry, PageTableIndex);
					PageTableEntry = FLumenPageTableEntry();
				}

				if (MipMap.PageTableSpanSize > 0)
				{
					PageTable.RemoveSpan(MipMap.PageTableSpanOffset, MipMap.PageTableSpanSize);

					for (int32 SpanOffset = 0; SpanOffset < MipMap.PageTableSpanSize; ++SpanOffset)
					{
						PageTableIndicesToUpdateInBuffer.Add(MipMap.PageTableSpanOffset + SpanOffset);
					}

					MipMap.PageTableSpanOffset = -1;
					MipMap.PageTableSpanSize = 0;
					MipMap.bLocked = false;
				}
			}
		}

		Card.UpdateMinMaxAllocatedLevel();
	}
}

/**
 * Remove any empty virtual mip allocations, and flatten page search by walking 
 * though the sparse mip maps and reusing lower res resident pages
 */
void FLumenSceneData::UpdateCardMipMapHierarchy(FLumenCard& Card)
{
	// Remove any mip map virtual allocations, which don't have any pages mapped
	for (int32 ResLevel = Card.MinAllocatedResLevel; ResLevel <= Card.MaxAllocatedResLevel; ++ResLevel)
	{
		FLumenSurfaceMipMap& MipMap = Card.GetMipMap(ResLevel);

		if (MipMap.IsAllocated())
		{
			bool IsAnyPageMapped = false;

			for (int32 LocalPageIndex = 0; LocalPageIndex < MipMap.SizeInPagesX * MipMap.SizeInPagesY; ++LocalPageIndex)
			{
				const int32 PageIndex = MipMap.GetPageTableIndex(LocalPageIndex);
				if (GetPageTableEntry(PageIndex).IsMapped())
				{
					IsAnyPageMapped = true;
					break;
				}
			}

			if (!IsAnyPageMapped)
			{
				FreeVirtualSurface(Card, ResLevel, ResLevel);
			}
		}
	}
	Card.UpdateMinMaxAllocatedLevel();


	int32 ParentResLevel = Card.MinAllocatedResLevel;

	for (int32 ResLevel = ParentResLevel + 1; ResLevel <= Card.MaxAllocatedResLevel; ++ResLevel)
	{
		FLumenSurfaceMipMap& MipMap = Card.GetMipMap(ResLevel);

		if (MipMap.PageTableSpanSize > 0)
		{
			for (int32 LocalPageIndex = 0; LocalPageIndex < MipMap.SizeInPagesX * MipMap.SizeInPagesY; ++LocalPageIndex)
			{
				const int32 PageIndex = MipMap.GetPageTableIndex(LocalPageIndex);
				FLumenPageTableEntry& PageTableEntry = GetPageTableEntry(PageIndex);

				if (!PageTableEntry.IsMapped())
				{
					FIntPoint LocalPageCoord;
					LocalPageCoord.X = LocalPageIndex % MipMap.SizeInPagesX;
					LocalPageCoord.Y = LocalPageIndex / MipMap.SizeInPagesX;

					FLumenSurfaceMipMap& ParentMipMap = Card.GetMipMap(ParentResLevel);
					const FIntPoint ParentLocalPageCoord = (LocalPageCoord * ParentMipMap.GetSizeInPages()) / MipMap.GetSizeInPages();
					const int32 ParentLocalPageIndex = ParentLocalPageCoord.X + ParentLocalPageCoord.Y * ParentMipMap.SizeInPagesX;

					const int32 ParentPageIndex = ParentMipMap.GetPageTableIndex(ParentLocalPageIndex);
					FLumenPageTableEntry& ParentPageTableEntry = GetPageTableEntry(ParentPageIndex);

					PageTableEntry.SampleAtlasBiasX = ParentPageTableEntry.SampleAtlasBiasX;
					PageTableEntry.SampleAtlasBiasY = ParentPageTableEntry.SampleAtlasBiasY;
					PageTableEntry.SampleCardResLevelX = ParentPageTableEntry.SampleCardResLevelX;
					PageTableEntry.SampleCardResLevelY = ParentPageTableEntry.SampleCardResLevelY;

					PageTableIndicesToUpdateInBuffer.Add(PageIndex);
				}
			}

			ParentResLevel = ResLevel;
		}
	}
}

/**
 * Evict all pages on demand, useful for debugging
 */
void FLumenSceneData::ForceEvictEntireCache()
{
	TSparseUniqueList<int32, SceneRenderingAllocator> DirtyCards;

	while (EvictOldestAllocation(/*bForceEvict*/ true, DirtyCards))
	{
	}

	for (int32 CardIndex : DirtyCards.Array)
	{
		FLumenCard& Card = Cards[CardIndex];
		UpdateCardMipMapHierarchy(Card);
		CardIndicesToUpdateInBuffer.Add(CardIndex);
	}
}

bool FLumenSceneData::EvictOldestAllocation(bool bForceEvict, TSparseUniqueList<int32, SceneRenderingAllocator>& DirtyCards)
{
	if (UnlockedAllocationHeap.Num() > 0)
	{
		const uint32 PageTableIndex = UnlockedAllocationHeap.Top();
		const uint32 LastFrameUsed = UnlockedAllocationHeap.GetKey(PageTableIndex);

		// Don't want to evict pages which may be picked up a jittering tile feedback
		const int32 MaxFrameDelta = bForceEvict ? 0 : Lumen::GetFeedbackBufferTileSize() * Lumen::GetFeedbackBufferTileSize();

		if (uint32(LastFrameUsed + MaxFrameDelta) <= SurfaceCacheFeedback.GetFrameIndex())
		{
			UnlockedAllocationHeap.Pop();

			FLumenPageTableEntry& Page = PageTable[PageTableIndex];
			if (Page.IsMapped())
			{
				UnmapSurfaceCachePage(false, Page, PageTableIndex);
				DirtyCards.Add(Page.CardIndex);
			}

			return true;
		}
	}

	return false;
}

void FLumenSceneData::DumpStats(const FDistanceFieldSceneData& DistanceFieldSceneData)
{
	const FIntPoint PageAtlasSizeInPages = GetDesiredPhysicalAtlasSizeInPages();
	const int32 NumPhysicalPages = PageAtlasSizeInPages.X * PageAtlasSizeInPages.Y;

	int32 NumCards = 0;
	int32 NumVisibleCards = 0;
	FLumenCard::FSurfaceStats SurfaceStats;

	for (const FLumenCard& Card : Cards)
	{
		++NumCards;

		if (Card.bVisible)
		{
			++NumVisibleCards;

			Card.GetSurfaceStats(PageTable, SurfaceStats);
		}
	}

	int32 NumLumenPrimitives = 0;
	int32 NumLumenPrimitiveInstances = 0;
	int32 NumInstancesMerged = 0;
	int32 NumMeshCards = 0;
	SIZE_T LumenPrimitiveInstanceAllocatedMemory = 0;

	for (const FLumenPrimitive& LumenPrimitive : LumenPrimitives)
	{
		++NumLumenPrimitives;
		LumenPrimitiveInstanceAllocatedMemory += LumenPrimitive.Instances.GetAllocatedSize();

		if (LumenPrimitive.bMergedInstances)
		{
			const TArray<FPrimitiveInstance>* PrimitiveInstances = LumenPrimitive.Primitive->Proxy->GetPrimitiveInstances();
			if (PrimitiveInstances)
			{
				NumInstancesMerged += PrimitiveInstances->Num();
			}
		}

		for (const FLumenPrimitiveInstance& LumenPrimitiveInstance : LumenPrimitive.Instances)
		{
			++NumLumenPrimitiveInstances;

			if (LumenPrimitiveInstance.MeshCardsIndex >= 0)
			{
				++NumMeshCards;
			}
		}
	}

	FLumenSurfaceCacheAllocator::FStats AllocatorStats;
	SurfaceCacheAllocator.GetStats(AllocatorStats);

	UE_LOG(LogRenderer, Log, TEXT("*** LumenScene Stats ***"));
	UE_LOG(LogRenderer, Log, TEXT("  Mesh SDF Objects: %d"), DistanceFieldSceneData.NumObjectsInBuffer);
	UE_LOG(LogRenderer, Log, TEXT("  Lumen Primitives: %d"), NumLumenPrimitives);
	UE_LOG(LogRenderer, Log, TEXT("  Lumen Primitive Instances: %d"), NumLumenPrimitiveInstances);
	UE_LOG(LogRenderer, Log, TEXT("  Merged instances: %d"), NumInstancesMerged);
	UE_LOG(LogRenderer, Log, TEXT("  Mesh cards: %d"), NumMeshCards);
	UE_LOG(LogRenderer, Log, TEXT("  Cards: %d"), NumCards);
	UE_LOG(LogRenderer, Log, TEXT("  Visible cards: %d"), NumVisibleCards);

	UE_LOG(LogRenderer, Log, TEXT("*** Surface cache ***"));
	UE_LOG(LogRenderer, Log, TEXT("  Allocated %d physical pages out of %d"), NumPhysicalPages - AllocatorStats.NumFreePages, NumPhysicalPages);
	UE_LOG(LogRenderer, Log, TEXT("  Bin pages: %d, wasted pages: %d, free texels: %.3fM"), AllocatorStats.BinNumPages, AllocatorStats.BinNumWastedPages, AllocatorStats.BinPageFreeTexels / (1024.0f * 1024.0f));
	UE_LOG(LogRenderer, Log, TEXT("  Virtual texels: %.3fM"), SurfaceStats.NumVirtualTexels / (1024.0f * 1024.0f));
	UE_LOG(LogRenderer, Log, TEXT("  Locked virtual texels: %.3fM"), SurfaceStats.NumLockedVirtualTexels / (1024.0f * 1024.0f));
	UE_LOG(LogRenderer, Log, TEXT("  Physical texels: %.3fM, usage: %.3f%%"), SurfaceStats.NumPhysicalTexels / (1024.0f * 1024.0f), (100.0f * SurfaceStats.NumPhysicalTexels) / (PhysicalAtlasSize.X * PhysicalAtlasSize.Y));
	UE_LOG(LogRenderer, Log, TEXT("  Locked Physical texels: %.3fM, usage: %.3f%%"), SurfaceStats.NumLockedPhysicalTexels / (1024.0f * 1024.0f), (100.0f * SurfaceStats.NumLockedPhysicalTexels) / (PhysicalAtlasSize.X * PhysicalAtlasSize.Y));
	UE_LOG(LogRenderer, Log, TEXT("  Dropped res levels: %u"), SurfaceStats.DroppedResLevels);
	UE_LOG(LogRenderer, Log, TEXT("  Mesh cards to add: %d"), NumMeshCardsToAdd);
	UE_LOG(LogRenderer, Log, TEXT("  Locked cards to update: %d"), NumLockedCardsToUpdate);
	UE_LOG(LogRenderer, Log, TEXT("  Hi-res pages to add: %d"), NumHiResPagesToAdd);

	UE_LOG(LogRenderer, Log, TEXT("*** CPU Memory ***"));
	UE_LOG(LogRenderer, Log, TEXT("  Primitives allocated memory: %.3fMb"), LumenPrimitives.GetAllocatedSize() / (1024.0f * 1024.0f));
	UE_LOG(LogRenderer, Log, TEXT("  Instances allocated memory: %.3fMb"), LumenPrimitiveInstanceAllocatedMemory / (1024.0f * 1024.0f));
	UE_LOG(LogRenderer, Log, TEXT("  Cards allocated memory: %.3fMb"), Cards.GetAllocatedSize() / (1024.0f * 1024.0f));
	UE_LOG(LogRenderer, Log, TEXT("  MeshCards allocated memory: %.3fMb"), MeshCards.GetAllocatedSize() / (1024.0f * 1024.0f));

	UE_LOG(LogRenderer, Log, TEXT("*** GPU Memory ***"));
	UE_LOG(LogRenderer, Log, TEXT("  CardBuffer: %.3fMb"), CardBuffer.NumBytes / (1024.0f * 1024.0f));
	UE_LOG(LogRenderer, Log, TEXT("  MeshCardsBuffer: %.3fMb"), MeshCardsBuffer.NumBytes / (1024.0f * 1024.0f));
	UE_LOG(LogRenderer, Log, TEXT("  PageTable: %.3fMb"), PageTableBuffer.NumBytes / (1024.0f * 1024.0f));
	UE_LOG(LogRenderer, Log, TEXT("  CardPages: %.3fMb"), CardPageBuffer.NumBytes / (1024.0f * 1024.0f));
	UE_LOG(LogRenderer, Log, TEXT("  DFObjectToMeshCardsIndexBuffer: %.3fMb"), DFObjectToMeshCardsIndexBuffer.NumBytes / (1024.0f * 1024.0f));
	UE_LOG(LogRenderer, Log, TEXT("  LumenDFInstanceToDFObjectIndexBuffer: %.3fMb"), LumenDFInstanceToDFObjectIndexBuffer.NumBytes / (1024.0f * 1024.0f));
	UE_LOG(LogRenderer, Log, TEXT("  UploadBuffer: %.3fMb"), UploadBuffer.GetNumBytes() / (1024.0f * 1024.0f));
	UE_LOG(LogRenderer, Log, TEXT("  ByteBufferUploadBuffer: %.3fMb"), ByteBufferUploadBuffer.GetNumBytes() / (1024.0f * 1024.0f));
}