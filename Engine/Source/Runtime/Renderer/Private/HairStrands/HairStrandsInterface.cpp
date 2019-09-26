// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairStrandsInterface.h: Hair manager implementation.
=============================================================================*/

#include "HairStrandsInterface.h"
#include "HairStrandsUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogHairRendering, Log, All);

static int32 GHairStrandsRenderingEnable = 1;
static FAutoConsoleVariableRef CVarHairStrandsRenderingEnable(TEXT("r.HairStrands.Enable"), GHairStrandsRenderingEnable, TEXT("Enable/Disable hair strands rendering"));

struct FHairStrandsManager
{
	struct Element
	{
		uint64 Id;
		FHairStrandsInterpolation Data;
	};
	TArray<Element> Elements;
};

FHairStrandsManager GHairManager;

void RegisterHairStrands(uint64 Id, const FHairStrandsInterpolation& E)
{ 
	for (int32 Index = 0; Index < GHairManager.Elements.Num(); ++Index)
	{
		if (GHairManager.Elements[Index].Id == Id)
		{
			// Component already registered. This should not happen.
			UE_LOG(LogHairRendering, Warning, TEXT("Component already register. This should't happen. Please report this to a rendering engineer."))
			return;
		}
	}

	GHairManager.Elements.Add({ Id, E });
}

FHairStrandsInterpolation UnregisterHairStrands(uint64 Id)
{
	for (int32 Index=0;Index< GHairManager.Elements.Num();++Index)
	{
		if (GHairManager.Elements[Index].Id == Id)
		{
			FHairStrandsInterpolation Out = GHairManager.Elements[Index].Data;
			GHairManager.Elements[Index] = GHairManager.Elements[GHairManager.Elements.Num()-1];
			GHairManager.Elements.SetNum(GHairManager.Elements.Num() - 1);
			return Out;
		}
	}

	return FHairStrandsInterpolation();
}

void RunHairStrandsInterpolation(FRHICommandListImmediate& RHICmdList)
{
	for (FHairStrandsManager::Element& E : GHairManager.Elements)
	{
		if (E.Data.Input && E.Data.Output && E.Data.Function)
		{
			E.Data.Function(RHICmdList, E.Data.Input, E.Data.Output);
		}
	}
}

bool IsHairStrandsEnable(EShaderPlatform Platform) { return IsHairStrandsSupported(Platform) && GHairStrandsRenderingEnable == 1 && GHairManager.Elements.Num() > 0; }