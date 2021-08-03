// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDExporterBlueprintLibrary.h"

#include "CoreMinimal.h"
#include "Editor.h"
#include "InstancedFoliageActor.h"
#include "UObject/ObjectMacros.h"

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

	for ( const TPair<UFoliageType*, TUniqueObj<FFoliageInfo>>& FoliagePair : Actor->FoliageInfos )
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

	if ( InstancesLevel == nullptr )
	{
		InstancesLevel = Actor->GetLevel();
	}

	// Modified from AInstancedFoliageActor::GetInstancesForComponent to limit traversal only to our FoliageType

	if ( TUniqueObj<FFoliageInfo>* FoundInfo = Actor->FoliageInfos.Find( FoliageType ) )
	{
		const FFoliageInfo& Info = (*FoundInfo).Get();

		// Collect IDs of components that are on the same level as the actor's level. This because later on we'll have level-by-level
		// export, and we'd want one point instancer per level
		for ( const TPair<FFoliageInstanceBaseId, FFoliageInstanceBaseInfo>& FoliageInstancePair : Actor->InstanceBaseCache.InstanceBaseMap )
		{
			UActorComponent* Comp = FoliageInstancePair.Value.BasePtr.Get();
			if ( !Comp || Comp->GetComponentLevel() != InstancesLevel )
			{
				continue;
			}

			if ( const auto* InstanceSet = Info.ComponentHash.Find( FoliageInstancePair.Key ) )
			{
				Result.Reserve( Result.Num() + InstanceSet->Num() );
				for ( int32 InstanceIndex : *InstanceSet )
				{
					const FFoliageInstancePlacementInfo* Instance = &Info.Instances[ InstanceIndex ];
					Result.Emplace( Instance->Rotation, Instance->Location, Instance->DrawScale3D );
				}
			}
		}
	}

	return Result;
}

