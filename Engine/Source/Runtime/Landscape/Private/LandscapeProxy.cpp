// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapeProxy.h"
#include "LandscapeInfo.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialExpressionLandscapeVisibilityMask.h"
#include "Materials/MaterialExpressionLandscapeLayerWeight.h"
#include "Materials/MaterialExpressionLandscapeLayerSample.h"
#include "Materials/MaterialExpressionLandscapeLayerBlend.h"
#include "Materials/MaterialExpressionLandscapeLayerSwitch.h"

#if WITH_EDITOR


// ----------------------------------------------------------------------------------

LANDSCAPE_API FLandscapeImportLayerInfo::FLandscapeImportLayerInfo(const FLandscapeInfoLayerSettings& InLayerSettings)
	: LayerName(InLayerSettings.GetLayerName())
	, LayerInfo(InLayerSettings.LayerInfoObj)
	, SourceFilePath(InLayerSettings.GetEditorSettings().ReimportLayerFilePath)
{
}

// ----------------------------------------------------------------------------------
// 
// Static initialization : 
FGetLayersFromMaterialCache* FGetLayersFromMaterialCache::ActiveCache = nullptr;

TArray<FName> FGetLayersFromMaterialCache::GetLayersFromMaterial(const UMaterialInterface* InMaterialInterface)
{
	// If we have an active cache at the moment, let's benefit from it, otherwise, recompute the list on the spot:
	return (ActiveCache) ? ActiveCache->GetLayersFromMaterialInternal(InMaterialInterface) : ComputeLayersFromMaterial(InMaterialInterface);
}

TArray<FName> FGetLayersFromMaterialCache::GetLayersFromMaterialInternal(const UMaterialInterface* InMaterialInterface)
{
	TArray<FName> Result;
	// Have we computed the layers for this material already? 
	if (TArray<FName>* CachedResult = PerMaterialLayersCache.Find(InMaterialInterface))
	{
		Result = *CachedResult;
	}
	else if (InMaterialInterface != nullptr)
	{
		// Recompute the layers now and add it to our cache : 
		Result = PerMaterialLayersCache.Add(InMaterialInterface, ComputeLayersFromMaterial(InMaterialInterface));
	}

	return Result;
}

TArray<FName> FGetLayersFromMaterialCache::ComputeLayersFromMaterial(const UMaterialInterface* InMaterialInterface)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ALandscapeProxy::ComputeLayersFromMaterial);
	TArray<FName> Result;

	if (InMaterialInterface)
	{
		TArray<FMaterialParameterInfo> OutParameterInfo;
		TArray<FGuid> Guids;
		if (const UMaterialInstance* Instance = Cast<const UMaterialInstance>(InMaterialInterface))
		{
			Instance->GetAllParameterInfo<UMaterialExpressionLandscapeLayerBlend>(OutParameterInfo, Guids);
			Instance->GetAllParameterInfo<UMaterialExpressionLandscapeLayerWeight>(OutParameterInfo, Guids);
			Instance->GetAllParameterInfo<UMaterialExpressionLandscapeLayerSwitch>(OutParameterInfo, Guids);
			Instance->GetAllParameterInfo<UMaterialExpressionLandscapeLayerSample>(OutParameterInfo, Guids);
		}
		else if (const UMaterial* Material = InMaterialInterface->GetMaterial())
		{
			Material->GetAllParameterInfo<UMaterialExpressionLandscapeLayerBlend>(OutParameterInfo, Guids);
			Material->GetAllParameterInfo<UMaterialExpressionLandscapeLayerWeight>(OutParameterInfo, Guids);
			Material->GetAllParameterInfo<UMaterialExpressionLandscapeLayerSwitch>(OutParameterInfo, Guids);
			Material->GetAllParameterInfo<UMaterialExpressionLandscapeLayerSample>(OutParameterInfo, Guids);
		}

		for (const FMaterialParameterInfo& ParameterInfo : OutParameterInfo)
		{
			Result.AddUnique(ParameterInfo.Name);
		}
	}

	return Result;
}

#endif // WITH_EDITOR
