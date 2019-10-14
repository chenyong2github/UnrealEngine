// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataprepCorePrivateUtils.h"

#include "DataPrepAsset.h"
#include "DataprepCoreUtils.h"

#include "ActorEditorUtils.h"
#include "AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "IMessageLogListing.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "MessageLogModule.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "DataprepAsset"

void DataprepCorePrivateUtils::DeleteRegisteredAsset(UObject* Asset)
{
	if(Asset != nullptr)
	{
		FDataprepCoreUtils::MoveToTransientPackage( Asset );

		Asset->ClearFlags(RF_Standalone | RF_Public);
		Asset->RemoveFromRoot();
		Asset->MarkPendingKill();

		FAssetRegistryModule::AssetDeleted( Asset ) ;
	}
}

void DataprepCorePrivateUtils::GetActorsFromWorld(const UWorld* World, TArray<AActor*>& OutActors )
{
	if(World != nullptr)
	{
		int32 ActorsCount = 0;
		for(ULevel* Level : World->GetLevels())
		{
			ActorsCount += Level->Actors.Num();
		}

		OutActors.Reserve( OutActors.Num() + ActorsCount );

		for(ULevel* Level : World->GetLevels())
		{
			for( AActor* Actor : Level->Actors )
			{
				const bool bIsValidActor = Actor &&
					!Actor->IsPendingKill() &&
					Actor->IsEditable() &&
					!Actor->IsTemplate() &&
					!FActorEditorUtils::IsABuilderBrush(Actor) &&
					!Actor->IsA(AWorldSettings::StaticClass());

				if( bIsValidActor )
				{
					OutActors.Add( Actor  );
				}
			}
		}
	}
}


const FString& DataprepCorePrivateUtils::GetRootTemporaryDir()
{
	static FString RootTemporaryDir = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("DataprepTemp") );
	return RootTemporaryDir;
}

const FString& DataprepCorePrivateUtils::GetRootPackagePath()
{
	static FString RootPackagePath( TEXT("/DataprepCore/Transient") );
	return RootPackagePath;
}

void DataprepCorePrivateUtils::LogMessage( EMessageSeverity::Type Severity, const FText& Message, const FText& NotificationText )
{
	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");

	TSharedPtr<IMessageLogListing> LogListing = MessageLogModule.GetLogListing( TEXT("DataprepCore") );
	LogListing->SetLabel( LOCTEXT("MessageLogger", "Dataprep Core") );

	LogListing->AddMessage( FTokenizedMessage::Create( Severity, Message ), /*bMirrorToOutputLog*/ true );

	if( !NotificationText.IsEmpty() )
	{
		LogListing->NotifyIfAnyMessages( NotificationText, EMessageSeverity::Info);
	}
}

#undef LOCTEXT_NAMESPACE