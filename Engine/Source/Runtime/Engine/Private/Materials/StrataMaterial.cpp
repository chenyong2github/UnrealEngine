// Copyright Epic Games, Inc. All Rights Reserved.

#include "StrataMaterial.h"
#include "MaterialCompiler.h"



FString GetStrataBSDFName(uint8 BSDFType)
{
	switch (BSDFType)
	{
	case STRATA_BSDF_TYPE_DIFFUSE:
		return TEXT("DIFFUSE");
		break;
	case STRATA_BSDF_TYPE_DIELECTRIC:
		return TEXT("DIELECTRIC");
		break;
	case STRATA_BSDF_TYPE_CONDUCTOR:
		return TEXT("CONDUCTOR");
		break;
	case STRATA_BSDF_TYPE_VOLUME:
		return TEXT("VOLUME");
		break;
	}
	check(false);
	return "";
}

static void UpdateTotalBSDFCount(FStrataMaterialCompilationInfo& StrataInfo)
{
	StrataInfo.TotalBSDFCount = 0;
	for (uint32 LayerIt = 0; LayerIt < StrataInfo.LayerCount; ++LayerIt)
	{
		StrataInfo.TotalBSDFCount += StrataInfo.Layers[LayerIt].BSDFCount;
	}
}


uint8 StrataCompilationInfoCreateSharedNormal(FMaterialCompiler* Compiler, int32 NormalCodeChunk)
{
	return Compiler->StrataCompilationInfoRegisterSharedNormalIndex(NormalCodeChunk);
}

uint8 StrataCompilationInfoCreateSharedNormal(FMaterialCompiler* Compiler, int32 NormalCodeChunk, int32 TangentCodeChunk)
{
	return Compiler->StrataCompilationInfoRegisterSharedNormalIndex(NormalCodeChunk, TangentCodeChunk);
}

void StrataCompilationInfoCreateSingleBSDFMaterial(FMaterialCompiler* Compiler, int32 CodeChunk, uint8 SharedNormalIndex, uint8 BSDFType)
{
	FStrataMaterialCompilationInfo StrataInfo;
	StrataInfo.LayerCount = 1;
	StrataInfo.Layers[0].BSDFCount = 1;
	StrataInfo.Layers[0].BSDFs[0].Type = BSDFType;
	StrataInfo.Layers[0].BSDFs[0].SharedNormalIndex = SharedNormalIndex;
	UpdateTotalBSDFCount(StrataInfo);
	Compiler->StrataCompilationInfoRegisterCodeChunk(CodeChunk, StrataInfo);
}


FStrataMaterialCompilationInfo StrataCompilationInfoAdd(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& A, const FStrataMaterialCompilationInfo& B)
{
	FStrataMaterialCompilationInfo StrataInfo = A;

	// Append each BSDF from B to A, with same layer position
	for (uint32 LayerIt = 0; LayerIt < B.LayerCount; ++LayerIt)
	{
		const FStrataMaterialCompilationInfo::FLayer& ALayer = A.Layers[LayerIt];
		const FStrataMaterialCompilationInfo::FLayer& BLayer = B.Layers[LayerIt];

		if ((ALayer.BSDFCount + BLayer.BSDFCount) > STRATA_MAX_BSDF_COUNT_PER_LAYER)
		{
			Compiler->Error(TEXT("Adding would result in too many BSDFs in a Layer"));
			return A;
		}

		for (uint32 BSDF = 0; BSDF < BLayer.BSDFCount; BSDF++)
		{
			StrataInfo.Layers[LayerIt].BSDFs[ALayer.BSDFCount + BSDF] = BLayer.BSDFs[BSDF];
		}

		StrataInfo.Layers[LayerIt].BSDFCount = ALayer.BSDFCount + BLayer.BSDFCount;
	}
	StrataInfo.LayerCount = FMath::Max(A.LayerCount, B.LayerCount);

	UpdateTotalBSDFCount(StrataInfo);
	return StrataInfo;
}


FStrataMaterialCompilationInfo StrataCompilationInfoMultiply(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& A)
{
	FStrataMaterialCompilationInfo StrataInfo = A;
	return StrataInfo;
}


FStrataMaterialCompilationInfo StrataCompilationInfoHorizontalMixing(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& A, const FStrataMaterialCompilationInfo& B)
{
	return StrataCompilationInfoAdd(Compiler, A, B); // Mixing is a similar operation to Add when it comes to bsdf count
}


FStrataMaterialCompilationInfo StrataCompilationInfoVerticalLayering(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& Top, const FStrataMaterialCompilationInfo& Base)
{
	if ((Top.LayerCount + Base.LayerCount) > STRATA_MAX_LAYER_COUNT)
	{
		Compiler->Error(TEXT("Layering would result in too many Layers"));
		return Base;
	}

	FStrataMaterialCompilationInfo StrataInfo = Top;

	// Add each layer from Base under Top
	const uint32 TopLayerCount = Top.LayerCount;
	for (uint32 LayerIt = 0; LayerIt < Base.LayerCount; ++LayerIt)
	{
		StrataInfo.Layers[TopLayerCount + LayerIt] = Base.Layers[LayerIt];
	}
	StrataInfo.LayerCount += Base.LayerCount;

	UpdateTotalBSDFCount(StrataInfo);
	return StrataInfo;
}

FStrataMaterialAnalysisResult::FStrataMaterialAnalysisResult()
{
	bFitInMemoryBudget = true;
	RequestedLayerCount = 0;
	RequestedBSDFCount = 0;
	RequestedByteCount = 0;
	ClampedLayerCount = 0;
	ClampedBSDFCount = 0;
	UsedByteCount = 0;
}

FStrataMaterialAnalysisResult StrataCompilationInfoMaterialAnalysis(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& Material, const uint32 StrataBytePerPixel)
{
	const uint32 UintByteSize = sizeof(uint32);

	FStrataMaterialAnalysisResult Result;

	// 1. Header

	// SharedNormals_BSDFCount
	Result.RequestedByteCount += UintByteSize;
	// Shared normals between BSDFs
	Result.RequestedByteCount += Compiler->StrataCompilationInfoGetSharedNormalCount() * STRATA_PACKED_NORMAL_STRIDE_BYTES;

	// 2. The list of BSDFs

	// We process layers from top to bottom to cull the bottom ones in case we run out of pixel bytes
	for (uint32 LayerIt = 0; LayerIt < Material.LayerCount; LayerIt++)
	{
		const FStrataMaterialCompilationInfo::FLayer& Layer = Material.Layers[LayerIt];

		for (uint32 BSDFIt = 0; BSDFIt < Layer.BSDFCount; BSDFIt++)
		{
			const FStrataMaterialCompilationInfo::FBSDF& BSDF = Layer.BSDFs[BSDFIt];

			// BSDF state
			Result.RequestedByteCount += UintByteSize;

			// From the compiler side, we can only assume the top layer has grey scale weight/throughput
			const bool bMayBeColoredWeight = LayerIt > 0;
			if (bMayBeColoredWeight)
			{
				Result.RequestedByteCount += UintByteSize;
			}

			switch (BSDF.Type)
			{
			case STRATA_BSDF_TYPE_DIFFUSE:
			{
				Result.RequestedByteCount += UintByteSize;
				break;
			}
			case STRATA_BSDF_TYPE_DIELECTRIC:
			{
				Result.RequestedByteCount += UintByteSize;
				Result.RequestedByteCount += UintByteSize;
				break;
			}
			case STRATA_BSDF_TYPE_CONDUCTOR:
			{
				Result.RequestedByteCount += UintByteSize;
				Result.RequestedByteCount += UintByteSize;
				break;
			}
			case STRATA_BSDF_TYPE_VOLUME:
			{
				Result.RequestedByteCount += UintByteSize;

				const bool bMayHaveScattering = true;
				if (bMayHaveScattering)
				{
					Result.RequestedByteCount += UintByteSize;
				}
				break;
			}
			}
		}

		Result.RequestedLayerCount++;
		Result.RequestedBSDFCount += Layer.BSDFCount;

		if (Result.RequestedByteCount <= StrataBytePerPixel && Result.bFitInMemoryBudget)
		{
			// We only validate all the BSDF of a layer if it remains within budget and we are not already out of budget
			Result.ClampedBSDFCount += Layer.BSDFCount;
			Result.ClampedLayerCount++;

			// Set the current used bytes
			Result.UsedByteCount = Result.RequestedByteCount;
		}
		else
		{
			// Used byte count remains unchanged and we notify that we start peeling off top layers now
			Result.bFitInMemoryBudget = false;
		}
	}
	return Result;

}
	

