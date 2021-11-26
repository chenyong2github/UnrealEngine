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

static void UpdateTotalBSDFCount(FMaterialCompiler* Compiler, FStrataMaterialCompilationInfo& StrataInfo)
{
	StrataInfo.TotalBSDFCount = 0;
	for (uint32 LayerIt = 0; LayerIt < StrataInfo.LayerCount; ++LayerIt)
	{
		StrataInfo.TotalBSDFCount += StrataInfo.Layers[LayerIt].BSDFCount;
	}

	if ((StrataInfo.TotalBSDFCount) > STRATA_MAX_BSDF_COUNT)
	{
		Compiler->Error(TEXT("This material contains too many BSDFs"));
	}
}

FStrataRegisteredSharedLocalBasis StrataCompilationInfoCreateNullSharedLocalBasis()
{
	FStrataRegisteredSharedLocalBasis FakeSharedLocalBasis;
	FakeSharedLocalBasis.NormalCodeChunk = INDEX_NONE;
	FakeSharedLocalBasis.TangentCodeChunk = INDEX_NONE;
	FakeSharedLocalBasis.NormalCodeChunkHash = 0;
	FakeSharedLocalBasis.TangentCodeChunkHash = 0;
	FakeSharedLocalBasis.GraphSharedLocalBasisIndex = 0;
	return FakeSharedLocalBasis;
}

FStrataRegisteredSharedLocalBasis StrataCompilationInfoCreateSharedLocalBasis(FMaterialCompiler* Compiler, int32 NormalCodeChunk, int32 TangentCodeChunk)
{
	if (TangentCodeChunk == INDEX_NONE)
	{
		return Compiler->StrataCompilationInfoRegisterSharedLocalBasis(NormalCodeChunk);
	}
	return Compiler->StrataCompilationInfoRegisterSharedLocalBasis(NormalCodeChunk, TangentCodeChunk);
}

void StrataCompilationInfoCreateSingleBSDFMaterial(FMaterialCompiler* Compiler, int32 CodeChunk,
	const FStrataRegisteredSharedLocalBasis& RegisteredSharedLocalBasis,
	uint8 BSDFType, bool bHasSSS, bool bHasDMFPPluggedIn, bool bHasEdgeColor, bool bHasThinFilm, bool bHasFuzz, bool bHasHaziness)
{
	FStrataMaterialCompilationInfo StrataInfo;
	StrataInfo.LayerCount = 1;
	StrataInfo.Layers[0].BSDFCount = 1;
	StrataInfo.Layers[0].BSDFs[0].Type = BSDFType;
	StrataInfo.Layers[0].BSDFs[0].RegisteredSharedLocalBasis = RegisteredSharedLocalBasis;
	StrataInfo.Layers[0].BSDFs[0].bHasSSS = bHasSSS;
	StrataInfo.Layers[0].BSDFs[0].bHasDMFPPluggedIn = bHasDMFPPluggedIn;
	StrataInfo.Layers[0].BSDFs[0].bHasEdgeColor = bHasEdgeColor;
	StrataInfo.Layers[0].BSDFs[0].bHasThinFilm = bHasThinFilm;
	StrataInfo.Layers[0].BSDFs[0].bHasFuzz = bHasFuzz;
	StrataInfo.Layers[0].BSDFs[0].bHasHaziness = bHasHaziness;
	UpdateTotalBSDFCount(Compiler, StrataInfo);
	Compiler->StrataCompilationInfoRegisterCodeChunk(CodeChunk, StrataInfo);
}

void  StrataCompilationInfoCreateNullBSDF(FMaterialCompiler* Compiler, int32 CodeChunk)
{
	FStrataMaterialCompilationInfo StrataInfo;
	StrataInfo.LayerCount = 0;
	StrataInfo.TotalBSDFCount = 0;
	Compiler->StrataCompilationInfoRegisterCodeChunk(CodeChunk, StrataInfo);
}


FStrataMaterialCompilationInfo StrataCompilationInfoWeight(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& A)
{
	FStrataMaterialCompilationInfo StrataInfo = A;
	return StrataInfo;
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

	UpdateTotalBSDFCount(Compiler, StrataInfo);
	return StrataInfo;
}


// ==> NOTE: Always pair with the shader behavior in StrataAddParameterBlending.
FStrataMaterialCompilationInfo StrataCompilationInfoAddParamBlend(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& A, const FStrataMaterialCompilationInfo& B, const FStrataRegisteredSharedLocalBasis& RegisteredSharedLocalBasis)
{
	check(A.TotalBSDFCount == 1);
	check(B.TotalBSDFCount == 1);

	FStrataMaterialCompilationInfo StrataInfo = A;
	FStrataMaterialCompilationInfo::FBSDF& NewBSDF = StrataInfo.Layers[0].BSDFs[0];
	const FStrataMaterialCompilationInfo::FBSDF& OtherBSDF = B.Layers[0].BSDFs[0];

	NewBSDF.RegisteredSharedLocalBasis = RegisteredSharedLocalBasis;

	// When parameter blending is used, we take the union of all the features activated by input BSDFs.
	NewBSDF.bHasSSS				|=	OtherBSDF.bHasSSS;
	NewBSDF.bHasDMFPPluggedIn	|=	OtherBSDF.bHasDMFPPluggedIn;
	NewBSDF.bHasEdgeColor		|=	OtherBSDF.bHasEdgeColor;
	NewBSDF.bHasThinFilm		|=	OtherBSDF.bHasThinFilm;
	NewBSDF.bHasFuzz			|=	OtherBSDF.bHasFuzz;
	NewBSDF.bHasHaziness		|=	OtherBSDF.bHasHaziness;
	UpdateTotalBSDFCount(Compiler, StrataInfo);
	return StrataInfo;
}


FStrataMaterialCompilationInfo StrataCompilationInfoHorizontalMixing(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& Background, const FStrataMaterialCompilationInfo& Foreground)
{
	return StrataCompilationInfoAdd(Compiler, Background, Foreground); // Mixing is a similar operation to Add when it comes to bsdf count
}


// ==> NOTE: Always pair with the shader behavior in StrataHorizontalMixingParameterBlending
FStrataMaterialCompilationInfo StrataCompilationInfoHorizontalMixingParamBlend(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& Background, const FStrataMaterialCompilationInfo& Foreground, const FStrataRegisteredSharedLocalBasis& RegisteredSharedLocalBasis)
{
	check(Background.TotalBSDFCount == 1);
	check(Foreground.TotalBSDFCount == 1);

	FStrataMaterialCompilationInfo StrataInfo = Background;
	FStrataMaterialCompilationInfo::FBSDF& NewBSDF = StrataInfo.Layers[0].BSDFs[0];
	const FStrataMaterialCompilationInfo::FBSDF& OtherBSDF = Foreground.Layers[0].BSDFs[0];

	NewBSDF.RegisteredSharedLocalBasis = RegisteredSharedLocalBasis;

	// When parameter blending is used, we take the union of all the features activated by input BSDFs.
	NewBSDF.bHasSSS				|=	OtherBSDF.bHasSSS;
	NewBSDF.bHasDMFPPluggedIn	|=	OtherBSDF.bHasDMFPPluggedIn;
	NewBSDF.bHasEdgeColor		|=	OtherBSDF.bHasEdgeColor;
	NewBSDF.bHasThinFilm		|=	OtherBSDF.bHasThinFilm;
	NewBSDF.bHasFuzz			|=	OtherBSDF.bHasFuzz;
	NewBSDF.bHasHaziness		|=	OtherBSDF.bHasHaziness;
	UpdateTotalBSDFCount(Compiler, StrataInfo);
	return StrataInfo;
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

	UpdateTotalBSDFCount(Compiler, StrataInfo);
	return StrataInfo;
}


// ==> NOTE: Always pair with the shader behavior in StrataVerticalLayeringParameterBlending
FStrataMaterialCompilationInfo StrataCompilationInfoVerticalLayeringParamBlend(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& Top, const FStrataMaterialCompilationInfo& Base, const FStrataRegisteredSharedLocalBasis& RegisteredSharedLocalBasis)
{
	check(Top.TotalBSDFCount == 1);
	check(Base.TotalBSDFCount == 1);

	FStrataMaterialCompilationInfo StrataInfo = Base;
	FStrataMaterialCompilationInfo::FBSDF& NewBSDF = StrataInfo.Layers[0].BSDFs[0];
	const FStrataMaterialCompilationInfo::FBSDF& TopBSDF = Top.Layers[0].BSDFs[0];

	NewBSDF.RegisteredSharedLocalBasis = RegisteredSharedLocalBasis;

//	NewBSDF.bHasSSS				= TopBSDF.bHasSSS;				// We keep SSS only if the base layer has it. Otherwise it will be simple volume and the throughput wll be applied on the parameters.
//	NewBSDF.bHasDMFPPluggedIn	= TopBSDF.bHasDMFPPluggedIn;	// Idem
	NewBSDF.bHasEdgeColor		|= TopBSDF.bHasEdgeColor;		// We keep the union of both, even though it will be hard to get a perfect match.
	NewBSDF.bHasFuzz			|= TopBSDF.bHasFuzz;			// Idem
	NewBSDF.bHasHaziness		|= TopBSDF.bHasHaziness;		// Idem
	NewBSDF.bHasThinFilm		 = TopBSDF.bHasThinFilm;		// We only keep thin film from the top layer, because its color is otherwise not controlable

	UpdateTotalBSDFCount(Compiler, StrataInfo);
	return StrataInfo;
}

static bool StrataIsSingleBSDF(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& Material)
{
	if (Material.TotalBSDFCount == 0 || Material.LayerCount == 0)
	{
		Compiler->Error(TEXT("There is no layer or BSDF plugged in, but one of the BSDF in the graph wants to enforce one and only one BSDF to be used."));
		return false;
	}
	if (Material.TotalBSDFCount > 1 || Material.LayerCount > 1)
	{
		Compiler->Error(TEXT("There is more than one layer or BSDF, but one of the BSDF in the graph wants to enforce one and only one BSDF to be used."));
		return false;
	}
	return true;
}

bool StrataIsVolumetricFogCloudOnly(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& Material)
{
	if (!StrataIsSingleBSDF(Compiler, Material))
	{
		return false;
	}
	if (Material.Layers[0].BSDFs[0].Type != STRATA_BSDF_TYPE_VOLUMETRICFOGCLOUD)
	{
		Compiler->Error(TEXT("The single BSDF resulting from the graph is not of type Volume."));
		return false;
	}

	return true;
}

bool StrataIsUnlitOnly(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& Material)
{
	if (!StrataIsSingleBSDF(Compiler, Material))
	{
		return false;
	}
	if (Material.Layers[0].BSDFs[0].Type != STRATA_BSDF_TYPE_UNLIT)
	{
		Compiler->Error(TEXT("The single BSDF resulting from the graph is not of type Unlit."));
		return false;
	}

	return true;
}

bool StrataIsHairOnly(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& Material)
{
	if (!StrataIsSingleBSDF(Compiler, Material))
	{
		return false;
	}
	if (Material.Layers[0].BSDFs[0].Type != STRATA_BSDF_TYPE_HAIR)
	{
		Compiler->Error(TEXT("The single BSDF resulting from the graph is not of type Hair."));
		return false;
	}

	return true;
}

bool StrataIsSingleLayerWaterOnly(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& Material)
{
	if (!StrataIsSingleBSDF(Compiler, Material))
	{
		return false;
	}
	if (Material.Layers[0].BSDFs[0].Type != STRATA_BSDF_TYPE_SINGLELAYERWATER)
	{
		Compiler->Error(TEXT("The single BSDF resulting from the graph is not of type SingleLayerWater."));
		return false;
	}

	return true;
}

bool StrataMaterialContainsAnyBSDF(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& Material, uint8 BSDFType)
{
	for (uint32 LayerIt = 0; LayerIt < Material.LayerCount; ++LayerIt)
	{
		const FStrataMaterialCompilationInfo::FLayer& Layer = Material.Layers[LayerIt];
		for (uint32 BSDF = 0; BSDF < Layer.BSDFCount; BSDF++)
		{
			if (Layer.BSDFs[BSDF].Type == BSDFType)
			{
				return true;
			}
		}
	}
	return false;
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

	// SharedLocalBases_BSDFCount
	Result.RequestedByteCount += UintByteSize;

	// shared local bases between BSDFs
	Result.RequestedByteCount += Compiler->StrataCompilationInfoGetSharedLocalBasesCount() * STRATA_PACKED_SHAREDLOCALBASIS_STRIDE_BYTES;
	
	// 2. The list of BSDFs
	Result.RequestedMaxBSDFCountPerLayer = 0;
	// We process layers from top to bottom to cull the bottom ones in case we run out of pixel bytes
	for (uint32 LayerIt = 0; LayerIt < Material.LayerCount; LayerIt++)
	{
		const FStrataMaterialCompilationInfo::FLayer& Layer = Material.Layers[LayerIt];

		const bool bTopLayer = LayerIt == 0;
		const bool bBottomLayer = LayerIt == (Material.LayerCount - 1);

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
			case STRATA_BSDF_TYPE_SLAB:
			{
				// Compute values closer to the reality for HasSSS and IsSimpleVolume, now that we know that we know the topology of the material.
				const bool bIsSimpleVolume = !bBottomLayer && BSDF.bHasDMFPPluggedIn;
				const bool bHasSSS = bBottomLayer && BSDF.bHasSSS && !bIsSimpleVolume;

				Result.RequestedByteCount += UintByteSize;
				Result.RequestedByteCount += UintByteSize;
				if (BSDF.bHasEdgeColor || BSDF.bHasThinFilm || BSDF.bHasHaziness)
				{
					Result.RequestedByteCount += UintByteSize;
				}
				if (bHasSSS || bIsSimpleVolume)
				{
					Result.RequestedByteCount += UintByteSize;
				}
				if (BSDF.bHasFuzz)
				{
					Result.RequestedByteCount += UintByteSize;
				}
				break;
			}
			case STRATA_BSDF_TYPE_HAIR:
			{
				Result.RequestedByteCount += UintByteSize;
				Result.RequestedByteCount += UintByteSize;
				break;
			}
			case STRATA_BSDF_TYPE_SINGLELAYERWATER:
			{
				Result.RequestedByteCount += UintByteSize;
				Result.RequestedByteCount += UintByteSize;
				break;
			}
			}
		}
		Result.RequestedMaxBSDFCountPerLayer = FMath::Max(Result.RequestedMaxBSDFCountPerLayer, Layer.BSDFCount);

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
	

