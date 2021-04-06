// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorSkeletalMeshLibrary.h"

#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkinnedMeshComponent.h"
#include "Editor.h"
#include "EditorFramework/AssetImportData.h"
#include "EditorScriptingUtils.h"
#include "Engine/EngineTypes.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "FbxMeshUtils.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "LODUtilities.h"
#include "Misc/CoreMisc.h"
#include "ObjectTools.h"
#include "PhysicsAssetUtils.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "ScopedTransaction.h"
#include "SkeletalMeshTypes.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Subsystems/ImportSubsystem.h"

#define LOCTEXT_NAMESPACE "EditorSkeletalMeshLibrary"

bool UEditorSkeletalMeshLibrary::RegenerateLOD(USkeletalMesh* SkeletalMesh, int32 NewLODCount /*= 0*/, bool bRegenerateEvenIfImported /*= false*/, bool bGenerateBaseLOD /*= false*/)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return false;
	}

	if (SkeletalMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("RegenerateLOD: The SkeletalMesh is null."));
		return false;
	}

	return FLODUtilities::RegenerateLOD(SkeletalMesh, GetTargetPlatformManagerRef().GetRunningTargetPlatform(), NewLODCount, bRegenerateEvenIfImported, bGenerateBaseLOD);
}

int32 UEditorSkeletalMeshLibrary::GetNumVerts(USkeletalMesh* SkeletalMesh, int32 LODIndex)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return 0;
	}

	if (SkeletalMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("GetNumVerts: The SkeletalMesh is null."));
		return 0;
	}

	if (SkeletalMesh->GetResourceForRendering() && SkeletalMesh->GetResourceForRendering()->LODRenderData.Num() > 0)
	{
		TIndirectArray<FSkeletalMeshLODRenderData>& LodRenderData = SkeletalMesh->GetResourceForRendering()->LODRenderData;
		if (LodRenderData.IsValidIndex(LODIndex))
		{
			return LodRenderData[LODIndex].GetNumVertices();
		}
	}

	return 0;
}

int32 UEditorSkeletalMeshLibrary::GetNumSections( USkeletalMesh* SkeletalMesh, int32 LODIndex )
{
	TGuardValue<bool> UnattendedScriptGuard( GIsRunningUnattendedScript, true );

	if ( !EditorScriptingUtils::IsInEditorAndNotPlaying() )
	{
		return INDEX_NONE;
	}

	if ( SkeletalMesh == nullptr )
	{
		UE_LOG( LogEditorScripting, Error, TEXT( "GetNumSections: The SkeletalMesh is null." ) );
		return INDEX_NONE;
	}

	if ( SkeletalMesh->GetResourceForRendering() && SkeletalMesh->GetResourceForRendering()->LODRenderData.Num() > 0 )
	{
		TIndirectArray<FSkeletalMeshLODRenderData>& LodRenderData = SkeletalMesh->GetResourceForRendering()->LODRenderData;
		if ( LodRenderData.IsValidIndex( LODIndex ) )
		{
			return LodRenderData[ LODIndex ].RenderSections.Num();
		}
	}

	return INDEX_NONE;
}

int32 UEditorSkeletalMeshLibrary::GetLODMaterialSlot( USkeletalMesh* SkeletalMesh, int32 LODIndex, int32 SectionIndex )
{
	TGuardValue<bool> UnattendedScriptGuard( GIsRunningUnattendedScript, true );

	if ( !EditorScriptingUtils::IsInEditorAndNotPlaying() )
	{
		return INDEX_NONE;
	}

	if ( SkeletalMesh == nullptr )
	{
		UE_LOG( LogEditorScripting, Error, TEXT( "GetLODMaterialSlot: The SkeletalMesh is null." ) );
		return INDEX_NONE;
	}

	if ( SkeletalMesh->GetResourceForRendering() && SkeletalMesh->GetResourceForRendering()->LODRenderData.Num() > 0 )
	{
		TIndirectArray<FSkeletalMeshLODRenderData>& LodRenderData = SkeletalMesh->GetResourceForRendering()->LODRenderData;
		if ( LodRenderData.IsValidIndex( LODIndex ) )
		{
			const TArray<FSkelMeshRenderSection>& Sections = LodRenderData[ LODIndex ].RenderSections;
			if ( Sections.IsValidIndex( SectionIndex ) )
			{
				int32 MaterialIndex = Sections[ SectionIndex ].MaterialIndex;

				const FSkeletalMeshLODInfo* LODInfo = SkeletalMesh->GetLODInfo( LODIndex );
				if ( LODInfo && LODInfo->LODMaterialMap.IsValidIndex( SectionIndex ) )
				{
					// If we have an optional LODMaterialMap, we need to reroute the material index through it
					MaterialIndex = LODInfo->LODMaterialMap[ SectionIndex ];
				}

				return MaterialIndex;
			}
			else
			{
				UE_LOG( LogEditorScripting, Error, TEXT( "GetLODMaterialSlot: Invalid SectionIndex." ) );
			}
		}
		else
		{
			UE_LOG( LogEditorScripting, Error, TEXT( "GetLODMaterialSlot: Invalid LODIndex." ) );
		}
	}

	return INDEX_NONE;
}

bool UEditorSkeletalMeshLibrary::RenameSocket(USkeletalMesh* SkeletalMesh, FName OldName, FName NewName)
{
	if (SkeletalMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("RenameSocket: The SkeletalMesh is null."));
		return false;
	}

	if (SkeletalMesh->GetSkeleton() == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("RenameSocket: The SkeletalMesh's Skeleton is null."));
		return false;
	}

	if (OldName == NAME_None)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("RenameSocket: The OldName is None."));
		return false;
	}

	if (NewName == NAME_None)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("RenameSocket: The NewName is None."));
		return false;
	}

	if (OldName == NewName)
	{
		return false;
	}

	USkeletalMeshSocket* MeshSocket = SkeletalMesh->FindSocket(OldName);
	if (MeshSocket == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("RenameSocket: The socket named '%s' does not exist on the SkeletalMesh."), *OldName.ToString());
		return false;
	}

	USkeletalMeshSocket* SkeletonSocket = SkeletalMesh->GetSkeleton()->FindSocket(OldName);
	if (SkeletonSocket == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("RenameSocket: The socket named '%s' does not exist on the Skeleton."), *OldName.ToString());
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("RenameSocket", "Rename Socket"));
	MeshSocket->SetFlags(RF_Transactional);
	MeshSocket->Modify();
	MeshSocket->SocketName = NewName;

	SkeletonSocket->SetFlags(RF_Transactional);
	SkeletonSocket->Modify();
	SkeletonSocket->SocketName = NewName;

	bool bMeshModified = false;
	for (int AttachedObjectIndex = 0; AttachedObjectIndex < SkeletalMesh->GetPreviewAttachedAssetContainer().Num(); ++AttachedObjectIndex)
	{
		FPreviewAttachedObjectPair& Pair = SkeletalMesh->GetPreviewAttachedAssetContainer()[AttachedObjectIndex];
		if (Pair.AttachedTo == OldName)
		{
			// Only modify the mesh if we actually intend to change something. Avoids dirtying
			// meshes when we don't actually update any data on them. (such as adding a new socket)
			if (!bMeshModified)
			{
				SkeletalMesh->Modify();
				bMeshModified = true;
			}
			Pair.AttachedTo = NewName;
		}
	}

	bool bSkeletonModified = false;
	for (int AttachedObjectIndex = 0; AttachedObjectIndex < SkeletalMesh->GetSkeleton()->PreviewAttachedAssetContainer.Num(); ++AttachedObjectIndex)
	{
		FPreviewAttachedObjectPair& Pair = SkeletalMesh->GetSkeleton()->PreviewAttachedAssetContainer[AttachedObjectIndex];
		if (Pair.AttachedTo == OldName)
		{
			// Only modify the skeleton if we actually intend to change something.
			if (!bSkeletonModified)
			{
				SkeletalMesh->GetSkeleton()->Modify();
				bSkeletonModified = true;
			}
			Pair.AttachedTo = NewName;
		}
	}

	return true;
}

int32 UEditorSkeletalMeshLibrary::GetLODCount(USkeletalMesh* SkeletalMesh)
{
	if (SkeletalMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("SkeletalMesh GetLODCount: The SkeletalMesh is null."));
		return INDEX_NONE;
	}

	return SkeletalMesh->GetLODNum();
}

int32 UEditorSkeletalMeshLibrary::ImportLOD(USkeletalMesh* BaseMesh, const int32 LODIndex, const FString& SourceFilename)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		UE_LOG(LogEditorScripting, Error, TEXT("SkeletalMesh ImportLOD: Cannot import or re-import when editor PIE is active."));
		return INDEX_NONE;
	}

	if (BaseMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("SkeletalMesh ImportLOD: The SkeletalMesh is null."));
		return INDEX_NONE;
	}

	// Make sure the LODIndex we want to add the LOD is valid
	if (BaseMesh->GetLODNum() < LODIndex)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("SkeletalMesh ImportLOD: Invalid LODIndex, the LOD index cannot be greater the the number of LOD, skeletal mesh cannot have hole in the LOD array."));
		return INDEX_NONE;
	}

	FString ResolveFilename = SourceFilename;
	const bool bSourceFileExists = FPaths::FileExists(ResolveFilename);
	if (!bSourceFileExists)
	{
		if (BaseMesh->IsValidLODIndex(LODIndex))
		{
			ResolveFilename = BaseMesh->GetLODInfo(LODIndex)->SourceImportFilename.IsEmpty() ?
				BaseMesh->GetLODInfo(LODIndex)->SourceImportFilename :
				UAssetImportData::ResolveImportFilename(BaseMesh->GetLODInfo(LODIndex)->SourceImportFilename, nullptr);
		}
	}

	if (!FPaths::FileExists(ResolveFilename))
	{
		UE_LOG(LogEditorScripting, Error, TEXT("SkeletalMesh ImportLOD: Invalid source filename."));
		return INDEX_NONE;
	}

	//We want to remove the Reduction this LOD if the following conditions are met
	if (BaseMesh->IsValidLODIndex(LODIndex) //Only test valid LOD, this function can add LODs
		&& BaseMesh->IsReductionActive(LODIndex) //We remove reduction settings only if they are active
		&& BaseMesh->GetLODInfo(LODIndex) //this test is redundant (IsReductionActive test this), but to avoid static analysis
		&& BaseMesh->GetLODInfo(LODIndex)->ReductionSettings.BaseLOD < LODIndex //We do not remove the reduction if the reduction is base on this LOD imported data
		&& (!BaseMesh->GetLODSettings() || BaseMesh->GetLODSettings()->GetNumberOfSettings() < LODIndex)) //We do not remove the reduction if the skeletal mesh is using a LODSettings for this LOD
	{
		//Remove the reduction settings
		BaseMesh->GetLODInfo(LODIndex)->ReductionSettings.NumOfTrianglesPercentage = 1.0f;
		BaseMesh->GetLODInfo(LODIndex)->ReductionSettings.NumOfVertPercentage = 1.0f;
		BaseMesh->GetLODInfo(LODIndex)->ReductionSettings.MaxNumOfTrianglesPercentage = MAX_uint32;
		BaseMesh->GetLODInfo(LODIndex)->ReductionSettings.MaxNumOfVertsPercentage = MAX_uint32;
		BaseMesh->GetLODInfo(LODIndex)->ReductionSettings.TerminationCriterion = SMTC_NumOfTriangles;

		BaseMesh->GetLODInfo(LODIndex)->bHasBeenSimplified = false;
	}

	if (!FbxMeshUtils::ImportSkeletalMeshLOD(BaseMesh, ResolveFilename, LODIndex))
	{
		UE_LOG(LogEditorScripting, Error, TEXT("SkeletalMesh ImportLOD: Cannot import mesh LOD."));
		return INDEX_NONE;
	}

	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostLODImport(BaseMesh, LODIndex);

	return LODIndex;
}

bool UEditorSkeletalMeshLibrary::ReimportAllCustomLODs(USkeletalMesh* SkeletalMesh)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		UE_LOG(LogEditorScripting, Error, TEXT("SkeletalMesh ReimportAllCustomLODs: Cannot import or re-import when editor PIE is active."));
		return false;
	}

	if (SkeletalMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("SkeletalMesh ReimportAllCustomLODs: The SkeletalMesh is null."));
		return false;
	}

	bool bResult = true;
	int32 LODNumber = SkeletalMesh->GetLODNum();
	//Iterate the static mesh LODs, start at index 1
	for (int32 LODIndex = 1; LODIndex < LODNumber; ++LODIndex)
	{
		//Do not reimport LOD that was re-import with the base mesh
		if (SkeletalMesh->GetLODInfo(LODIndex)->bImportWithBaseMesh)
		{
			continue;
		}
		if (SkeletalMesh->GetLODInfo(LODIndex)->bHasBeenSimplified)
		{
			 continue;
		}

		if (ImportLOD(SkeletalMesh, LODIndex, SkeletalMesh->GetLODInfo(LODIndex)->SourceImportFilename) != LODIndex)
		{
			UE_LOG(LogEditorScripting, Error, TEXT("SkeletalMesh ReimportAllCustomLODs: Cannot re-import LOD %d."), LODIndex);
			bResult = false;
		}
	}
	return bResult;
}

void UEditorSkeletalMeshLibrary::GetLodBuildSettings(const USkeletalMesh* SkeletalMesh, const int32 LodIndex, FSkeletalMeshBuildSettings& OutBuildOptions)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return;
	}

	if (SkeletalMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("GetLodReductionSettings: The SkeletalMesh is null."));
		return;
	}

	// If LOD 0 does not exist, warn and return
	if (LodIndex < 0 || SkeletalMesh->GetLODNum() <= LodIndex)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("GetLodReductionSettings: Invalid LOD index."));
		return;
	}

	const FSkeletalMeshLODInfo* LODInfo = SkeletalMesh->GetLODInfo(LodIndex);
	//LodIndex validity was verify before
	check(LODInfo);

	// Copy over the reduction settings
	OutBuildOptions = LODInfo->BuildSettings;
}

void UEditorSkeletalMeshLibrary::SetLodBuildSettings(USkeletalMesh* SkeletalMesh, const int32 LodIndex, const FSkeletalMeshBuildSettings& BuildOptions)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::IsInEditorAndNotPlaying())
	{
		return;
	}

	if (SkeletalMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("SetLodReductionSettings: The SkeletalMesh is null."));
		return;
	}

	// If LOD 0 does not exist, warn and return
	if (LodIndex < 0 || SkeletalMesh->GetLODNum() <= LodIndex)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("GetLodReductionSettings: Invalid LOD index."));
		return;
	}

	FSkeletalMeshLODInfo* LODInfo = SkeletalMesh->GetLODInfo(LodIndex);
	//LodIndex validity was verify before
	check(LODInfo);

	// Close the mesh editor to prevent crashing. If changes are applied, reopen it after the mesh has been built.
	bool bSkeletalMeshIsEdited = false;
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (AssetEditorSubsystem->FindEditorForAsset(SkeletalMesh, false))
	{
		AssetEditorSubsystem->CloseAllEditorsForAsset(SkeletalMesh);
		bSkeletalMeshIsEdited = true;
	}

	//Copy the reduction setting on the LODInfo
	{
		FScopedSkeletalMeshPostEditChange ScopedSkeletalMeshPostEditChange(SkeletalMesh);
		SkeletalMesh->Modify();

		// Copy over the reduction settings
		LODInfo->BuildSettings = BuildOptions;
	}
	// Reopen MeshEditor on this mesh if the MeshEditor was previously opened in it
	if (bSkeletalMeshIsEdited)
	{
		AssetEditorSubsystem->OpenEditorForAsset(SkeletalMesh);
	}
}

bool UEditorSkeletalMeshLibrary::RemoveLODs(USkeletalMesh* SkeletalMesh, TArray<int32> ToRemoveLODs)
{
	FSkeletalMeshUpdateContext UpdateContext;
	UpdateContext.SkeletalMesh = SkeletalMesh;
	int32 OriginalLODNumber = SkeletalMesh->GetLODNum();

	// Close the mesh editor to be sure the editor is showing the correct data after the LODs are removed.
	bool bMeshIsEdited = false;
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (AssetEditorSubsystem->FindEditorForAsset(SkeletalMesh, false))
	{
		AssetEditorSubsystem->CloseAllEditorsForAsset(SkeletalMesh);
		bMeshIsEdited = true;
	}

	// Now iterate over all skeletal mesh components to add them to the UpdateContext
	for (TObjectIterator<USkinnedMeshComponent> It; It; ++It)
	{
		USkinnedMeshComponent* SkinComp = *It;
		if (SkinComp->SkeletalMesh == SkeletalMesh)
		{
			UpdateContext.AssociatedComponents.Add(SkinComp);
		}
	}

	FLODUtilities::RemoveLODs(UpdateContext, ToRemoveLODs);

	if (bMeshIsEdited)
	{
		AssetEditorSubsystem->OpenEditorForAsset(SkeletalMesh);
	}

	int32 FinalLODNumber = SkeletalMesh->GetLODNum();
	return (OriginalLODNumber-FinalLODNumber == ToRemoveLODs.Num());
}

bool UEditorSkeletalMeshLibrary::StripLODGeometry(USkeletalMesh* SkeletalMesh, const int32 LODIndex, UTexture2D* TextureMask, const float Threshold)
{
	return FLODUtilities::StripLODGeometry(SkeletalMesh, LODIndex, TextureMask, Threshold);
}

UPhysicsAsset* UEditorSkeletalMeshLibrary::CreatePhysicsAsset(USkeletalMesh* SkeletalMesh)
{
	if (!SkeletalMesh)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("CreatePhysicsAsset failed: The SkeletalMesh is null."));
		return nullptr;
	}

	FString ObjectName = FString::Printf(TEXT("%s_PhysicsAsset"), *SkeletalMesh->GetName());
	FString PackageName = SkeletalMesh->GetOutermost()->GetName();

	FString ParentPath = FString::Printf(TEXT("%s/%s"), *FPackageName::GetLongPackagePath(*PackageName), *ObjectName);
	UObject* Package = CreatePackage(*ParentPath);

	// See if an object with this name exists
	UObject* Object = LoadObject<UObject>(Package, *ObjectName, nullptr, LOAD_NoWarn | LOAD_Quiet, nullptr);

	// If an object with same name but different class exists, fail and warn the user
	if ((Object != nullptr) && (Object->GetClass() != UPhysicsAsset::StaticClass()))
	{
		UE_LOG(LogEditorScripting, Error, TEXT("CreatePhysicsAsset failed: An object that is not a Physics Asset already exists with the name %s."), *ObjectName);
		return nullptr;
	}

	if (Object == nullptr)
	{
		Object = NewObject<UPhysicsAsset>(Package, *ObjectName, RF_Public | RF_Standalone);

		FAssetRegistryModule::AssetCreated(Object);
	}

	UPhysicsAsset* NewPhysicsAsset = Cast<UPhysicsAsset>(Object);
	if (NewPhysicsAsset)
	{
		NewPhysicsAsset->MarkPackageDirty();

		FPhysAssetCreateParams NewBodyData;
		FText CreationErrorMessage;

		bool bSuccess = FPhysicsAssetUtils::CreateFromSkeletalMesh(NewPhysicsAsset, SkeletalMesh, NewBodyData, CreationErrorMessage);
		if (!bSuccess)
		{
			UE_LOG(LogEditorScripting, Error, TEXT("CreatePhysicsAsset failed: Couldn't create PhysicsAsset for the SkeletalMesh."));
			ObjectTools::DeleteObjects({NewPhysicsAsset}, false);
			return nullptr;
		}
		else
		{
			RefreshSkelMeshOnPhysicsAssetChange(SkeletalMesh);
		}
	}

	return NewPhysicsAsset;
}

#undef LOCTEXT_NAMESPACE
