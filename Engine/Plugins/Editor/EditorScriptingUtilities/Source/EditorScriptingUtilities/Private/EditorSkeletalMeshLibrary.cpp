// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EditorSkeletalMeshLibrary.h"

#include "EditorScriptingUtils.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMeshSocket.h"
#include "LODUtilities.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "EditorSkeletalMeshLibrary"

bool UEditorSkeletalMeshLibrary::RegenerateLOD(USkeletalMesh* SkeletalMesh, int32 NewLODCount /*= 0*/, bool bRegenerateEvenIfImported /*= false*/, bool bGenerateBaseLOD /*= false*/)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::CheckIfInEditorAndPIE())
	{
		return false;
	}

	if (SkeletalMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("RegenerateLOD: The SkeletalMesh is null."));
		return false;
	}

	return FLODUtilities::RegenerateLOD(SkeletalMesh, NewLODCount, bRegenerateEvenIfImported, bGenerateBaseLOD);
}

int32 UEditorSkeletalMeshLibrary::GetNumVerts(USkeletalMesh* SkeletalMesh, int32 LODIndex)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingUtils::CheckIfInEditorAndPIE())
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

bool UEditorSkeletalMeshLibrary::RenameSocket(USkeletalMesh* SkeletalMesh, FName OldName, FName NewName)
{
	if (SkeletalMesh == nullptr)
	{
		UE_LOG(LogEditorScripting, Error, TEXT("RenameSocket: The SkeletalMesh is null."));
		return false;
	}

	if (SkeletalMesh->Skeleton == nullptr)
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

	USkeletalMeshSocket* SkeletonSocket = SkeletalMesh->Skeleton->FindSocket(OldName);
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
	for (int AttachedObjectIndex = 0; AttachedObjectIndex < SkeletalMesh->PreviewAttachedAssetContainer.Num(); ++AttachedObjectIndex)
	{
		FPreviewAttachedObjectPair& Pair = SkeletalMesh->PreviewAttachedAssetContainer[AttachedObjectIndex];
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
	for (int AttachedObjectIndex = 0; AttachedObjectIndex < SkeletalMesh->Skeleton->PreviewAttachedAssetContainer.Num(); ++AttachedObjectIndex)
	{
		FPreviewAttachedObjectPair& Pair = SkeletalMesh->Skeleton->PreviewAttachedAssetContainer[AttachedObjectIndex];
		if (Pair.AttachedTo == OldName)
		{
			// Only modify the skeleton if we actually intend to change something.
			if (!bSkeletonModified)
			{
				SkeletalMesh->Skeleton->Modify();
				bSkeletonModified = true;
			}
			Pair.AttachedTo = NewName;
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
