// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StrataDefinitions.h"
#include "Containers/Map.h"



// Material conversion to strata is disabled until ready.
// To enable it locally:
//		- Set CONVERT_MATERIAL_TO_STRATA_ON_LOAD to 1
//		- Locally change ShaderVersion.ush GUID
// !!!!!! !!!!!! DO NOT CHECK THIS IN SET TO 1 !!!!!! !!!!!!
#define CONVERT_MATERIAL_TO_STRATA_ON_LOAD 0



class FMaterialCompiler;



struct FStrataMaterialCompilationInfo
{
	struct FBSDF
	{
		uint8 Type;
		uint8 SharedNormalIndex;
		bool  bHasScattering;
		bool  bHasEdgeColor;
		bool  bHasThinFilm;
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
uint8 StrataCompilationInfoCreateSharedNormal(FMaterialCompiler* Compiler, int32 NormalCodeChunk, int32 TangentCodeChunk);
void  StrataCompilationInfoCreateNullBSDF(FMaterialCompiler* Compiler, int32 CodeChunk);
void  StrataCompilationInfoCreateSingleBSDFMaterial(FMaterialCompiler* Compiler, int32 CodeChunk, uint8 SharedNormalIndex, uint8 BSDFType, bool bHasEdgeColor = false, bool bHasScattering = false, bool bHasThinFilm = false);

FStrataMaterialCompilationInfo StrataCompilationInfoAdd(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& A, const FStrataMaterialCompilationInfo& B);
FStrataMaterialCompilationInfo StrataCompilationInfoMultiply(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& A);
FStrataMaterialCompilationInfo StrataCompilationInfoHorizontalMixing(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& A, const FStrataMaterialCompilationInfo& B);
FStrataMaterialCompilationInfo StrataCompilationInfoVerticalLayering(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& Top, const FStrataMaterialCompilationInfo& Base);

bool StrataIsVolumetricFogCloudOnly(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& Material);
bool StrataIsUnlitOnly(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& Material);
bool StrataIsHairOnly(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& Material);
bool StrataIsSingleLayerWaterOnly(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& Material);
bool StrataMaterialContainsAnyBSDF(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& Material, uint8 BSDFType);

struct FStrataMaterialAnalysisResult
{
	FStrataMaterialAnalysisResult();
	bool bFitInMemoryBudget;
	uint32 RequestedLayerCount;
	uint32 RequestedBSDFCount;
	uint32 RequestedByteCount;
	uint32 ClampedLayerCount;
	uint32 ClampedBSDFCount;
	uint32 UsedByteCount;
};
FStrataMaterialAnalysisResult StrataCompilationInfoMaterialAnalysis(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& Material, const uint32 StrataBytePerPixel);
