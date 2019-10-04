// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataPrepContentProducer.h"

#include "DataprepCoreUtils.h"

#include "AssetToolsModule.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "LevelSequence.h"

const FString DefaultNamespace( TEXT("void") );

FString UDataprepContentProducer::GetNamespace() const
{
	return DefaultNamespace;
}

bool UDataprepContentProducer::Produce(const FDataprepProducerContext& InContext, TArray< TWeakObjectPtr< UObject > >& OutAssets)
{
	Context = InContext;

	if( !IsValid() || !Initialize() )
	{
		Terminate();
		return false;
	}

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
	int32 LastAssetCount = OutAssets.Num();

	// Prefix all newly created actors with the namespace of the producer
	// #ueent_todo: find a better way to identify newly created actors
	if(!Execute( OutAssets ))
	{
		Terminate();
		return false;
	}

	// Collect all packages containing LevelSequence assets to remap their reference to an newly created actor
	TSet< UPackage* > LevelSequencePackagesToCheck;
	for(int32 Index = LastAssetCount; Index < OutAssets.Num(); ++Index)
	{
		if( ULevelSequence* LevelSequence = Cast<ULevelSequence>( OutAssets[Index].Get() ) )
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
			FDataprepCoreUtils::RenameObject( Actor, *ActorName );

			ActorRedirectorMap.Emplace( PreviousActorSoftPath, Actor );
		}
	}

	// Update reference of LevelSequence assets if necessary
	if(LevelSequencePackagesToCheck.Num() > 0)
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		AssetTools.RenameReferencingSoftObjectPaths( LevelSequencePackagesToCheck.Array(), ActorRedirectorMap );
	}

	Terminate();

	return true;
}