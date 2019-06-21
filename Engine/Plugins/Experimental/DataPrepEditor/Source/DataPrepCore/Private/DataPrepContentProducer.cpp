// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataPrepContentProducer.h"

#include "AssetToolsModule.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "LevelSequence.h"

const FString DefaultNamespace( TEXT("void") );

bool UDataprepContentProducer::Initialize( const ProducerContext& InContext, FString& OutReason )
{
	Context = InContext;

	return IsValid();
}

void UDataprepContentProducer::Reset()
{
	// Release hold onto all context's objects
	Context.WorldPtr.Reset();
	Context.RootPackagePtr.Reset();
	Context.ProgressReporterPtr.Reset();
	Context.LoggerPtr.Reset();
	Assets.Empty();
}

FString UDataprepContentProducer::GetNamespace() const
{
	return DefaultNamespace;
}

bool UDataprepContentProducer::Produce()
{
	TSet< AActor* > ExistingActors;
	ExistingActors.Reserve( Context.WorldPtr->GetCurrentLevel()->Actors.Num() );

	// Cache all actors in the world before the producer is run
	const EActorIteratorFlags Flags = EActorIteratorFlags::SkipPendingKill;
	for (TActorIterator<AActor> It(Context.WorldPtr.Get(), AActor::StaticClass(), Flags); It; ++It)
	{
		if(*It != nullptr)
		{
			ExistingActors.Add( *It );
		}
	}

	// Cache number of assets to go through new assets after call to Execute
	int32 LastAssetCount = Assets.Num();

	// Prefix all newly created actors with the namespace of the producer
	// #ueent_todo: find a better way to identify newly created actors
	if( Execute() )
	{
		// Collect all packages containing LevelSequence assets to remap their reference to an newly created actor
		TSet< UPackage* > LevelSequencePackagesToCheck;
		for(int32 Index = LastAssetCount; Index < Assets.Num(); ++Index)
		{
			if( ULevelSequence* LevelSequence = Cast<ULevelSequence>( Assets[Index].Get() ) )
			{
				LevelSequencePackagesToCheck.Add( LevelSequence->GetOutermost() );
			}
		}

		// Map between old path and new path of newly created actors
		TMap< FSoftObjectPath, FSoftObjectPath > ActorRedirectorMap;

		// Rename actors
		const FString Namespace = GetNamespace();

		for (TActorIterator<AActor> It(Context.WorldPtr.Get(), AActor::StaticClass(), Flags); It; ++It)
		{
			if(*It != nullptr && ExistingActors.Find( *It ) == nullptr)
			{
				AActor* Actor = *It;

				FSoftObjectPath PreviousActorSoftPath(Actor);

				const FString ActorName =  Namespace + TEXT("_") + Actor->GetName();
				Actor->Rename( *ActorName, nullptr );

				ActorRedirectorMap.Emplace( PreviousActorSoftPath, Actor );
			}
		}

		// Update reference of LevelSequence assets if necessary
		if(LevelSequencePackagesToCheck.Num() > 0)
		{
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
			AssetTools.RenameReferencingSoftObjectPaths( LevelSequencePackagesToCheck.Array(), ActorRedirectorMap );
		}

		return true;
	}

	return false;
}