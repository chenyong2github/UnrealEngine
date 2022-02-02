// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDExporterBlueprintLibrary.h"

#include "AnalyticsBlueprintLibrary.h"
#include "AnalyticsEventAttribute.h"
#include "CoreMinimal.h"
#include "Editor.h"
#include "InstancedFoliageActor.h"
#include "UObject/ObjectMacros.h"
#include "USDClassesModule.h"

AInstancedFoliageActor* UUsdExporterBlueprintLibrary::GetInstancedFoliageActorForLevel( bool bCreateIfNone /*= false */, ULevel* Level /*= nullptr */ )
{
	if ( !Level )
	{
		const bool bEnsureIsGWorld = false;
		UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext( bEnsureIsGWorld ).World() : nullptr;
		if ( !EditorWorld )
		{
			return nullptr;
		}

		Level = EditorWorld->GetCurrentLevel();
		if ( !Level )
		{
			return nullptr;
		}
	}

	return AInstancedFoliageActor::GetInstancedFoliageActorForLevel( Level, bCreateIfNone );
}

TArray<UFoliageType*> UUsdExporterBlueprintLibrary::GetUsedFoliageTypes( AInstancedFoliageActor* Actor )
{
	TArray<UFoliageType*> Result;
	if ( !Actor )
	{
		return Result;
	}

	for ( const TPair<UFoliageType*, TUniqueObj<FFoliageInfo>>& FoliagePair : Actor->GetFoliageInfos())
	{
		Result.Add( FoliagePair.Key );
	}

	return Result;
}

UObject* UUsdExporterBlueprintLibrary::GetSource( UFoliageType* FoliageType )
{
	if ( FoliageType )
	{
		return FoliageType->GetSource();
	}

	return nullptr;
}

TArray<FTransform> UUsdExporterBlueprintLibrary::GetInstanceTransforms( AInstancedFoliageActor* Actor, UFoliageType* FoliageType, ULevel* InstancesLevel )
{
	TArray<FTransform> Result;
	if ( !Actor || !FoliageType )
	{
		return Result;
	}

	// Modified from AInstancedFoliageActor::GetInstancesForComponent to limit traversal only to our FoliageType

	if ( const TUniqueObj<FFoliageInfo>* FoundInfo = Actor->GetFoliageInfos().Find( FoliageType ) )
	{
		const FFoliageInfo& Info = (*FoundInfo).Get();

		// Collect IDs of components that are on the same level as the actor's level. This because later on we'll have level-by-level
		// export, and we'd want one point instancer per level
		for ( const TPair<FFoliageInstanceBaseId, FFoliageInstanceBaseInfo>& FoliageInstancePair : Actor->InstanceBaseCache.InstanceBaseMap )
		{
			UActorComponent* Comp = FoliageInstancePair.Value.BasePtr.Get();
			if ( !Comp || ( InstancesLevel && ( Comp->GetComponentLevel() != InstancesLevel ) ) )
			{
				continue;
			}

			if ( const auto* InstanceSet = Info.ComponentHash.Find( FoliageInstancePair.Key ) )
			{
				Result.Reserve( Result.Num() + InstanceSet->Num() );
				for ( int32 InstanceIndex : *InstanceSet )
				{
					const FFoliageInstancePlacementInfo* Instance = &Info.Instances[ InstanceIndex ];
					Result.Emplace( FQuat(Instance->Rotation), Instance->Location, (FVector)Instance->DrawScale3D );
				}
			}
		}
	}

	return Result;
}

void UUsdExporterBlueprintLibrary::SendAnalytics( const TArray<FAnalyticsEventAttr>& Attrs, const FString& EventName, bool bAutomated, double ElapsedSeconds, double NumberOfFrames, const FString& Extension )
{
	TArray<FAnalyticsEventAttribute> Converted;
	Converted.Reserve( Attrs.Num() );
	for ( const FAnalyticsEventAttr& Attr : Attrs )
	{
		Converted.Emplace( Attr.Name, Attr.Value );
	}

	IUsdClassesModule::SendAnalytics( MoveTemp( Converted ), EventName, bAutomated, ElapsedSeconds, NumberOfFrames, Extension );
}

