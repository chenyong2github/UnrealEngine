// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataPrepContentProducer.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

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

	// Prefix all newly created actors with the namespace of the producer
	// #ueent_todo: find a better way to identify newly created actors
	if( Execute() )
	{
		const FString Namespace = GetNamespace();

		for (TActorIterator<AActor> It(Context.WorldPtr.Get(), AActor::StaticClass(), Flags); It; ++It)
		{
			if(*It != nullptr && ExistingActors.Find( *It ) == nullptr)
			{
				AActor* Actor = *It;

				const FString ActorName =  Namespace + TEXT("_") + Actor->GetName();
				Actor->Rename( *ActorName, nullptr );
			}
		}

		return true;
	}

	return false;
}