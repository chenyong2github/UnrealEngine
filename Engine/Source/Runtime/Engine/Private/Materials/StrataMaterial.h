// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StrataDefinitions.h"
#include "Containers/Map.h"
#include "MaterialCompiler.h"



class FMaterialCompiler;



struct FStrataMaterialCompilationInfo
{
	struct FBSDF
	{
		uint8 Type;
		FStrataRegisteredSharedLocalBasis RegisteredSharedLocalBasis;

		// Those properties are centered around the SLAB node. Maybe this can be abstracted at some point.
		bool  bHasSSS;
		bool  bHasDMFPPluggedIn;
		bool  bHasEdgeColor;
		bool  bHasThinFilm;
		bool  bHasFuzz;
		bool  bHasHaziness;
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

	uint32 GetPerLayerMaxBSDFCount() const 
	{
		uint32 Count = 0;
		for (uint32 LayerIt = 0; LayerIt < LayerCount; ++LayerIt)
		{
			Count = FMath::Max(Count, Layers[LayerIt].BSDFCount);
		}
		return Count;
	}
};

FString GetStrataBSDFName(uint8 BSDFType);

FStrataRegisteredSharedLocalBasis StrataCompilationInfoCreateNullSharedLocalBasis();
FStrataRegisteredSharedLocalBasis StrataCompilationInfoCreateSharedLocalBasis(FMaterialCompiler* Compiler, int32 NormalCodeChunk, int32 TangentCodeChunk = INDEX_NONE);

void  StrataCompilationInfoCreateNullBSDF(FMaterialCompiler* Compiler, int32 CodeChunk);
void  StrataCompilationInfoCreateSingleBSDFMaterial(FMaterialCompiler* Compiler, int32 CodeChunk, 
	const FStrataRegisteredSharedLocalBasis& RegisteredSharedLocalBasis,
	uint8 BSDFType, bool bHasSSS = false, bool bHasDMFPPluggedIn = false, bool bHasEdgeColor = false, bool bHasThinFilm = false, bool bHasFuzz = false, bool bHasHaziness = false);

FStrataMaterialCompilationInfo StrataCompilationInfoWeight(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& A);
FStrataMaterialCompilationInfo StrataCompilationInfoAdd(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& A, const FStrataMaterialCompilationInfo& B);
FStrataMaterialCompilationInfo StrataCompilationInfoAddParamBlend(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& A, const FStrataMaterialCompilationInfo& B, const FStrataRegisteredSharedLocalBasis& RegisteredSharedLocalBasis);
FStrataMaterialCompilationInfo StrataCompilationInfoHorizontalMixing(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& Background, const FStrataMaterialCompilationInfo& Foreground);
FStrataMaterialCompilationInfo StrataCompilationInfoHorizontalMixingParamBlend(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& Background, const FStrataMaterialCompilationInfo& Foreground, const FStrataRegisteredSharedLocalBasis& RegisteredSharedLocalBasis);
FStrataMaterialCompilationInfo StrataCompilationInfoVerticalLayering(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& Top, const FStrataMaterialCompilationInfo& Base);
FStrataMaterialCompilationInfo StrataCompilationInfoVerticalLayeringParamBlend(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& Top, const FStrataMaterialCompilationInfo& Base, const FStrataRegisteredSharedLocalBasis& RegisteredSharedLocalBasis);

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
	uint32 RequestedMaxBSDFCountPerLayer;
	uint32 RequestedBSDFCount;
	uint32 RequestedByteCount;
	uint32 ClampedLayerCount;
	uint32 ClampedBSDFCount;
	uint32 UsedByteCount;
};
FStrataMaterialAnalysisResult StrataCompilationInfoMaterialAnalysis(FMaterialCompiler* Compiler, const FStrataMaterialCompilationInfo& Material, const uint32 StrataBytePerPixel);
