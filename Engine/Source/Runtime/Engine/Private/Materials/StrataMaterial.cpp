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
	if ((A.TotalBSDFCount + B.TotalBSDFCount) > STRATA_MAX_TOTAL_BSDF)
	{
		Compiler->Error(TEXT("Adding would result in too many BSDFs"));
		return A;
	}

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
	if ((A.TotalBSDFCount + B.TotalBSDFCount) > STRATA_MAX_TOTAL_BSDF)
	{
		Compiler->Error(TEXT("Mixing would result in too many BSDFs"));
		return A;
	}

	return StrataCompilationInfoAdd(Compiler, A, B); // Mixing is a similar operation to Add when it comes to bsdf count
}


FStrataMaterialCompilationInfo StrataCompilationInfoVerticalLayering(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& Top, const FStrataMaterialCompilationInfo& Base)
{
	if ((Top.TotalBSDFCount + Base.TotalBSDFCount) > STRATA_MAX_TOTAL_BSDF) // See StrataDefinitions.h
	{
		Compiler->Error(TEXT("Layering would result in too many BSDFs"));
		return Base;
	}

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

