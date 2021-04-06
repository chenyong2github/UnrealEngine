// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorStaticMeshLibrary.h"

#include "EditorScriptingUtils.h"

#include "ActorEditorUtils.h"
#include "AssetRegistryModule.h"
#include "Components/MeshComponent.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "EditorFramework/AssetImportData.h"
#include "EngineUtils.h"
#include "Engine/Brush.h"
#include "Engine/Selection.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "FbxMeshUtils.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "IContentBrowserSingleton.h"
#include "IMeshMergeUtilities.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet2/ComponentEditorUtils.h"
#include "LevelEditorViewport.h"
#include "Engine/MapBuildDataRegistry.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "MeshMergeModule.h"
#include "PhysicsEngine/BodySetup.h"
#include "ScopedTransaction.h"
#include "Async/ParallelFor.h"
#include "Algo/AllOf.h"
#include "Algo/AnyOf.h"
#include "Async/Async.h"
#include "Misc/ScopedSlowTask.h"

#include "UnrealEdGlobals.h"
#include "UnrealEd/Private/GeomFitUtils.h"
#include "UnrealEd/Private/ConvexDecompTool.h"
#include "Subsystems/AssetEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "EditorStaticMeshLibrary"

/**
 *
 * Editor Scripting | Dataprep
 *
 **/

namespace InternalEditorMeshLibrary
{
	/** Note: This method is a replicate of FStaticMeshEditor::DoDecomp */
	bool GenerateConvexCollision(UStaticMesh* StaticMesh, uint32 HullCount, int32 MaxHullVerts, uint32 HullPrecision)
	{
		// Check we have a valid StaticMesh
		if (!StaticMesh || !StaticMesh->IsMeshDescriptionValid(0))
		{
			return false;
		}

		TRACE_CPUPROFILER_EVENT_SCOPE(GenerateConvexCollision)

		// If RenderData has not been computed yet, do it
		if (!StaticMesh->GetRenderData())
		{
			StaticMesh->CacheDerivedData();
		}

		const FStaticMeshLODResources& LODModel = StaticMesh->GetRenderData()->LODResources[0];

		// Make vertex buffer
		int32 NumVerts = LODModel.VertexBuffers.StaticMeshVertexBuffer.GetNumVertices();
		TArray<FVector> Verts;
		Verts.Reserve(NumVerts);
		for(int32 i=0; i<NumVerts; i++)
		{
			Verts.Add(LODModel.VertexBuffers.PositionVertexBuffer.VertexPosition(i));
		}

		// Grab all indices
		TArray<uint32> AllIndices;
		LODModel.IndexBuffer.GetCopy(AllIndices);

		// Only copy indices that have collision enabled
		TArray<uint32> CollidingIndices;
		for(const FStaticMeshSection& Section : LODModel.Sections)
		{
			if(Section.bEnableCollision)
			{
				for (uint32 IndexIdx = Section.FirstIndex; IndexIdx < Section.FirstIndex + (Section.NumTriangles * 3); IndexIdx++)
				{
					CollidingIndices.Add(AllIndices[IndexIdx]);
				}
			}
		}

		// Do not perform any action if we have invalid input
		if(Verts.Num() < 3 || CollidingIndices.Num() < 3)
		{
			return false;
		}

		// Get the BodySetup we are going to put the collision into
		UBodySetup* BodySetup = StaticMesh->GetBodySetup();
		if(BodySetup)
		{
			BodySetup->RemoveSimpleCollision();
		}
		else
		{
			// Otherwise, create one here.
			StaticMesh->CreateBodySetup();
			BodySetup = StaticMesh->GetBodySetup();
		}

		// Run actual util to do the work (if we have some valid input)
		DecomposeMeshToHulls(BodySetup, Verts, CollidingIndices, HullCount, MaxHullVerts, HullPrecision);

		StaticMesh->bCustomizedCollision = true;	//mark the static mesh for collision customization

		return true;
	}

	bool IsUVChannelValid(UStaticMesh* StaticMesh, int32 LODIndex, int32 UVChannelIndex)
	{
		if (StaticMesh == nullptr)
		{
			UE_LOG(LogEditorScripting, Error, TEXT("The StaticMesh is null."));
			return false;
		}

		if (LODIndex >= StaticMesh->GetNumLODs() || LODIndex < 0)
		{
			UE_LOG(LogEditorScripting, Error, TEXT("The StaticMesh doesn't have LOD %d."), LODIndex);
			return false;
		}

		if (!StaticMesh->IsMeshDescriptionValid(LODIndex))
		{
			UE_LOG(LogEditorScripting, Error, TEXT("No mesh description for LOD %d."), LODIndex);
			return false;
		}

		int32 NumUVChannels = StaticMesh->GetNumUVChannels(LODIndex);
		if (UVChannelIndex < 0 || UVChannelIndex >= NumUVChannels)
		{
			UE_LOG(LogEditorScripting, Error, TEXT("The given UV channel index %d is out of bounds."), UVChannelIndex);
			return false;
		}

		return true;
	}
}

int32 UEditorStaticMeshLibrary::SetLodsWithNotification(UStaticMesh* StaticMesh, const FEditorScriptingMeshReductionOptions& ReductionOptions, bool bApplyChanges)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return -1;
	}

	if (StaticMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("SetLODs: The StaticMesh is null."));
		return -1;
	}

	// If LOD 0 does not exist, warn and return
	if (StaticMesh->GetNumSourceModels() == 0)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("SetLODs: This StaticMesh does not have LOD 0."));
		return -1;
	}

	if(ReductionOptions.ReductionSettings.Num() == 0)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("SetLODs: Nothing done as no LOD settings were provided."));
		return -1;
	}

	// Close the mesh editor to prevent crashing. If changes are applied, reopen it after the mesh has been built.
	bool bStaticMeshIsEdited = false;
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (AssetEditorSubsystem->FindEditorForAsset(StaticMesh, false))
	{
		AssetEditorSubsystem->CloseAllEditorsForAsset(StaticMesh);
		bStaticMeshIsEdited = true;
	}

	if(bApplyChanges)
	{
		StaticMesh->Modify();
	}

	// Resize array of LODs to only keep LOD 0
	StaticMesh->SetNumSourceModels(1);

	// Set up LOD 0
	StaticMesh->GetSourceModel(0).ReductionSettings.PercentTriangles = ReductionOptions.ReductionSettings[0].PercentTriangles;
	StaticMesh->GetSourceModel(0).ScreenSize = ReductionOptions.ReductionSettings[0].ScreenSize;

	int32 LODIndex = 1;
	for (; LODIndex < ReductionOptions.ReductionSettings.Num(); ++LODIndex)
	{
		// Create new SourceModel for new LOD
		FStaticMeshSourceModel& SrcModel = StaticMesh->AddSourceModel();

		// Copy settings from previous LOD
		SrcModel.BuildSettings = StaticMesh->GetSourceModel(LODIndex-1).BuildSettings;
		SrcModel.ReductionSettings = StaticMesh->GetSourceModel(LODIndex-1).ReductionSettings;

		// Modify reduction settings based on user's requirements
		SrcModel.ReductionSettings.PercentTriangles = ReductionOptions.ReductionSettings[LODIndex].PercentTriangles;
		SrcModel.ScreenSize = ReductionOptions.ReductionSettings[LODIndex].ScreenSize;

		// Stop when reaching maximum of supported LODs
		if (StaticMesh->GetNumSourceModels() == MAX_STATIC_MESH_LODS)
		{
			break;
		}
	}

	StaticMesh->bAutoComputeLODScreenSize = ReductionOptions.bAutoComputeLODScreenSize ? 1 : 0;

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

	return LODIndex;
}

void UEditorStaticMeshLibrary::GetLodReductionSettings(const UStaticMesh* StaticMesh, const int32 LodIndex, FMeshReductionSettings& OutReductionOptions)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return;
	}

	if (StaticMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("GetLodReductionSettings: The StaticMesh is null."));
		return;
	}

	// If LOD 0 does not exist, warn and return
	if (LodIndex < 0 || StaticMesh->GetNumSourceModels() <= LodIndex)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("GetLodReductionSettings: Invalid LOD index."));
		return;
	}

	const FStaticMeshSourceModel& LODModel = StaticMesh->GetSourceModel(LodIndex);

	// Copy over the reduction settings
	OutReductionOptions = LODModel.ReductionSettings;
}

void UEditorStaticMeshLibrary::SetLodReductionSettings(UStaticMesh* StaticMesh, const int32 LodIndex, const FMeshReductionSettings& ReductionOptions)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return;
	}

	if (StaticMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("SetLodReductionSettings: The StaticMesh is null."));
		return;
	}

	// If LOD 0 does not exist, warn and return
	if (LodIndex < 0 || StaticMesh->GetNumSourceModels() <= LodIndex)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("SetLodReductionSettings: Invalid LOD index."));
		return;
	}

	// Close the mesh editor to prevent crashing. If changes are applied, reopen it after the mesh has been built.
	bool bStaticMeshIsEdited = false;
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (AssetEditorSubsystem->FindEditorForAsset(StaticMesh, false))
	{
		AssetEditorSubsystem->CloseAllEditorsForAsset(StaticMesh);
		bStaticMeshIsEdited = true;
	}

	StaticMesh->Modify();

	FStaticMeshSourceModel& LODModel = StaticMesh->GetSourceModel(LodIndex);

	// Copy over the reduction settings
	LODModel.ReductionSettings = ReductionOptions;

	// Request re-building of mesh with new LODs
	StaticMesh->PostEditChange();

	// Reopen MeshEditor on this mesh if the MeshEditor was previously opened in it
	if (bStaticMeshIsEdited)
	{
		AssetEditorSubsystem->OpenEditorForAsset(StaticMesh);
	}
}

void UEditorStaticMeshLibrary::GetLodBuildSettings(const UStaticMesh* StaticMesh, const int32 LodIndex, FMeshBuildSettings& OutBuildOptions)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return;
	}

	if (StaticMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("GetLodBuildSettings: The StaticMesh is null."));
		return;
	}

	// If LOD 0 does not exist, warn and return
	if (LodIndex < 0 || StaticMesh->GetNumSourceModels() <= LodIndex)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("GetLodBuildSettings: Invalid LOD index."));
		return;
	}

	const FStaticMeshSourceModel& LODModel = StaticMesh->GetSourceModel(LodIndex);

	// Copy over the reduction settings
	OutBuildOptions = LODModel.BuildSettings;
}

void UEditorStaticMeshLibrary::SetLodBuildSettings(UStaticMesh* StaticMesh, const int32 LodIndex, const FMeshBuildSettings& BuildOptions)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return;
	}

	if (StaticMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("SetLodBuildSettings: The StaticMesh is null."));
		return;
	}

	// If LOD 0 does not exist, warn and return
	if (LodIndex < 0 || StaticMesh->GetNumSourceModels() <= LodIndex)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("SetLodBuildSettings: Invalid LOD index."));
		return;
	}

	// Close the mesh editor to prevent crashing. If changes are applied, reopen it after the mesh has been built.
	bool bStaticMeshIsEdited = false;
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (AssetEditorSubsystem->FindEditorForAsset(StaticMesh, false))
	{
		AssetEditorSubsystem->CloseAllEditorsForAsset(StaticMesh);
		bStaticMeshIsEdited = true;
	}

	StaticMesh->Modify();

	FStaticMeshSourceModel& LODModel = StaticMesh->GetSourceModel(LodIndex);

	// Copy over the build settings
	LODModel.BuildSettings = BuildOptions;

	// Request re-building of mesh with new LODs
	StaticMesh->PostEditChange();

	// Reopen MeshEditor on this mesh if the MeshEditor was previously opened in it
	if (bStaticMeshIsEdited)
	{
		AssetEditorSubsystem->OpenEditorForAsset(StaticMesh);
	}
}

int32 UEditorStaticMeshLibrary::ImportLOD(UStaticMesh* BaseStaticMesh, const int32 LODIndex, const FString& SourceFilename)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		UE_LOG(LogEditorScripting, Error, TEXT("StaticMesh ImportLOD: Cannot import or re-import when editor PIE is active."));
		return INDEX_NONE;
	}

	if (BaseStaticMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("StaticMesh ImportLOD: The StaticMesh is null."));
		return INDEX_NONE;
	}

	// Make sure the LODIndex we want to add the LOD is valid
	if (BaseStaticMesh->GetNumSourceModels() < LODIndex)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("StaticMesh ImportLOD: Invalid LODIndex, the LOD index cannot be greater the the number of LOD, static mesh cannot have hole in the LOD array."));
		return INDEX_NONE;
	}

	FString ResolveFilename = SourceFilename;
	const bool bSourceFileExists = FPaths::FileExists(ResolveFilename);
	if(!bSourceFileExists)
	{
		if (BaseStaticMesh->IsSourceModelValid(LODIndex))
		{
			const FStaticMeshSourceModel& SourceModel = BaseStaticMesh->GetSourceModel(LODIndex);
			ResolveFilename = SourceModel.SourceImportFilename.IsEmpty() ?
				SourceModel.SourceImportFilename :
				UAssetImportData::ResolveImportFilename(SourceModel.SourceImportFilename, nullptr);
		}
	}

	if (!FPaths::FileExists(ResolveFilename))
	{
		UE_LOG(LogEditorScripting, Error, TEXT("StaticMesh ImportLOD: Invalid source filename."));
		return INDEX_NONE;
	}


	if(!FbxMeshUtils::ImportStaticMeshLOD(BaseStaticMesh, ResolveFilename, LODIndex))
	{
		UE_LOG(LogEditorScripting, Error, TEXT("StaticMesh ImportLOD: Cannot import mesh LOD."));
		return INDEX_NONE;
	}

	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostLODImport(BaseStaticMesh, LODIndex);

	return LODIndex;
}

bool UEditorStaticMeshLibrary::ReimportAllCustomLODs(UStaticMesh* StaticMesh)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		UE_LOG(LogEditorScripting, Error, TEXT("StaticMesh ReimportAllCustomLODs: Cannot import or re-import when editor PIE is active."));
		return false;
	}

	if (StaticMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("StaticMesh ReimportAllCustomLODs: The StaticMesh is null."));
		return false;
	}

	bool bResult = true;
	int32 LODNumber = StaticMesh->GetNumLODs();
	//Iterate the static mesh LODs, start at index 1
	for (int32 LODIndex = 1; LODIndex < LODNumber; ++LODIndex)
	{
		const FStaticMeshSourceModel& SourceModel = StaticMesh->GetSourceModel(LODIndex);
		//Skip LOD import in the same file as the base mesh, they are already re-import
		if (SourceModel.bImportWithBaseMesh)
		{
			continue;
		}

		bool bHasBeenSimplified = !StaticMesh->IsMeshDescriptionValid(LODIndex) || StaticMesh->IsReductionActive(LODIndex);
		if (bHasBeenSimplified)
		{
			continue;
		}

		if (ImportLOD(StaticMesh, LODIndex, SourceModel.SourceImportFilename) != LODIndex)
		{
			UE_LOG(LogEditorScripting, Error, TEXT("StaticMesh ReimportAllCustomLODs: Cannot re-import LOD %d."), LODIndex);
			bResult = false;
		}
	}
	return bResult;
}

int32 UEditorStaticMeshLibrary::SetLodFromStaticMesh(UStaticMesh* DestinationStaticMesh, int32 DestinationLodIndex, UStaticMesh* SourceStaticMesh, int32 SourceLodIndex, bool bReuseExistingMaterialSlots)
{
	TGuardValue<bool> UnattendedScriptGuard( GIsRunningUnattendedScript, true );

	if ( !EditorScriptingUtils::IsInEditorAndNotPlaying() )
	{
		return -1;
	}

	if ( DestinationStaticMesh == nullptr )
	{
		UE_LOG(LogEditorScripting, Error, TEXT("SetLodFromStaticMesh: The DestinationStaticMesh is null."));
		return -1;
	}

	if ( SourceStaticMesh == nullptr )
	{
		UE_LOG(LogEditorScripting, Error, TEXT("SetLodFromStaticMesh: The SourceStaticMesh is null."));
		return -1;
	}

	if ( !SourceStaticMesh->IsSourceModelValid( SourceLodIndex ) )
	{
		UE_LOG(LogEditorScripting, Error, TEXT("SetLodFromStaticMesh: SourceLodIndex is invalid."));
		return -1;
	}

	// Close the mesh editor to prevent crashing. Reopen it after the mesh has been built.
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	bool bStaticMeshIsEdited = false;
	if ( AssetEditorSubsystem->FindEditorForAsset( DestinationStaticMesh, false ) )
	{
		AssetEditorSubsystem->CloseAllEditorsForAsset( DestinationStaticMesh );
		bStaticMeshIsEdited = true;
	}

	DestinationStaticMesh->Modify();

	if ( DestinationStaticMesh->GetNumSourceModels() < DestinationLodIndex + 1 )
	{
		// Add one LOD
		DestinationStaticMesh->AddSourceModel();

		DestinationLodIndex = DestinationStaticMesh->GetNumSourceModels() - 1;

		// The newly added SourceModel won't have a MeshDescription so create it explicitly
		DestinationStaticMesh->CreateMeshDescription(DestinationLodIndex);
	}

	// Transfers the build settings and the reduction settings.
	const FStaticMeshSourceModel& SourceMeshSourceModel = SourceStaticMesh->GetSourceModel(SourceLodIndex);
	FStaticMeshSourceModel& DestinationMeshSourceModel = DestinationStaticMesh->GetSourceModel(DestinationLodIndex);
	DestinationMeshSourceModel.BuildSettings = SourceMeshSourceModel.BuildSettings;
	DestinationMeshSourceModel.ReductionSettings = SourceMeshSourceModel.ReductionSettings;
	// Base the reduction on the new lod
	DestinationMeshSourceModel.ReductionSettings.BaseLODModel = DestinationLodIndex;

	// Fragile. If a public function emerge to determine if a reduction will be used please consider using it and remove this code.
	bool bDoesSourceLodUseReduction = false;
	switch (SourceMeshSourceModel.ReductionSettings.TerminationCriterion)
	{
	case EStaticMeshReductionTerimationCriterion::Triangles:
		bDoesSourceLodUseReduction = !FMath::IsNearlyEqual(SourceMeshSourceModel.ReductionSettings.PercentTriangles, 100.f);
		break;
	case EStaticMeshReductionTerimationCriterion::Vertices:
		bDoesSourceLodUseReduction = !FMath::IsNearlyEqual(SourceMeshSourceModel.ReductionSettings.PercentVertices, 100.f);
		break;
	case EStaticMeshReductionTerimationCriterion::Any:
		bDoesSourceLodUseReduction = !(FMath::IsNearlyEqual(SourceMeshSourceModel.ReductionSettings.PercentTriangles, 100.f) && FMath::IsNearlyEqual(SourceMeshSourceModel.ReductionSettings.PercentVertices, 100.f));
		break;
	default:
		break;
	}
	bDoesSourceLodUseReduction |= SourceMeshSourceModel.ReductionSettings.MaxDeviation > 0.f;


	int32 BaseSourceLodIndex  = bDoesSourceLodUseReduction ? SourceMeshSourceModel.ReductionSettings.BaseLODModel : SourceLodIndex;
	bool bIsReductionSettingAproximated = false;

	// Find the original mesh description for this LOD
	while (!SourceStaticMesh->IsMeshDescriptionValid(BaseSourceLodIndex ))
	{
		if (!SourceStaticMesh->IsSourceModelValid(BaseSourceLodIndex))
		{
			UE_LOG(LogEditorScripting, Error, TEXT("SetLodFromStaticMesh: The SourceStaticMesh is in a invalid state."));
			return -1;
		}

		const FMeshReductionSettings& PossibleSourceMeshReductionSetting = SourceStaticMesh->GetSourceModel(BaseSourceLodIndex).ReductionSettings;
		DestinationMeshSourceModel.ReductionSettings.PercentTriangles *= PossibleSourceMeshReductionSetting.PercentTriangles;
		DestinationMeshSourceModel.ReductionSettings.PercentVertices *= PossibleSourceMeshReductionSetting.PercentVertices;
		BaseSourceLodIndex  = SourceStaticMesh->GetSourceModel(BaseSourceLodIndex).ReductionSettings.BaseLODModel;

		bIsReductionSettingAproximated = true;
	}

	if (bIsReductionSettingAproximated)
	{
		TArray<FStringFormatArg> InOrderedArguments;
		InOrderedArguments.Reserve(4);
		InOrderedArguments.Add(SourceStaticMesh->GetName());
		InOrderedArguments.Add(SourceLodIndex);
		InOrderedArguments.Add(DestinationLodIndex);
		InOrderedArguments.Add(DestinationStaticMesh->GetName());

		UE_LOG(LogEditorScripting, Warning, TEXT("%s"), *FString::Format(TEXT("SetLodFromStaticMesh: The reduction settings from the SourceStaticMesh {0} LOD {1} were approximated."
			" The LOD {2} from {3} might not be identical."), InOrderedArguments));
	}

	// Copy the source import file.
	DestinationMeshSourceModel.SourceImportFilename = SourceStaticMesh->GetSourceModel(BaseSourceLodIndex).SourceImportFilename;

	// Copy the mesh description
	const FMeshDescription& SourceMeshDescription = *SourceStaticMesh->GetMeshDescription(BaseSourceLodIndex);
	FMeshDescription& DestinationMeshDescription = *DestinationStaticMesh->GetMeshDescription(DestinationLodIndex);
	DestinationMeshDescription = SourceMeshDescription;
	DestinationStaticMesh->CommitMeshDescription(DestinationLodIndex);

	// Assign materials for the destination LOD
	{
		auto FindMaterialIndex = []( UStaticMesh* StaticMesh, const UMaterialInterface* Material ) -> int32
		{
			for ( int32 MaterialIndex = 0; MaterialIndex < StaticMesh->GetStaticMaterials().Num(); ++MaterialIndex )
			{
				if ( StaticMesh->GetMaterial( MaterialIndex ) == Material )
				{
					return MaterialIndex;
				}
			}

			return INDEX_NONE;
		};

		TMap< int32, int32 > LodSectionMaterialMapping; // LOD section index -> destination material index

		int32 NumDestinationMaterial = DestinationStaticMesh->GetStaticMaterials().Num();

		const int32 SourceLodNumSections = SourceStaticMesh->GetSectionInfoMap().GetSectionNumber( SourceLodIndex );

		for ( int32 SourceLodSectionIndex = 0; SourceLodSectionIndex < SourceLodNumSections; ++SourceLodSectionIndex )
		{
			const FMeshSectionInfo& SourceMeshSectionInfo = SourceStaticMesh->GetSectionInfoMap().Get( SourceLodIndex, SourceLodSectionIndex );

			const UMaterialInterface* SourceMaterial = SourceStaticMesh->GetMaterial( SourceMeshSectionInfo.MaterialIndex );

			int32 DestinationMaterialIndex = INDEX_NONE;

			if ( bReuseExistingMaterialSlots )
			{
				DestinationMaterialIndex = FindMaterialIndex( DestinationStaticMesh, SourceMaterial );
			}

			if ( DestinationMaterialIndex == INDEX_NONE )
			{
				DestinationMaterialIndex = NumDestinationMaterial++;
			}

			LodSectionMaterialMapping.Add( SourceLodSectionIndex, DestinationMaterialIndex );
		}

		for ( TMap< int32, int32 >::TConstIterator It = LodSectionMaterialMapping.CreateConstIterator(); It; ++It )
		{
			const int32 SectionIndex = It->Key;

			const FMeshSectionInfo& SourceSectionInfo = SourceStaticMesh->GetSectionInfoMap().Get( SourceLodIndex, SectionIndex );

			UMaterialInterface* SourceMaterial = SourceStaticMesh->GetMaterial( SourceSectionInfo.MaterialIndex );

			const int32 SourceMaterialIndex = SourceSectionInfo.MaterialIndex;
			const int32 DestinationMaterialIndex = It->Value;

			if ( !DestinationStaticMesh->GetStaticMaterials().IsValidIndex( DestinationMaterialIndex ) )
			{
				DestinationStaticMesh->GetStaticMaterials().Add( SourceStaticMesh->GetStaticMaterials()[ SourceSectionInfo.MaterialIndex ] );

				ensure( DestinationStaticMesh->GetStaticMaterials().Num() == DestinationMaterialIndex + 1 ); // We assume that we are not creating holes in StaticMaterials
			}

			FMeshSectionInfo DestinationSectionInfo = SourceSectionInfo;
			DestinationSectionInfo.MaterialIndex = DestinationMaterialIndex;

			DestinationStaticMesh->GetSectionInfoMap().Set( DestinationLodIndex, SectionIndex, MoveTemp( DestinationSectionInfo ) );
		}
	}

	DestinationStaticMesh->PostEditChange();

	// Reopen MeshEditor on this mesh if the MeshEditor was previously opened in it
	if ( bStaticMeshIsEdited )
	{
		AssetEditorSubsystem->OpenEditorForAsset( DestinationStaticMesh );
	}

	return DestinationLodIndex;
}

int32 UEditorStaticMeshLibrary::GetLodCount(UStaticMesh* StaticMesh)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (StaticMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("GetLODCount: The StaticMesh is null."));
		return -1;
	}

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return -1;
	}

	return StaticMesh->GetNumSourceModels();
}

bool UEditorStaticMeshLibrary::RemoveLods(UStaticMesh* StaticMesh)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (StaticMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("RemoveLODs: The StaticMesh is null."));
		return false;
	}

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return false;
	}

	// No main LOD, skip
	if (StaticMesh->GetNumSourceModels() == 0)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("RemoveLODs: This StaticMesh does not have LOD 0."));
		return false;
	}

	// Close the mesh editor to prevent crashing. Reopen it after the mesh has been built.
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	bool bStaticMeshIsEdited = false;
	if (AssetEditorSubsystem->FindEditorForAsset(StaticMesh, false))
	{
		AssetEditorSubsystem->CloseAllEditorsForAsset(StaticMesh);
		bStaticMeshIsEdited = true;
	}

	// Reduce array of source models to 1
	StaticMesh->Modify();
	StaticMesh->SetNumSourceModels(1);

	// Request re-building of mesh with new LODs
	StaticMesh->PostEditChange();

	// Reopen MeshEditor on this mesh if the MeshEditor was previously opened in it
	if (bStaticMeshIsEdited)
	{
		AssetEditorSubsystem->OpenEditorForAsset(StaticMesh);
	}

	return true;
}

TArray<float> UEditorStaticMeshLibrary::GetLodScreenSizes(UStaticMesh* StaticMesh)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	TArray<float> ScreenSizes;
	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return ScreenSizes;
	}

	if (StaticMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("GetLodScreenSizes: The StaticMesh is null."));
		return ScreenSizes;
	}

	for (int i = 0; i < StaticMesh->GetNumLODs(); i++)
	{
		if (StaticMesh->GetRenderData())
		{
			float CurScreenSize = StaticMesh->GetRenderData()->ScreenSize[i].Default;
			ScreenSizes.Add(CurScreenSize);
		}
		else
		{
			UE_LOG(LogEditorScripting, Warning, TEXT("GetLodScreenSizes: The RenderData is invalid for LOD %d."), i);
		}
	}

	return ScreenSizes;

}

int32 UEditorStaticMeshLibrary::AddSimpleCollisionsWithNotification(UStaticMesh* StaticMesh, const EScriptingCollisionShapeType ShapeType, bool bApplyChanges)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (StaticMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("AddSimpleCollisions: The StaticMesh is null."));
		return INDEX_NONE;
	}

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return INDEX_NONE;
	}

	// Close the mesh editor to prevent crashing. Reopen it after the mesh has been built.
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	bool bStaticMeshIsEdited = false;
	if (AssetEditorSubsystem->FindEditorForAsset(StaticMesh, false))
	{
		AssetEditorSubsystem->CloseAllEditorsForAsset(StaticMesh);
		bStaticMeshIsEdited = true;
	}

	int32 PrimIndex = INDEX_NONE;

	switch (ShapeType)
	{
		case EScriptingCollisionShapeType::Box:
		{
			PrimIndex = GenerateBoxAsSimpleCollision(StaticMesh);
			break;
		}
		case EScriptingCollisionShapeType::Sphere:
		{
			PrimIndex = GenerateSphereAsSimpleCollision(StaticMesh);
			break;
		}
		case EScriptingCollisionShapeType::Capsule:
		{
			PrimIndex = GenerateSphylAsSimpleCollision(StaticMesh);
			break;
		}
		case EScriptingCollisionShapeType::NDOP10_X:
		{
			TArray<FVector>	DirArray(KDopDir10X, 10);
			PrimIndex = GenerateKDopAsSimpleCollision(StaticMesh, DirArray);
			break;
		}
		case EScriptingCollisionShapeType::NDOP10_Y:
		{
			TArray<FVector>	DirArray(KDopDir10Y, 10);
			PrimIndex = GenerateKDopAsSimpleCollision(StaticMesh, DirArray);
			break;
		}
		case EScriptingCollisionShapeType::NDOP10_Z:
		{
			TArray<FVector>	DirArray(KDopDir10Z, 10);
			PrimIndex = GenerateKDopAsSimpleCollision(StaticMesh, DirArray);
			break;
		}
		case EScriptingCollisionShapeType::NDOP18:
		{
			TArray<FVector>	DirArray(KDopDir18, 18);
			PrimIndex = GenerateKDopAsSimpleCollision(StaticMesh, DirArray);
			break;
		}
		case EScriptingCollisionShapeType::NDOP26:
		{
			TArray<FVector>	DirArray(KDopDir26, 26);
			PrimIndex = GenerateKDopAsSimpleCollision(StaticMesh, DirArray);
			break;
		}
	}

	if(bApplyChanges)
	{
		// Request re-building of mesh with new collision shapes
		StaticMesh->PostEditChange();

		// Reopen MeshEditor on this mesh if the MeshEditor was previously opened in it
		if (bStaticMeshIsEdited)
		{
			AssetEditorSubsystem->OpenEditorForAsset(StaticMesh);
		}
	}

	return PrimIndex;
}

int32 UEditorStaticMeshLibrary::GetSimpleCollisionCount(UStaticMesh* StaticMesh)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (StaticMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("GetSimpleCollisionCount: The StaticMesh is null."));
		return -1;
	}

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return -1;
	}

	UBodySetup* BodySetup = StaticMesh->GetBodySetup();
	if (BodySetup == nullptr)
	{
		return 0;
	}

	int32 Count = BodySetup->AggGeom.BoxElems.Num();
	Count += BodySetup->AggGeom.SphereElems.Num();
	Count += BodySetup->AggGeom.SphylElems.Num();

	return Count;
}

TEnumAsByte<ECollisionTraceFlag> UEditorStaticMeshLibrary::GetCollisionComplexity(UStaticMesh* StaticMesh)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (StaticMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("GetCollisionComplexity: The StaticMesh is null."));
		return ECollisionTraceFlag::CTF_UseDefault;
	}

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return ECollisionTraceFlag::CTF_UseDefault;
	}

	if (StaticMesh->GetBodySetup())
	{
		return StaticMesh->GetBodySetup()->CollisionTraceFlag;
	}

	return ECollisionTraceFlag::CTF_UseDefault;
}

int32 UEditorStaticMeshLibrary::GetConvexCollisionCount(UStaticMesh* StaticMesh)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (StaticMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("GetConvexCollisionCount: The StaticMesh is null."));
		return -1;
	}

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return -1;
	}

	UBodySetup* BodySetup = StaticMesh->GetBodySetup();
	if (BodySetup == nullptr)
	{
		return 0;
	}

	return BodySetup->AggGeom.ConvexElems.Num();
}

bool UEditorStaticMeshLibrary::BulkSetConvexDecompositionCollisionsWithNotification(const TArray<UStaticMesh*>& InStaticMeshes, int32 HullCount, int32 MaxHullVerts, int32 HullPrecision, bool bApplyChanges)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UEditorStaticMeshLibrary::SetConvexDecompositionCollisionsWithNotification)

	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return false;
	}

	TArray<UStaticMesh*> StaticMeshes(InStaticMeshes);
	StaticMeshes.RemoveAll([](const UStaticMesh* StaticMesh) { return StaticMesh == nullptr || !StaticMesh->IsMeshDescriptionValid(0); });

	if (StaticMeshes.Num() == 0)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("SetConvexDecompositionCollisions: The StaticMesh is null."));
		return false;
	}

	if (HullCount < 0 || HullPrecision < 0)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("SetConvexDecompositionCollisions: Parameters HullCount and HullPrecision must be positive."));
		return false;
	}

	if (Algo::AnyOf(StaticMeshes, [](const UStaticMesh* StaticMesh) { return StaticMesh->GetRenderData() == nullptr; }))
	{
		UStaticMesh::BatchBuild(StaticMeshes);
	}

	Algo::SortBy(
		StaticMeshes,
		[](const UStaticMesh* StaticMesh){ return StaticMesh->GetRenderData()->LODResources[0].VertexBuffers.StaticMeshVertexBuffer.GetNumVertices(); },
		TGreater<>()
	);

	// Close the mesh editor to prevent crashing. Reopen it after the mesh has been built.
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();

	TSet<UStaticMesh*> EditedStaticMeshes;
	for (UStaticMesh* StaticMesh : StaticMeshes)
	{
		if (AssetEditorSubsystem->FindEditorForAsset(StaticMesh, false))
		{
			AssetEditorSubsystem->CloseAllEditorsForAsset(StaticMesh);
			EditedStaticMeshes.Add(StaticMesh);
		}

		if (StaticMesh->GetBodySetup())
		{
			if (bApplyChanges)
			{
				StaticMesh->GetBodySetup()->Modify();
			}

			// Remove simple collisions
			StaticMesh->GetBodySetup()->RemoveSimpleCollision();
		}
	}

	TArray<bool> bResults;
	bResults.SetNumZeroed(StaticMeshes.Num());

	TAtomic<uint32> Processed(0);
	TFuture<void> Result =
		Async(
			EAsyncExecution::ThreadPool,
			[&Processed, &bResults, &StaticMeshes, HullCount, MaxHullVerts, HullPrecision]()
			{
				ParallelFor(
					StaticMeshes.Num(),
					[&Processed, &bResults, &StaticMeshes, HullCount, MaxHullVerts, HullPrecision](int32 Index)
					{
						bResults[Index] = InternalEditorMeshLibrary::GenerateConvexCollision(StaticMeshes[Index], HullCount, MaxHullVerts, HullPrecision);
						Processed++;
					},
					EParallelForFlags::Unbalanced
				);
			}
		);

	uint32 LastProcessed = 0;
	const FText ProgressText = LOCTEXT("ComputingConvexCollision", "Computing convex collision for static mesh {0}/{1} ...");
	FScopedSlowTask Progress(StaticMeshes.Num(), FText::Format(ProgressText, LastProcessed, StaticMeshes.Num()));
	Progress.MakeDialog();

	while (!Result.WaitFor(FTimespan::FromMilliseconds(33.0)))
	{
		uint32 LocalProcessed = Processed.Load(EMemoryOrder::Relaxed);
		Progress.EnterProgressFrame(LocalProcessed - LastProcessed, FText::Format(ProgressText, LocalProcessed, StaticMeshes.Num()));
		LastProcessed = LocalProcessed;
	}

	// refresh collision change back to static mesh components
	RefreshCollisionChanges(StaticMeshes);

	if (bApplyChanges)
	{
		for (UStaticMesh* StaticMesh : StaticMeshes)
		{
			// Mark mesh as dirty
			StaticMesh->MarkPackageDirty();

			// Request re-building of mesh following collision changes
			StaticMesh->PostEditChange();
		}
	}

	// Reopen MeshEditor on this mesh if the MeshEditor was previously opened in it
	for (UStaticMesh* StaticMesh : EditedStaticMeshes)
	{
		AssetEditorSubsystem->OpenEditorForAsset(StaticMesh);
	}

	return Algo::AllOf(bResults);
}

bool UEditorStaticMeshLibrary::SetConvexDecompositionCollisionsWithNotification(UStaticMesh* StaticMesh, int32 HullCount, int32 MaxHullVerts, int32 HullPrecision, bool bApplyChanges)
{
	return BulkSetConvexDecompositionCollisionsWithNotification({StaticMesh}, HullCount, MaxHullVerts, HullPrecision, bApplyChanges);
}

bool UEditorStaticMeshLibrary::RemoveCollisionsWithNotification(UStaticMesh* StaticMesh, bool bApplyChanges)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return false;
	}

	if (StaticMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("RemoveCollisions: The StaticMesh is null."));
		return false;
	}

	if (StaticMesh->GetBodySetup() == nullptr)
	{
		UE_LOG(LogEditorScripting, Log, TEXT("RemoveCollisions: No collision set up. Nothing to do."));
		return true;
	}

	// Close the mesh editor to prevent crashing. Reopen it after the mesh has been built.
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	bool bStaticMeshIsEdited = false;
	if (AssetEditorSubsystem->FindEditorForAsset(StaticMesh, false))
	{
		AssetEditorSubsystem->CloseAllEditorsForAsset(StaticMesh);
		bStaticMeshIsEdited = true;
	}

	if(bApplyChanges)
	{
		StaticMesh->GetBodySetup()->Modify();
	}

	// Remove simple collisions
	StaticMesh->GetBodySetup()->RemoveSimpleCollision();

	// refresh collision change back to static mesh components
	RefreshCollisionChange(*StaticMesh);

	if(bApplyChanges)
	{
		// Request re-building of mesh with new collision shapes
		StaticMesh->PostEditChange();

		// Reopen MeshEditor on this mesh if the MeshEditor was previously opened in it
		if (bStaticMeshIsEdited)
		{
			AssetEditorSubsystem->OpenEditorForAsset(StaticMesh);
		}
	}

	return true;
}

void UEditorStaticMeshLibrary::EnableSectionCollision(UStaticMesh* StaticMesh, bool bCollisionEnabled, int32 LODIndex, int32 SectionIndex)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return;
	}

	if (StaticMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("EnableSectionCollision: The StaticMesh is null."));
		return;
	}

	if (LODIndex >= StaticMesh->GetNumLODs())
	{
		UE_LOG(LogEditorScripting, Error, TEXT("EnableSectionCollision: Invalid LOD index %d (of %d)."), LODIndex, StaticMesh->GetNumLODs());
		return;
	}

	if (SectionIndex >= StaticMesh->GetNumSections(LODIndex))
	{
		UE_LOG(LogEditorScripting, Error, TEXT("EnableSectionCollision: Invalid section index %d (of %d)."), SectionIndex, StaticMesh->GetNumSections(LODIndex));
		return;
	}

	StaticMesh->Modify();

	FMeshSectionInfo SectionInfo = StaticMesh->GetSectionInfoMap().Get(LODIndex, SectionIndex);

	SectionInfo.bEnableCollision = bCollisionEnabled;

	StaticMesh->GetSectionInfoMap().Set(LODIndex, SectionIndex, SectionInfo);

	StaticMesh->PostEditChange();
}

bool UEditorStaticMeshLibrary::IsSectionCollisionEnabled(UStaticMesh* StaticMesh, int32 LODIndex, int32 SectionIndex)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return false;
	}

	if (StaticMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("IsSectionCollisionEnabled: The StaticMesh is null."));
		return false;
	}

	if (LODIndex >= StaticMesh->GetNumLODs())
	{
		UE_LOG(LogEditorScripting, Error, TEXT("IsSectionCollisionEnabled: Invalid LOD index %d (of %d)."), LODIndex, StaticMesh->GetNumLODs());
		return false;
	}

	if (SectionIndex >= StaticMesh->GetNumSections(LODIndex))
	{
		UE_LOG(LogEditorScripting, Error, TEXT("IsSectionCollisionEnabled: Invalid section index %d (of %d)."), SectionIndex, StaticMesh->GetNumSections(LODIndex));
		return false;
	}

	FMeshSectionInfo SectionInfo = StaticMesh->GetSectionInfoMap().Get(LODIndex, SectionIndex);
	return SectionInfo.bEnableCollision;
}

void UEditorStaticMeshLibrary::EnableSectionCastShadow(UStaticMesh* StaticMesh, bool bCastShadow, int32 LODIndex, int32 SectionIndex)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return;
	}

	if (StaticMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("EnableSectionCastShadow: The StaticMesh is null."));
		return;
	}

	if (LODIndex >= StaticMesh->GetNumLODs())
	{
		UE_LOG(LogEditorScripting, Error, TEXT("EnableSectionCastShadow: Invalid LOD index %d (of %d)."), LODIndex, StaticMesh->GetNumLODs());
		return;
	}

	if (SectionIndex >= StaticMesh->GetNumSections(LODIndex))
	{
		UE_LOG(LogEditorScripting, Error, TEXT("EnableSectionCastShadow: Invalid section index %d (of %d)."), SectionIndex, StaticMesh->GetNumSections(LODIndex));
		return;
	}

	StaticMesh->Modify();

	FMeshSectionInfo SectionInfo = StaticMesh->GetSectionInfoMap().Get(LODIndex, SectionIndex);

	SectionInfo.bCastShadow = bCastShadow;

	StaticMesh->GetSectionInfoMap().Set(LODIndex, SectionIndex, SectionInfo);

	StaticMesh->PostEditChange();
}

void UEditorStaticMeshLibrary::SetLODMaterialSlot(UStaticMesh* StaticMesh, int32 MaterialSlotIndex, int32 LODIndex, int32 SectionIndex)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return;
	}

	if (StaticMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("SetLODMaterialSlot: The StaticMesh is null."));
		return;
	}

	if (LODIndex >= StaticMesh->GetNumLODs())
	{
		UE_LOG(LogEditorScripting, Error, TEXT("SetLODMaterialSlot: Invalid LOD index %d (of %d)."), LODIndex, StaticMesh->GetNumLODs());
		return;
	}

	if (SectionIndex >= StaticMesh->GetNumSections(LODIndex))
	{
		UE_LOG(LogEditorScripting, Error, TEXT("SetLODMaterialSlot: Invalid section index %d (of %d)."), SectionIndex, StaticMesh->GetNumSections(LODIndex));
		return;
	}

	if (MaterialSlotIndex >= StaticMesh->GetStaticMaterials().Num())
	{
		UE_LOG(LogEditorScripting, Error, TEXT("SetLODMaterialSlot: Invalid slot index %d (of %d)."), MaterialSlotIndex, StaticMesh->GetStaticMaterials().Num());
		return;
	}

	StaticMesh->Modify();

	FMeshSectionInfo SectionInfo = StaticMesh->GetSectionInfoMap().Get(LODIndex, SectionIndex);

	SectionInfo.MaterialIndex = MaterialSlotIndex;

	StaticMesh->GetSectionInfoMap().Set(LODIndex, SectionIndex, SectionInfo);

	StaticMesh->PostEditChange();
}

int32 UEditorStaticMeshLibrary::GetLODMaterialSlot( UStaticMesh* StaticMesh, int32 LODIndex, int32 SectionIndex )
{
	TGuardValue<bool> UnattendedScriptGuard( GIsRunningUnattendedScript, true );

	if ( !EditorScriptingUtils::IsInEditorAndNotPlaying() )
	{
		return INDEX_NONE;
	}

	if ( StaticMesh == nullptr )
	{
		UE_LOG( LogEditorScripting, Error, TEXT( "GetLODMaterialSlot: The StaticMesh is null." ) );
		return INDEX_NONE;
	}

	if ( LODIndex >= StaticMesh->GetNumLODs() )
	{
		UE_LOG( LogEditorScripting, Error, TEXT( "GetLODMaterialSlot: Invalid LOD index %d (of %d)." ), LODIndex, StaticMesh->GetNumLODs() );
		return INDEX_NONE;
	}

	if ( SectionIndex >= StaticMesh->GetNumSections( LODIndex ) )
	{
		UE_LOG( LogEditorScripting, Error, TEXT( "GetLODMaterialSlot: Invalid section index %d (of %d)." ), SectionIndex, StaticMesh->GetNumSections( LODIndex ) );
		return INDEX_NONE;
	}

	return StaticMesh->GetSectionInfoMap().Get( LODIndex, SectionIndex ).MaterialIndex;
}

bool UEditorStaticMeshLibrary::HasVertexColors(UStaticMesh* StaticMesh)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return false;
	}

	if (StaticMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("HasVertexColors: The StaticMesh is null."));
		return false;
	}

	for (int32 LodIndex = 0; LodIndex < StaticMesh->GetNumSourceModels(); ++LodIndex)
	{
		const FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription(LodIndex);
		FStaticMeshConstAttributes Attributes(*MeshDescription);
		TVertexInstanceAttributesConstRef<FVector4> VertexInstanceColors = Attributes.GetVertexInstanceColors();
		if (!VertexInstanceColors.IsValid())
		{
			continue;
		}

		for (const FVertexInstanceID VertexInstanceID : MeshDescription->VertexInstances().GetElementIDs())
		{
			FLinearColor VertexInstanceColor(VertexInstanceColors[VertexInstanceID]);
			if (VertexInstanceColor != FLinearColor::White)
			{
				return true;
			}
		}
	}
	return false;
}

bool UEditorStaticMeshLibrary::HasInstanceVertexColors(UStaticMeshComponent* StaticMeshComponent)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return false;
	}

	if (StaticMeshComponent == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("HasInstanceVertexColors: The StaticMeshComponent is null."));
		return false;
	}

	for (const FStaticMeshComponentLODInfo& CurrentLODInfo : StaticMeshComponent->LODData)
	{
		if (CurrentLODInfo.OverrideVertexColors != nullptr || CurrentLODInfo.PaintedVertices.Num() > 0)
		{
			return true;
		}
	}

	return false;
}

bool UEditorStaticMeshLibrary::SetGenerateLightmapUVs(UStaticMesh* StaticMesh, bool bGenerateLightmapUVs)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return false;
	}

	if (StaticMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("SetGenerateLightmapUVs: The StaticMesh is null."));
		return false;
	}

	bool AnySettingsToChange = false;
	for (int32 LodIndex = 0; LodIndex < StaticMesh->GetNumSourceModels(); ++LodIndex)
	{
		FStaticMeshSourceModel& SourceModel = StaticMesh->GetSourceModel(LodIndex);
		//Make sure LOD is not a reduction before considering its BuildSettings
		if (StaticMesh->IsMeshDescriptionValid(LodIndex))
		{
			AnySettingsToChange = (SourceModel.BuildSettings.bGenerateLightmapUVs != bGenerateLightmapUVs);

			if (AnySettingsToChange)
			{
				break;
			}
		}
	}

	if (AnySettingsToChange)
	{
		StaticMesh->Modify();
		for (FStaticMeshSourceModel& SourceModel : StaticMesh->GetSourceModels())
		{
			SourceModel.BuildSettings.bGenerateLightmapUVs = bGenerateLightmapUVs;

		}

		StaticMesh->Build();
		StaticMesh->PostEditChange();
		return true;
	}

	return false;
}

int32 UEditorStaticMeshLibrary::GetNumberVerts(UStaticMesh* StaticMesh, int32 LODIndex)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return 0;
	}

	if (StaticMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("GetNumberVerts: The StaticMesh is null."));
		return 0;
	}

	return StaticMesh->GetNumVertices(LODIndex);
}

int32 UEditorStaticMeshLibrary::GetNumberMaterials(UStaticMesh* StaticMesh)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return 0;
	}

	if (StaticMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("GetNumberMaterials: The StaticMesh is null."));
		return 0;
	}

	return StaticMesh->GetStaticMaterials().Num();
}

void UEditorStaticMeshLibrary::SetAllowCPUAccess(UStaticMesh* StaticMesh, bool bAllowCPUAccess)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return;
	}

	if (StaticMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("SetAllowCPUAccess: The StaticMesh is null."));
		return;
	}

	StaticMesh->Modify();
	StaticMesh->bAllowCPUAccess = bAllowCPUAccess;
	StaticMesh->PostEditChange();
}

int32 UEditorStaticMeshLibrary::GetNumUVChannels(UStaticMesh* StaticMesh, int32 LODIndex)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return 0;
	}

	if (StaticMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("GetNumUVChannels: The StaticMesh is null."));
		return 0;
	}

	if (LODIndex >= StaticMesh->GetNumLODs() || LODIndex < 0)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("GetNumUVChannels: The StaticMesh doesn't have LOD %d."), LODIndex);
		return 0;
	}

	return StaticMesh->GetNumUVChannels(LODIndex);
}

bool UEditorStaticMeshLibrary::AddUVChannel(UStaticMesh* StaticMesh, int32 LODIndex)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return false;
	}

	if (StaticMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("AddUVChannel: The StaticMesh is null."));
		return false;
	}

	if (LODIndex >= StaticMesh->GetNumLODs() || LODIndex < 0)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("AddUVChannel: The StaticMesh doesn't have LOD %d."), LODIndex);
		return false;
	}

	if (StaticMesh->GetNumUVChannels(LODIndex) >= MAX_MESH_TEXTURE_COORDS_MD)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("AddUVChannel: Cannot add UV channel. Maximum number of UV channels reached (%d)."), MAX_MESH_TEXTURE_COORDS_MD);
		return false;
	}

	return StaticMesh->AddUVChannel(LODIndex);
}

bool UEditorStaticMeshLibrary::InsertUVChannel(UStaticMesh* StaticMesh, int32 LODIndex, int32 UVChannelIndex)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return false;
	}

	if (StaticMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("InsertUVChannel: The StaticMesh is null."));
		return false;
	}

	if (LODIndex >= StaticMesh->GetNumLODs() || LODIndex < 0)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("InsertUVChannel: The StaticMesh doesn't have LOD %d."), LODIndex);
		return false;
	}

	int32 NumUVChannels = StaticMesh->GetNumUVChannels(LODIndex);
	if (UVChannelIndex < 0 || UVChannelIndex > NumUVChannels)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("InsertUVChannel: Cannot insert UV channel. Given UV channel index %d is out of bounds."), UVChannelIndex);
		return false;
	}

	if (NumUVChannels >= MAX_MESH_TEXTURE_COORDS_MD)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("InsertUVChannel: Cannot add UV channel. Maximum number of UV channels reached (%d)."), MAX_MESH_TEXTURE_COORDS_MD);
		return false;
	}

	return StaticMesh->InsertUVChannel(LODIndex, UVChannelIndex);
}

bool UEditorStaticMeshLibrary::RemoveUVChannel(UStaticMesh* StaticMesh, int32 LODIndex, int32 UVChannelIndex)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return false;
	}

	if (StaticMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("RemoveUVChannel: The StaticMesh is null."));
		return false;
	}

	if (LODIndex >= StaticMesh->GetNumLODs() || LODIndex < 0)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("RemoveUVChannel: The StaticMesh doesn't have LOD %d."), LODIndex);
		return false;
	}

	int32 NumUVChannels = StaticMesh->GetNumUVChannels(LODIndex);
	if (NumUVChannels == 1)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("RemoveUVChannel: Cannot remove UV channel. There must be at least one channel."));
		return false;
	}

	if (UVChannelIndex < 0 || UVChannelIndex >= NumUVChannels)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("RemoveUVChannel: Cannot remove UV channel. Given UV channel index %d is out of bounds."), UVChannelIndex);
		return false;
	}

	return StaticMesh->RemoveUVChannel(LODIndex, UVChannelIndex);
}

bool UEditorStaticMeshLibrary::GeneratePlanarUVChannel(UStaticMesh* StaticMesh, int32 LODIndex, int32 UVChannelIndex, const FVector& Position, const FRotator& Orientation, const FVector2D& Tiling)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return false;
	}

	if (!InternalEditorMeshLibrary::IsUVChannelValid(StaticMesh, LODIndex, UVChannelIndex))
	{
		return false;
	}

	FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription(LODIndex);

	FUVMapParameters UVParameters(Position, Orientation.Quaternion(), StaticMesh->GetBoundingBox().GetSize(), FVector::OneVector, Tiling );

	TMap<FVertexInstanceID, FVector2D> TexCoords;
	FStaticMeshOperations::GeneratePlanarUV(*MeshDescription, UVParameters, TexCoords);

	return StaticMesh->SetUVChannel(LODIndex, UVChannelIndex, TexCoords);
}

bool UEditorStaticMeshLibrary::GenerateCylindricalUVChannel(UStaticMesh* StaticMesh, int32 LODIndex, int32 UVChannelIndex, const FVector& Position, const FRotator& Orientation, const FVector2D& Tiling)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return false;
	}

	if (!InternalEditorMeshLibrary::IsUVChannelValid(StaticMesh, LODIndex, UVChannelIndex))
	{
		return false;
	}

	FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription(LODIndex);

	FUVMapParameters UVParameters(Position, Orientation.Quaternion(), StaticMesh->GetBoundingBox().GetSize(), FVector::OneVector, Tiling);

	TMap<FVertexInstanceID, FVector2D> TexCoords;
	FStaticMeshOperations::GenerateCylindricalUV(*MeshDescription, UVParameters, TexCoords);

	return StaticMesh->SetUVChannel(LODIndex, UVChannelIndex, TexCoords);
}

bool UEditorStaticMeshLibrary::GenerateBoxUVChannel(UStaticMesh* StaticMesh, int32 LODIndex, int32 UVChannelIndex, const FVector& Position, const FRotator& Orientation, const FVector& Size)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return false;
	}

	if (!InternalEditorMeshLibrary::IsUVChannelValid(StaticMesh, LODIndex, UVChannelIndex))
	{
		return false;
	}

	FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription(LODIndex);

	FUVMapParameters UVParameters(Position, Orientation.Quaternion(), Size, FVector::OneVector, FVector2D::UnitVector);

	TMap<FVertexInstanceID, FVector2D> TexCoords;
	FStaticMeshOperations::GenerateBoxUV(*MeshDescription, UVParameters, TexCoords);

	return StaticMesh->SetUVChannel(LODIndex, UVChannelIndex, TexCoords);
}

#undef LOCTEXT_NAMESPACE
