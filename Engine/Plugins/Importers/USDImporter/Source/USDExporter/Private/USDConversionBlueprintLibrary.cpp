// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDConversionBlueprintLibrary.h"

#include "UnrealUSDWrapper.h"
#include "USDConversionUtils.h"
#include "USDLayerUtils.h"
#include "USDLog.h"

#include "UsdWrappers/SdfLayer.h"
#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/UsdStage.h"

#include "Engine/LevelStreaming.h"
#include "Engine/World.h"

TSet<AActor*> UUsdConversionBlueprintLibrary::GetActorsToConvert( UWorld* World )
{
	TSet<AActor*> Result;
	if ( !World )
	{
		return Result;
	}

	const bool bForce = true;
	World->LoadSecondaryLevels(bForce);

	auto CollectActors = [ &Result ]( ULevel* Level )
	{
		if ( !Level )
		{
			return;
		}

		Result.Append( Level->Actors );
	};

	CollectActors( World->PersistentLevel );

	for ( ULevelStreaming* StreamingLevel : World->GetStreamingLevels() )
	{
		if ( StreamingLevel )
		{
			if ( ULevel* Level = StreamingLevel->GetLoadedLevel() )
			{
				CollectActors( Level );
			}
		}
	}

	Result.Remove( nullptr );
	return Result;
}

FString UUsdConversionBlueprintLibrary::MakePathRelativeToLayer( const FString& AnchorLayerPath, const FString& PathToMakeRelative )
{
#if USE_USD_SDK
	if ( UE::FSdfLayer Layer = UE::FSdfLayer::FindOrOpen( *AnchorLayerPath ) )
	{
		FString Path = PathToMakeRelative;
		UsdUtils::MakePathRelativeToLayer( Layer, Path );
		return Path;
	}
	else
	{
		UE_LOG(LogUsd, Error, TEXT("Failed to find a layer with path '%s' to make the path '%s' relative to"), *AnchorLayerPath, *PathToMakeRelative );
		return PathToMakeRelative;
	}
#else
	return FString();
#endif // USE_USD_SDK
}

void UUsdConversionBlueprintLibrary::InsertSubLayer( const FString& ParentLayerPath, const FString& SubLayerPath, int32 Index /*= -1 */ )
{
#if USE_USD_SDK
	if ( ParentLayerPath.IsEmpty() || SubLayerPath.IsEmpty() )
	{
		return;
	}

	if ( UE::FSdfLayer Layer = UE::FSdfLayer::FindOrOpen( *ParentLayerPath ) )
	{
		UsdUtils::InsertSubLayer( Layer, *SubLayerPath, Index );
	}
	else
	{
		UE_LOG( LogUsd, Error, TEXT( "Failed to find a parent layer '%s' when trying to insert sublayer '%s'" ), *ParentLayerPath, *SubLayerPath );
	}
#endif // USE_USD_SDK
}

void UUsdConversionBlueprintLibrary::AddPayload( const FString& ReferencingStagePath, const FString& ReferencingPrimPath, const FString& TargetStagePath )
{
#if USE_USD_SDK
	TArray<UE::FUsdStage> PreviouslyOpenedStages = UnrealUSDWrapper::GetAllStagesFromCache();

	// Open using the stage cache as it's very likely this stage is already in there anyway
	UE::FUsdStage ReferencingStage = UnrealUSDWrapper::OpenStage( *ReferencingStagePath, EUsdInitialLoadSet::LoadAll );
	if ( ReferencingStage )
	{
		if ( UE::FUsdPrim ReferencingPrim = ReferencingStage.GetPrimAtPath( UE::FSdfPath( *ReferencingPrimPath ) ) )
		{
			UsdUtils::AddPayload( ReferencingPrim, *TargetStagePath );
		}
	}

	// Cleanup or else the stage cache will keep these stages open forever
	if ( !PreviouslyOpenedStages.Contains( ReferencingStage ) )
	{
		UnrealUSDWrapper::EraseStageFromCache( ReferencingStage );
	}
#endif // USE_USD_SDK
}
