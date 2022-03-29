// Copyright Epic Games, Inc. All Rights Reserved.

#include "StrataMaterial.h"
#include "MaterialCompiler.h"



FString GetStrataBSDFName(uint8 BSDFType)
{
	switch (BSDFType)
	{
	case STRATA_BSDF_TYPE_SLAB:
		return TEXT("SLAB");
		break;
	case STRATA_BSDF_TYPE_VOLUMETRICFOGCLOUD:
		return TEXT("VOLUMETRICFOGCLOUD");
		break;
	case STRATA_BSDF_TYPE_UNLIT:
		return TEXT("UNLIT");
		break;
	case STRATA_BSDF_TYPE_HAIR:
		return TEXT("HAIR");
		break;
	case STRATA_BSDF_TYPE_SINGLELAYERWATER:
		return TEXT("SINGLELAYERWATER");
		break;
	}
	check(false);
	return "";
}

FStrataRegisteredSharedLocalBasis StrataCompilationInfoCreateNullSharedLocalBasis()
{
	return FStrataRegisteredSharedLocalBasis();
}

FStrataRegisteredSharedLocalBasis StrataCompilationInfoCreateSharedLocalBasis(FMaterialCompiler* Compiler, int32 NormalCodeChunk, int32 TangentCodeChunk)
{
	if (TangentCodeChunk == INDEX_NONE)
	{
		return Compiler->StrataCompilationInfoRegisterSharedLocalBasis(NormalCodeChunk);
	}
	return Compiler->StrataCompilationInfoRegisterSharedLocalBasis(NormalCodeChunk, TangentCodeChunk);
}

//FStrataMaterialAnalysisResult::FStrataMaterialAnalysisResult()
//{
//	bFitInMemoryBudget = true;
//	RequestedLayerCount = 0;
//	RequestedBSDFCount = 0;
//	RequestedByteCount = 0;
//	ClampedLayerCount = 0;
//	ClampedBSDFCount = 0;
//	UsedByteCount = 0;
//}
//
//FStrataMaterialAnalysisResult StrataCompilationInfoMaterialAnalysis(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& Material, const uint32 StrataBytePerPixel)
//{
//	const uint32 UintByteSize = sizeof(uint32);
//
//	FStrataMaterialAnalysisResult Result;
//
//	// 1. Header
//
//	// SharedLocalBases_BSDFCount
//	Result.RequestedByteCount += UintByteSize;
//
//	// shared local bases between BSDFs
//	Result.RequestedByteCount += Compiler->StrataCompilationInfoGetSharedLocalBasesCount() * STRATA_PACKED_SHAREDLOCALBASIS_STRIDE_BYTES;
//	
//	// 2. The list of BSDFs
//	Result.RequestedMaxBSDFCountPerLayer = 0;
//	// We process layers from top to bottom to cull the bottom ones in case we run out of pixel bytes
//	for (uint32 LayerIt = 0; LayerIt < Material.LayerCount; LayerIt++)
//	{
//		const FStrataMaterialCompilationInfo::FLayer& Layer = Material.Layers[LayerIt];
//
//		const bool bTopLayer = LayerIt == 0;
//		const bool bBottomLayer = LayerIt == (Material.LayerCount - 1);
//
//		for (uint32 BSDFIt = 0; BSDFIt < Layer.BSDFCount; BSDFIt++)
//		{
//			const FStrataMaterialCompilationInfo::FBSDF& BSDF = Layer.BSDFs[BSDFIt];
//
//			// BSDF state
//			Result.RequestedByteCount += UintByteSize;
//
//			// From the compiler side, we can only assume the top layer has grey scale weight/throughput
//			const bool bMayBeColoredWeight = LayerIt > 0;
//			if (bMayBeColoredWeight)
//			{
//				Result.RequestedByteCount += UintByteSize;
//			}
//
//			switch (BSDF.Type)
//			{
//			case STRATA_BSDF_TYPE_SLAB:
//			{
//				// Compute values closer to the reality for HasSSS and IsSimpleVolume, now that we know that we know the topology of the material.
//				const bool bIsSimpleVolume = !bBottomLayer && BSDF.bHasMFPPluggedIn;
//				const bool bHasSSS = bBottomLayer && BSDF.bHasSSS && !bIsSimpleVolume;
//
//				Result.RequestedByteCount += UintByteSize;
//				Result.RequestedByteCount += UintByteSize;
//				if (BSDF.bHasEdgeColor || BSDF.bHasThinFilm || BSDF.bHasHaziness)
//				{
//					Result.RequestedByteCount += UintByteSize;
//				}
//				if (bHasSSS || bIsSimpleVolume)
//				{
//					Result.RequestedByteCount += UintByteSize;
//				}
//				if (BSDF.bHasFuzz)
//				{
//					Result.RequestedByteCount += UintByteSize;
//				}
//				break;
//			}
//			case STRATA_BSDF_TYPE_HAIR:
//			{
//				Result.RequestedByteCount += UintByteSize;
//				Result.RequestedByteCount += UintByteSize;
//				break;
//			}
//			case STRATA_BSDF_TYPE_SINGLELAYERWATER:
//			{
//				Result.RequestedByteCount += UintByteSize;
//				Result.RequestedByteCount += UintByteSize;
//				break;
//			}
//			}
//		}
//		Result.RequestedMaxBSDFCountPerLayer = FMath::Max(Result.RequestedMaxBSDFCountPerLayer, Layer.BSDFCount);
//
//		Result.RequestedLayerCount++;
//		Result.RequestedBSDFCount += Layer.BSDFCount;
//
//		if (Result.RequestedByteCount <= StrataBytePerPixel && Result.bFitInMemoryBudget)
//		{
//			// We only validate all the BSDF of a layer if it remains within budget and we are not already out of budget
//			Result.ClampedBSDFCount += Layer.BSDFCount;
//			Result.ClampedLayerCount++;
//
//			// Set the current used bytes
//			Result.UsedByteCount = Result.RequestedByteCount;
//		}
//		else
//		{
//			// Used byte count remains unchanged and we notify that we start peeling off top layers now
//			Result.bFitInMemoryBudget = false;
//		}
//	}
//	return Result;
//
//}
	

