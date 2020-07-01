// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorSkeletalMeshLibrary.h"
#include "Editor.h"
#include "EditorScriptingUtils.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMeshSocket.h"
#include "LODUtilities.h"
#include "ScopedTransaction.h"
#include "SkeletalMeshEditorSubsystem.h"

bool UEditorSkeletalMeshLibrary::RegenerateLOD(USkeletalMesh* SkeletalMesh, int32 NewLODCount /*= 0*/, bool bRegenerateEvenIfImported /*= false*/, bool bGenerateBaseLOD /*= false*/)
{
	USkeletalMeshEditorSubsystem* SkeletalMeshEditorSubsystem = GEditor->GetEditorSubsystem<USkeletalMeshEditorSubsystem>();

	return SkeletalMeshEditorSubsystem ? SkeletalMeshEditorSubsystem->RegenerateLOD(SkeletalMesh, NewLODCount, bRegenerateEvenIfImported, bGenerateBaseLOD) : false;
}

int32 UEditorSkeletalMeshLibrary::GetNumVerts(USkeletalMesh* SkeletalMesh, int32 LODIndex)
{
	USkeletalMeshEditorSubsystem* SkeletalMeshEditorSubsystem = GEditor->GetEditorSubsystem<USkeletalMeshEditorSubsystem>();

	return SkeletalMeshEditorSubsystem ? SkeletalMeshEditorSubsystem->GetNumVerts(SkeletalMesh, LODIndex) : 0;
}

bool UEditorSkeletalMeshLibrary::RenameSocket(USkeletalMesh* SkeletalMesh, FName OldName, FName NewName)
{
	USkeletalMeshEditorSubsystem* SkeletalMeshEditorSubsystem = GEditor->GetEditorSubsystem<USkeletalMeshEditorSubsystem>();

	return SkeletalMeshEditorSubsystem ? SkeletalMeshEditorSubsystem->RenameSocket(SkeletalMesh, OldName, NewName) : 0;
}
