// Copyright Epic Games, Inc. All Rights Reserved.

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
#include "MeshExport.h"
#include "Misc/PackageName.h"
#include "PackageTools.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "Subsystems/AssetEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "DatasmithBlueprintLibrary"

DEFINE_LOG_CATEGORY_STATIC(LogSetupStaticLighting, Log, All);

namespace DatasmithStaticMeshBlueprintLibraryUtil
{
	void EnsureLightmapSourceUVsAreAvailable( UStaticMesh* StaticMesh )
	{
		if ( StaticMesh->GetNumSourceModels() > 0 && StaticMesh->GetSourceModel(0).BuildSettings.bGenerateLightmapUVs )
		{
			FDatasmithStaticMeshImporter::PreBuildStaticMesh( StaticMesh );			
		}
	}

	float ParallelogramArea(FVector v0, FVector v1, FVector v2)
	{
		FVector TriangleNormal = (v1 - v0) ^ (v2 - v0);
		float Area = TriangleNormal.Size();

		return Area;
	}

	//Used for creating a mapping of StaticMeshes and the StaticMeshComponents that references them in the given list.
	TMap< UStaticMesh*, TSet< UStaticMeshComponent* > > GetStaticMeshComponentMap(const TArray< UObject* >& Objects)
	{
		TMap< UStaticMesh*, TSet< UStaticMeshComponent* > > StaticMeshMap;

		for (UObject* Object : Objects)
		{
			if (AActor* Actor = Cast< AActor >(Object))
			{
				TInlineComponentArray<UStaticMeshComponent*> StaticMeshComponents(Actor);
				for (UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
				{
					if (StaticMeshComponent && StaticMeshComponent->GetStaticMesh())
					{
						TSet<UStaticMeshComponent*>& Components = StaticMeshMap.FindOrAdd(StaticMeshComponent->GetStaticMesh());
						Components.Add(StaticMeshComponent);
					}
				}
			}
			else if (UStaticMeshComponent* StaticMeshComponent = Cast< UStaticMeshComponent >(Object))
			{
				if (UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh())
				{
					TSet<UStaticMeshComponent*>& Components = StaticMeshMap.FindOrAdd(StaticMesh);
					Components.Add(StaticMeshComponent);
				}
			}
			else if (UStaticMesh* StaticMesh = Cast< UStaticMesh >(Object))
			{
				TSet<UStaticMeshComponent*>& Components = StaticMeshMap.FindOrAdd(StaticMesh);
				Components.Add(nullptr);
			}
		}

		return StaticMeshMap;
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

void UDatasmithStaticMeshBlueprintLibrary::SetupStaticLighting(const TArray< UObject* >& Objects, bool bApplyChanges, bool bGenerateLightmapUVs, float LightmapResolutionIdealRatio)
{
	// Collect all the static meshes and static mesh components to compute lightmap resolution for
	TMap< UStaticMesh*, TSet< UStaticMeshComponent* > > StaticMeshMap(DatasmithStaticMeshBlueprintLibraryUtil::GetStaticMeshComponentMap(Objects));

	for (const auto& StaticMeshPair : StaticMeshMap)
	{
		UStaticMesh* StaticMesh = StaticMeshPair.Key;

		if (bApplyChanges)
		{
			StaticMesh->Modify();
		}

		for (int32 LODIndex = 0; LODIndex < StaticMesh->GetNumSourceModels(); ++LODIndex)
		{
			FStaticMeshSourceModel& SourceModel = StaticMesh->GetSourceModel(LODIndex);
			const bool bDidChangeSettings = SourceModel.BuildSettings.bGenerateLightmapUVs != bGenerateLightmapUVs;
			SourceModel.BuildSettings.bGenerateLightmapUVs = bGenerateLightmapUVs;

			if (LODIndex == 0)
			{
				int32 MaxBiggestUVChannel = Lightmass::MAX_TEXCOORDS;
				
				if (const FMeshDescription* MeshDescription = SourceModel.MeshDescription.Get())
				{
					FStaticMeshConstAttributes Attributes(*MeshDescription);
				
					// 3 is the maximum that lightmass accept. Defined in MeshExport.h : MAX_TEXCOORDS .
					MaxBiggestUVChannel = FMath::Min(MaxBiggestUVChannel, Attributes.GetVertexInstanceUVs().GetNumIndices() - 1);
				}

				if (bGenerateLightmapUVs)
				{
					const int32 GeneratedLightmapChannel = SourceModel.BuildSettings.DstLightmapIndex;
					
					if (GeneratedLightmapChannel < Lightmass::MAX_TEXCOORDS)
					{
						StaticMesh->LightMapCoordinateIndex = GeneratedLightmapChannel;
					}
					else
					{
						UE_LOG(LogSetupStaticLighting, Warning, TEXT("Could not complete the static lighting setup for static mesh %s as the generated lightmap UV is set to be in channel #%i while the maximum lightmap channel is %i"), *StaticMesh->GetName(), GeneratedLightmapChannel, Lightmass::MAX_TEXCOORDS);
						break;
					}
				}
				else if (StaticMesh->LightMapCoordinateIndex > MaxBiggestUVChannel && bDidChangeSettings)
				{
					// If we are not generating the lightmap anymore make sure we are selecting a valid lightmap index.
					StaticMesh->LightMapCoordinateIndex = MaxBiggestUVChannel;
				}
			}
		}
	}

	// Compute the lightmap resolution, do not apply the changes so that the computation is done on multiple threads
	// We'll directly call PostEditChange() at the end of the function so that we also get the StaticLightingSetup changes.
	ComputeLightmapResolution(StaticMeshMap, /* bApplyChange */false, LightmapResolutionIdealRatio);

	for (const auto& StaticMeshPair : StaticMeshMap)
	{
		StaticMeshPair.Key->PostEditChange();
	}
}

void UDatasmithStaticMeshBlueprintLibrary::ComputeLightmapResolution(const TArray< UObject* >& Objects, bool bApplyChanges, float IdealRatio)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UDatasmithStaticMeshBlueprintLibrary::ComputeLightmapResolution)

	// Collect all the static meshes and static mesh components to compute lightmap resolution for
	TMap< UStaticMesh*, TSet< UStaticMeshComponent* > > StaticMeshMap(DatasmithStaticMeshBlueprintLibraryUtil::GetStaticMeshComponentMap(Objects));

	ComputeLightmapResolution(StaticMeshMap, bApplyChanges, IdealRatio);
}

void UDatasmithStaticMeshBlueprintLibrary::ComputeLightmapResolution(const TMap< UStaticMesh*, TSet< UStaticMeshComponent* > >& StaticMeshMap, bool bApplyChanges, float IdealRatio)
{
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
		TArray< UStaticMesh* > StaticMeshes;
		StaticMeshMap.GenerateKeyArray( StaticMeshes );

		// Start with the biggest mesh first to help balancing tasks on threads
		Algo::SortBy(
			StaticMeshes,
			[](const UStaticMesh* Mesh){ return Mesh->IsMeshDescriptionValid(0) ? Mesh->GetMeshDescription(0)->Vertices().Num() : 0; },
			TGreater<>()
		);

		ParallelFor(StaticMeshes.Num(),
			[&](int32 Index)
			{
				// We need to ensure the source UVs for generated lightmaps are available before generating then in the UStaticMesh::BatchBuild().
				DatasmithStaticMeshBlueprintLibraryUtil::EnsureLightmapSourceUVsAreAvailable(StaticMeshes[Index]);
			},
			EParallelForFlags::Unbalanced
		);

		UStaticMesh::BatchBuild( StaticMeshes, true);

		ParallelFor( StaticMeshes.Num(),
			[&]( int32 Index )
			{
				Compute( StaticMeshes[Index], StaticMeshMap[StaticMeshes[Index]] );
			},
			EParallelForFlags::Unbalanced
		);
	}
	// Do not take any chance, compute sequentially
	else
	{
		for(const TPair< UStaticMesh*, TSet< UStaticMeshComponent* > >& Entry : StaticMeshMap)
		{
			Compute(  Entry.Key, Entry.Value );
		}
	}
}

int32 UDatasmithStaticMeshBlueprintLibrary::ComputeLightmapResolution(UStaticMesh* StaticMesh, float IdealRatio, const FVector& StaticMeshScale)
{
	if(StaticMesh == nullptr || !StaticMesh->HasValidRenderData())
	{
		return 0;
	}

	const FRawStaticIndexBuffer& IndexBuffer = StaticMesh->RenderData->LODResources[0].IndexBuffer;
	const FPositionVertexBuffer& PositionBuffer = StaticMesh->RenderData->LODResources[0].VertexBuffers.PositionVertexBuffer;
	const FStaticMeshVertexBuffer& VertexBuffer = StaticMesh->RenderData->LODResources[0].VertexBuffers.StaticMeshVertexBuffer;

	if (VertexBuffer.GetNumTexCoords() <= (uint32)StaticMesh->LightMapCoordinateIndex)
	{
		return 0;
	}

	// Compute the mesh UV density, based on FStaticMeshRenderData::ComputeUVDensities, except that we're only working the Lightmap UV.
	TArray< FVector2D > PolygonAreas;
	const int32 NumberOfTriangles = IndexBuffer.GetNumIndices() / 3;
	for (int32 TriangleIndex = 0; TriangleIndex < NumberOfTriangles; ++TriangleIndex)
	{
		FVector VertexPosition[3];
		FVector2D LightmapUVs[3];

		for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
		{
			uint32 VertexIndex = IndexBuffer.GetIndex(TriangleIndex * 3 + CornerIndex);
			VertexPosition[CornerIndex] = PositionBuffer.VertexPosition(VertexIndex);
			LightmapUVs[CornerIndex] = VertexBuffer.GetVertexUV(VertexIndex, StaticMesh->LightMapCoordinateIndex);
		}

		const float PolygonArea = DatasmithStaticMeshBlueprintLibraryUtil::ParallelogramArea(VertexPosition[0], VertexPosition[1], VertexPosition[2]);
		const float PolygonUVArea = DatasmithStaticMeshBlueprintLibraryUtil::ParallelogramArea(FVector(LightmapUVs[0], 0.f), FVector(LightmapUVs[1], 0.f), FVector(LightmapUVs[2], 0.f));

		PolygonAreas.Emplace(FMath::Sqrt(PolygonArea), FMath::Sqrt(PolygonArea / PolygonUVArea));
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
