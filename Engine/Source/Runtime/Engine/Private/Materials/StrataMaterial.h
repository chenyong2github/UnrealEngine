// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StrataDefinitions.h"
#include "Containers/Map.h"



class FMaterialCompiler;



struct FStrataMaterialCompilationInfo
{
	struct FBSDF
	{
		uint8 Type;
		uint8 SharedNormalIndex;
	};

	struct FLayer
	{
		uint32 BSDFCount;
		FBSDF BSDFs[STRATA_MAX_BSDF_COUNT_PER_LAYER];
	};

	uint32 LayerCount;
	FLayer Layers[STRATA_MAX_LAYER_COUNT];

	uint32 TotalBSDFCount;

	FStrataMaterialCompilationInfo()
	{
		TotalBSDFCount = 0;
		LayerCount = 0;
		memset(Layers, 0, sizeof(Layers));
	}
};

FString GetStrataBSDFName(uint8 BSDFType);

uint8 StrataCompilationInfoCreateSharedNormal(FMaterialCompiler* Compiler, int32 NormalCodeChunk);
void  StrataCompilationInfoCreateSingleBSDFMaterial(FMaterialCompiler* Compiler, int32 CodeChunk, uint8 SharedNormalIndex, uint8 BSDFType);

FStrataMaterialCompilationInfo StrataCompilationInfoAdd(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& A, const FStrataMaterialCompilationInfo& B);
FStrataMaterialCompilationInfo StrataCompilationInfoMultiply(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& A);
FStrataMaterialCompilationInfo StrataCompilationInfoHorizontalMixing(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& A, const FStrataMaterialCompilationInfo& B);
FStrataMaterialCompilationInfo StrataCompilationInfoVerticalLayering(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& Top, const FStrataMaterialCompilationInfo& Base);


