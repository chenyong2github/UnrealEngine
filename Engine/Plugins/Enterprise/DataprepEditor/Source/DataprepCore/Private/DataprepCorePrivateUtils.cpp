// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataprepCorePrivateUtils.h"

#include "DataprepAsset.h"
#include "DataprepCoreUtils.h"
#include "IDataprepProgressReporter.h"

#include "ActorEditorUtils.h"
#include "AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "IMessageLogListing.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "MeshDescription.h"
#include "MessageLogModule.h"
#include "Modules/ModuleManager.h"
#include "StaticMeshAttributes.h"

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
	static FString RootPackagePath( TEXT("/Engine/DataprepCore/Transient") );
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

void DataprepCorePrivateUtils::BuildStaticMeshes(TSet<UStaticMesh*>& StaticMeshes, TFunction<bool(UStaticMesh*)> ProgressFunction, bool bForceBuild)
{
	TArray<UStaticMesh*> BuiltMeshes;
	BuiltMeshes.Reserve( StaticMeshes.Num() );

	if(bForceBuild)
	{
		BuiltMeshes.Append( StaticMeshes.Array() );
	}
	else
	{
		for(UStaticMesh* StaticMesh : StaticMeshes)
		{
			if(StaticMesh && (!StaticMesh->RenderData.IsValid() || !StaticMesh->RenderData->IsInitialized()))
			{
				BuiltMeshes.Add( StaticMesh );
			}
		}
	}

	if(BuiltMeshes.Num() > 0)
	{
		// Start with the biggest mesh first to help balancing tasks on threads
		BuiltMeshes.Sort(
			[](const UStaticMesh& Lhs, const UStaticMesh& Rhs) 
		{ 
			int32 LhsVerticesNum = Lhs.IsMeshDescriptionValid(0) ? Lhs.GetMeshDescription(0)->Vertices().Num() : 0;
			int32 RhsVerticesNum = Rhs.IsMeshDescriptionValid(0) ? Rhs.GetMeshDescription(0)->Vertices().Num() : 0;

			return LhsVerticesNum > RhsVerticesNum;
		}
		);

		TArray< TArray<FMeshBuildSettings> > StaticMeshesSettings;
		StaticMeshesSettings.Reserve( BuiltMeshes.Num() );

		//Cache the BuildSettings and update them before building the meshes.
		for (UStaticMesh* StaticMesh : BuiltMeshes)
		{
			TArray<FStaticMeshSourceModel>& SourceModels = StaticMesh->GetSourceModels();
			TArray<FMeshBuildSettings> BuildSettings;
			BuildSettings.Reserve(SourceModels.Num());

			for(int32 Index = 0; Index < SourceModels.Num(); ++Index)
			{
				FStaticMeshSourceModel& SourceModel = SourceModels[Index];

				BuildSettings.Add( SourceModel.BuildSettings );

				if(FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription(Index))
				{
					FStaticMeshAttributes Attributes(*MeshDescription);
					if(SourceModel.BuildSettings.DstLightmapIndex != -1)
					{
						TVertexInstanceAttributesConstRef<FVector2D> VertexInstanceUVs = Attributes.GetVertexInstanceUVs();
						SourceModel.BuildSettings.bGenerateLightmapUVs = VertexInstanceUVs.IsValid() && VertexInstanceUVs.GetNumIndices() > SourceModel.BuildSettings.DstLightmapIndex;
					}
					else
					{
						SourceModel.BuildSettings.bGenerateLightmapUVs = false;
					}

					SourceModel.BuildSettings.bRecomputeNormals = !(Attributes.GetVertexInstanceNormals().IsValid() && Attributes.GetVertexInstanceNormals().GetNumIndices() > 0);
					SourceModel.BuildSettings.bRecomputeTangents = false;
					//SourceModel.BuildSettings.bBuildAdjacencyBuffer = false;
					//SourceModel.BuildSettings.bBuildReversedIndexBuffer = false;
				}
			}

			StaticMeshesSettings.Add(MoveTemp(BuildSettings));				
		}

		// Disable warnings from LogStaticMesh. Not useful
		ELogVerbosity::Type PrevLogStaticMeshVerbosity = LogStaticMesh.GetVerbosity();
		LogStaticMesh.SetVerbosity( ELogVerbosity::Error );

		UStaticMesh::BatchBuild(BuiltMeshes, true, ProgressFunction);

		// Restore LogStaticMesh verbosity
		LogStaticMesh.SetVerbosity( PrevLogStaticMeshVerbosity );

		for(int32 Index = 0; Index < BuiltMeshes.Num(); ++Index)
		{
			UStaticMesh* StaticMesh = BuiltMeshes[Index];
			TArray<FMeshBuildSettings>& PrevBuildSettings = StaticMeshesSettings[Index];

			TArray<FStaticMeshSourceModel>& SourceModels = StaticMesh->GetSourceModels();

			for(int32 SourceModelIndex = 0; SourceModelIndex < SourceModels.Num(); ++SourceModelIndex)
			{
				SourceModels[SourceModelIndex].BuildSettings = PrevBuildSettings[SourceModelIndex];
			}

			if(FStaticMeshRenderData* RenderData = StaticMesh->RenderData.Get())
			{
				for ( FStaticMeshLODResources& LODResources : RenderData->LODResources )
				{
					LODResources.bHasColorVertexData = true;
				}
			}
		}
	}
}

void DataprepCorePrivateUtils::ClearAssets(const TArray<TWeakObjectPtr<UObject>>& Assets)
{
	for(const TWeakObjectPtr<UObject>& ObjectPtr : Assets)
	{
		if(UStaticMesh* StaticMesh = Cast<UStaticMesh>(ObjectPtr.Get()))
		{
			StaticMesh->PreEditChange( nullptr );
			StaticMesh->RenderData.Reset();
		}
	}
}

#undef LOCTEXT_NAMESPACE