// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithBlueprintLibrary.h"

#include "DatasmithImportFactory.h"
#include "DatasmithImportOptions.h"
#include "DatasmithMeshHelper.h"
#include "DatasmithSceneActor.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithSceneSource.h"
#include "DatasmithStaticMeshImporter.h"
#include "ObjectElements/DatasmithUSceneElement.h"
#include "Translators/DatasmithTranslatableSource.h"
#include "Utility/DatasmithImporterUtils.h"

#include "Async/ParallelFor.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "StaticMeshAttributes.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "PackageTools.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "Subsystems/AssetEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "DatasmithBlueprintLibrary"

namespace DatasmithStaticMeshBlueprintLibraryUtil
{
	void EnsureLightmapUVsAreAvailable( UStaticMesh* StaticMesh )
	{
		if ( FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription( 0 ) )
		{
			const bool bAreLightmapUVsAvailable = DatasmithMeshHelper::HasUVData( *MeshDescription, StaticMesh->LightMapCoordinateIndex );

			if ( !bAreLightmapUVsAvailable )
			{
				FDatasmithStaticMeshImporter::PreBuildStaticMesh( StaticMesh );
			}
		}
	}

	float ParallelogramArea(FVector v0, FVector v1, FVector v2)
	{
		FVector TriangleNormal = (v1 - v0) ^ (v2 - v0);
		float Area = TriangleNormal.Size();

		return Area;
	}
}

namespace DatasmithBlueprintLibraryImpl
{
	static FName GetLoggerName() { return FName(TEXT("DatasmithLibrary")); }

	static FText GetDisplayName() { return NSLOCTEXT("DatasmithBlueprintLibrary", "LibraryName", "Datasmith Library"); }

	bool ValidatePackage(FString PackageName, UPackage*& OutPackage, const TCHAR* OutFailureReason)
	{
		OutFailureReason = TEXT("");
		OutPackage = nullptr;

		if (PackageName.IsEmpty())
		{
			OutFailureReason = TEXT("No destination Folder was provided.");
			return false;
		}

		PackageName.ReplaceCharInline(TEXT('\\'), TEXT('/'));
		while(PackageName.ReplaceInline(TEXT("//"), TEXT("/"), ESearchCase::CaseSensitive)) {}
		PackageName = UPackageTools::SanitizePackageName(PackageName);

		while (PackageName.EndsWith(TEXT("/")))
		{
			PackageName = PackageName.LeftChop(1);
		}

		FText Reason;
		if (!FPackageName::IsValidLongPackageName(PackageName, true, &Reason))
		{
			OutFailureReason = TEXT("Invalid long package mame.");
			return false;
		}

		OutPackage = CreatePackage( nullptr, *PackageName );
		return OutPackage != nullptr;
	}
}

UDatasmithSceneElement* UDatasmithSceneElement::ConstructDatasmithSceneFromFile(const FString& InFilename)
{
	using namespace DatasmithBlueprintLibraryImpl;
	FDatasmithSceneSource Source;
	Source.SetSourceFile(InFilename);

	UDatasmithSceneElement* DatasmithScene = NewObject<UDatasmithSceneElement>();

	DatasmithScene->SourcePtr.Reset(new FDatasmithTranslatableSceneSource(Source));
	FDatasmithTranslatableSceneSource& TranslatableSource = *DatasmithScene->SourcePtr;

	if (!TranslatableSource.IsTranslatable())
	{
		UE_LOG(LogDatasmithImport, Error, TEXT("Datasmith import error: no suitable translator found for this source. Abort import."));
		return nullptr;
	}

	TSharedRef< IDatasmithScene > Scene = FDatasmithSceneFactory::CreateScene(*Source.GetSceneName());
	DatasmithScene->SetDatasmithSceneElement(Scene);

	bool bLoadConfig = false; //!IsAutomatedImport();
	DatasmithScene->ImportContextPtr.Reset(new FDatasmithImportContext(Source.GetSourceFile(), bLoadConfig, GetLoggerName(), GetDisplayName(), TranslatableSource.GetTranslator()));

	if (!TranslatableSource.Translate(Scene))
	{
		UE_LOG(LogDatasmithImport, Error, TEXT("Datasmith import error: Scene translation failure. Abort import."));
		return nullptr;
	}

	return DatasmithScene;
}

FDatasmithImportFactoryCreateFileResult UDatasmithSceneElement::ImportScene(const FString& DestinationFolder)
{
	FDatasmithImportFactoryCreateFileResult Result;

	if (this == nullptr || !ImportContextPtr.IsValid() || !SourcePtr.IsValid() || SourcePtr->GetTranslator() == nullptr || !GetSceneElement().IsValid())
	{
		UE_LOG(LogDatasmithImport, Error, TEXT("Invalid State. Ensure ConstructDatasmithSceneFromFile has been called."));
		return Result;
	}

	UPackage* DestinationPackage;
	const TCHAR* OutFailureReason = TEXT("");
	if (!DatasmithBlueprintLibraryImpl::ValidatePackage(DestinationFolder, DestinationPackage, OutFailureReason))
	{
		UE_LOG(LogDatasmithImport, Error, TEXT("Invalid Destination '%s': %s"), *DestinationFolder, OutFailureReason);
		return Result;
	}

	FDatasmithImportContext& ImportContext = *ImportContextPtr;
	TSharedRef< IDatasmithScene > Scene = GetSceneElement().ToSharedRef();
	EObjectFlags NewObjectFlags = RF_Public | RF_Standalone | RF_Transactional;
	TSharedPtr<FJsonObject> ImportSettingsJson;
	bool bIsSilent = true;
	if ( !ImportContext.Init( Scene, DestinationPackage->GetName(), NewObjectFlags, GWarn, ImportSettingsJson, bIsSilent ) )
	{
		return Result;
	}

	bool bUserCancelled = false;
	Result.bImportSucceed = DatasmithImportFactoryImpl::ImportDatasmithScene(ImportContext, bUserCancelled);
	Result.bImportSucceed &= !bUserCancelled;

	if (Result.bImportSucceed)
	{
		Result.FillFromImportContext(ImportContext);
	}

	DestroyScene();

	return Result;
}

UObject* UDatasmithSceneElement::GetOptions(UClass* OptionType)
{
	if (OptionType == nullptr)
	{
		OptionType = UDatasmithImportOptions::StaticClass();
	}

	if (ImportContextPtr.IsValid())
	{
		// Standard options from Datasmith
		if (ImportContextPtr->Options.IsValid() && ImportContextPtr->Options->GetClass()->IsChildOf(OptionType))
		{
			return ImportContextPtr->Options.Get();
		}

		// Additional options from specific translators
		for(const TStrongObjectPtr<UObject>& AdditionalOption : ImportContextPtr->AdditionalImportOptions)
		{
			if (AdditionalOption->GetClass()->IsChildOf(OptionType))
			{
				return AdditionalOption.Get();
			}
		}
	}
	return nullptr;
}

TMap<UClass*, UObject*> UDatasmithSceneElement::GetAllOptions()
{
	TMap<UClass*, UObject*> M;

	auto Append = [&](UObject* Option)
	{
		if (Option)
	{
			M.Add(Option->GetClass(), Option);
	}
	};

	if (ImportContextPtr.IsValid())
	{
		// Standard options from Datasmith
		if (ImportContextPtr->Options.IsValid())
		{
			Append(ImportContextPtr->Options.Get());
		}

		// Additional options from specific translators
		for(const auto& AdditionalOption : ImportContextPtr->AdditionalImportOptions)
		{
			Append(AdditionalOption.Get());
		}
	}

	return M;
}

UDatasmithImportOptions* UDatasmithSceneElement::GetImportOptions()
{
	return Cast<UDatasmithImportOptions>(GetOptions());
}

void UDatasmithSceneElement::DestroyScene()
{
	ImportContextPtr.Reset();
	SourcePtr.Reset();
	Reset();
}

void UDatasmithStaticMeshBlueprintLibrary::ComputeLightmapResolution( const TArray< UObject* >& Objects, bool bApplyChanges, float IdealRatio )
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UDatasmithStaticMeshBlueprintLibrary::ComputeLightmapResolution)

	// Collect all the static meshes and static mesh components to compute lightmap resolution for
	TMap< UStaticMesh*, TSet< UStaticMeshComponent* > > WorkingSet;

	for ( UObject* Object : Objects )
	{
		if ( AActor* Actor = Cast< AActor >( Object ) )
		{
			TInlineComponentArray<UStaticMeshComponent*> StaticMeshComponents(Actor);
			for (UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
			{
				if(StaticMeshComponent && StaticMeshComponent->GetStaticMesh())
				{
					TSet<UStaticMeshComponent*>& Components = WorkingSet.FindOrAdd( StaticMeshComponent->GetStaticMesh() );
					Components.Add( StaticMeshComponent );
				}
			}
		}
		else if ( UStaticMesh* StaticMesh = Cast< UStaticMesh >( Object ) )
		{
			TSet<UStaticMeshComponent*>& Components = WorkingSet.FindOrAdd(StaticMesh);
			Components.Add( nullptr );
		}
	}

	// The actual work
	auto Compute = [&](UStaticMesh* StaticMesh, const TSet<UStaticMeshComponent*>& Components)
	{
		bool bComputeForComponents = true;

		// Compute light map resolution for static mesh asset if required
		if(Components.Contains(nullptr))
		{
			if( int32 LightMapResolution = ComputeLightmapResolution(StaticMesh, IdealRatio, FVector::OneVector) )
			{
				// Close the mesh editor to prevent crashing. If changes are applied, reopen it after the mesh has been built.
				bool bStaticMeshIsEdited = false;
				UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
				if (AssetEditorSubsystem && AssetEditorSubsystem->FindEditorForAsset(StaticMesh, false))
				{
					AssetEditorSubsystem->CloseAllEditorsForAsset(StaticMesh);
					bStaticMeshIsEdited = true;
				}

				if(bApplyChanges)
				{
					StaticMesh->Modify();
				}

				StaticMesh->LightMapResolution = LightMapResolution;

				if(bApplyChanges)
				{
					// Request re-building of mesh with new LODs
					StaticMesh->PostEditChange();

					// Reopen MeshEditor on this mesh if the MeshEditor was previously opened in it
					if (bStaticMeshIsEdited)
					{
						AssetEditorSubsystem->OpenEditorForAsset(StaticMesh);
					}
				}
			}
			else
			{
				bComputeForComponents = false;
			}
		}

		if(bComputeForComponents)
		{
			for (UStaticMeshComponent* StaticMeshComponent : Components)
			{
				if(StaticMeshComponent)
				{
					if( int32 LightMapResolution = ComputeLightmapResolution( StaticMesh, IdealRatio, StaticMeshComponent->GetComponentScale() ) )
					{
						StaticMeshComponent->bOverrideLightMapRes = true;
						StaticMeshComponent->OverriddenLightMapRes = LightMapResolution;
					}
				}
			}
		}
	};

	// If no need to notify changes, multi-thread the computing
	if(!bApplyChanges)
	{
		TArray< UStaticMesh* > WorkingSetKeys;
		WorkingSet.GenerateKeyArray( WorkingSetKeys );

		// Start with the biggest mesh first to help balancing tasks on threads
		Algo::SortBy(
			WorkingSetKeys,
			[](const UStaticMesh* Mesh){ return Mesh->IsMeshDescriptionValid(0) ? Mesh->GetMeshDescription(0)->Vertices().Num() : 0; },
			TGreater<>()
		);

		ParallelFor( WorkingSetKeys.Num(),
			[&]( int32 Index )
			{
				Compute(  WorkingSetKeys[Index], WorkingSet[WorkingSetKeys[Index]] );
			},
			EParallelForFlags::Unbalanced
		);
	}
	// Do not take any chance, compute sequentially
	else
	{
		for(TPair< UStaticMesh*, TSet< UStaticMeshComponent* > >& Entry : WorkingSet)
		{
			Compute(  Entry.Key, Entry.Value );
		}
	}
}

int32 UDatasmithStaticMeshBlueprintLibrary::ComputeLightmapResolution(UStaticMesh* StaticMesh, float IdealRatio, const FVector& StaticMeshScale)
{
	if(StaticMesh == nullptr)
	{
		return 0;
	}

	DatasmithStaticMeshBlueprintLibraryUtil::EnsureLightmapUVsAreAvailable( StaticMesh );

	if ( const FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription( 0 ) )
	{
		FStaticMeshConstAttributes Attributes(*MeshDescription);

		// Compute the mesh UV density, based on FStaticMeshRenderData::ComputeUVDensities, except that we're working on a MeshDescription
		const TVertexAttributesConstRef< FVector > VertexPositions = Attributes.GetVertexPositions();
		const TVertexInstanceAttributesConstRef< FVector2D > VertexUVs = Attributes.GetVertexInstanceUVs();

		if ( VertexUVs.GetNumElements() <= StaticMesh->LightMapCoordinateIndex )
		{
			return 0;
		}

		float MeshArea = 0.f;
		float MeshUVArea = 0.f;

		TArray< FVector2D > PolygonAreas;

		for ( const FPolygonID PolygonID : MeshDescription->Polygons().GetElementIDs() )
		{
			float PolygonArea = 0.f;
			float PolygonUVArea = 0.f;

			const TArray< FTriangleID >& PolygonTriangleIDs = MeshDescription->GetPolygonTriangleIDs( PolygonID );
			for ( const FTriangleID TriangleID : PolygonTriangleIDs )
			{
				FVector VertexPosition[3];
				FVector2D LightmapUVs[3];

				for ( int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex )
				{
					const FVertexInstanceID VertexInstanceID = MeshDescription->GetTriangleVertexInstance( TriangleID, CornerIndex );
					VertexPosition[ CornerIndex ] = VertexPositions[ MeshDescription->GetVertexInstanceVertex( VertexInstanceID ) ] * StaticMeshScale;

					LightmapUVs[ CornerIndex ] = VertexUVs.Get( VertexInstanceID, StaticMesh->LightMapCoordinateIndex );
				}

				PolygonArea += DatasmithStaticMeshBlueprintLibraryUtil::ParallelogramArea( VertexPosition[0],  VertexPosition[1],  VertexPosition[2] );
				PolygonUVArea += DatasmithStaticMeshBlueprintLibraryUtil::ParallelogramArea( FVector( LightmapUVs[0], 0.f ),  FVector( LightmapUVs[1], 0.f ),  FVector( LightmapUVs[2], 0.f ) );
			}

			PolygonAreas.Emplace( FMath::Sqrt( PolygonArea ), FMath::Sqrt(PolygonArea / PolygonUVArea ) );
		}

		Algo::Sort( PolygonAreas, []( const FVector2D& ElementA, const FVector2D& ElementB )
		{
			return ElementA[1] < ElementB[1];
		} );

		float WeightedUVDensity = 0.f;
		float Weight = 0.f;

		// Remove 10% of higher and lower texel factors.
		const int32 Threshold = FMath::FloorToInt( 0.1f * (float)PolygonAreas.Num() );
		for (int32 Index = Threshold; Index < PolygonAreas.Num() - Threshold; ++Index)
		{
			WeightedUVDensity += PolygonAreas[ Index ][ 1 ] * PolygonAreas[ Index ][ 0 ];
			Weight += PolygonAreas[ Index ][ 0 ];
		}

		float UVDensity = WeightedUVDensity / Weight;

		int32 LightmapResolution = FMath::FloorToInt( UVDensity * IdealRatio );

		// Ensure that LightmapResolution is a factor of 4
		return FMath::Max( LightmapResolution + 3 & ~3, 4 );
	}

	return 0;
}

FDatasmithImportFactoryCreateFileResult::FDatasmithImportFactoryCreateFileResult()
	: ImportedBlueprint(nullptr)
	, bImportSucceed(false)
{}

void FDatasmithImportFactoryCreateFileResult::FillFromImportContext(const FDatasmithImportContext& ImportContext)
{
	switch (ImportContext.Options->HierarchyHandling)
	{
	case EDatasmithImportHierarchy::UseMultipleActors:
		ImportedActors.Append(ImportContext.GetImportedActors());
		break;
	case EDatasmithImportHierarchy::UseSingleActor:
		ImportedActors.Append(ImportContext.ActorsContext.FinalSceneActors.Array());
		break;
	case EDatasmithImportHierarchy::UseOneBlueprint:
		ImportedBlueprint = ImportContext.RootBlueprint;
		break;
	default:
		check(false);
		break;
	}

	ImportedMeshes.Reserve(ImportContext.ImportedStaticMeshes.Num());
	for ( const TPair< TSharedRef< IDatasmithMeshElement >, UStaticMesh* >& MeshPair : ImportContext.ImportedStaticMeshes )
	{
		ImportedMeshes.Add( MeshPair.Value );
	}
}

#undef LOCTEXT_NAMESPACE
