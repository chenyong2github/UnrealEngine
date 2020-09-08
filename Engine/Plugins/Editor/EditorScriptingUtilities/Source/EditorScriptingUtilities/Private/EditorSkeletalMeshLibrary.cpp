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

bool UDEPRECATED_EditorSkeletalMeshLibrary::RegenerateLOD(USkeletalMesh* SkeletalMesh, int32 NewLODCount /*= 0*/, bool bRegenerateEvenIfImported /*= false*/, bool bGenerateBaseLOD /*= false*/)
{
	USkeletalMeshEditorSubsystem* SkeletalMeshEditorSubsystem = GEditor->GetEditorSubsystem<USkeletalMeshEditorSubsystem>();

	return SkeletalMeshEditorSubsystem ? SkeletalMeshEditorSubsystem->RegenerateLOD(SkeletalMesh, NewLODCount, bRegenerateEvenIfImported, bGenerateBaseLOD) : false;
}

int32 UDEPRECATED_EditorSkeletalMeshLibrary::GetNumVerts(USkeletalMesh* SkeletalMesh, int32 LODIndex)
{
	USkeletalMeshEditorSubsystem* SkeletalMeshEditorSubsystem = GEditor->GetEditorSubsystem<USkeletalMeshEditorSubsystem>();

	return SkeletalMeshEditorSubsystem ? SkeletalMeshEditorSubsystem->GetNumVerts(SkeletalMesh, LODIndex) : 0;
}

bool UDEPRECATED_EditorSkeletalMeshLibrary::RenameSocket(USkeletalMesh* SkeletalMesh, FName OldName, FName NewName)
{
	USkeletalMeshEditorSubsystem* SkeletalMeshEditorSubsystem = GEditor->GetEditorSubsystem<USkeletalMeshEditorSubsystem>();

	return SkeletalMeshEditorSubsystem ? SkeletalMeshEditorSubsystem->RenameSocket(SkeletalMesh, OldName, NewName) : 0;
}
int32 UDEPRECATED_EditorSkeletalMeshLibrary::GetLODCount(USkeletalMesh* SkeletalMesh)
{
	USkeletalMeshEditorSubsystem* SkeletalMeshEditorSubsystem = GEditor->GetEditorSubsystem<USkeletalMeshEditorSubsystem>();

	return SkeletalMeshEditorSubsystem ? SkeletalMeshEditorSubsystem->GetLODCount(SkeletalMesh) : INDEX_NONE;
}

int32 UDEPRECATED_EditorSkeletalMeshLibrary::ImportLOD(USkeletalMesh* BaseMesh, const int32 LODIndex, const FString& SourceFilename)
{
	USkeletalMeshEditorSubsystem* SkeletalMeshEditorSubsystem = GEditor->GetEditorSubsystem<USkeletalMeshEditorSubsystem>();

	return SkeletalMeshEditorSubsystem ? SkeletalMeshEditorSubsystem->ImportLOD(BaseMesh, LODIndex, SourceFilename) : INDEX_NONE;
}

bool UDEPRECATED_EditorSkeletalMeshLibrary::ReimportAllCustomLODs(USkeletalMesh* SkeletalMesh)
{
	USkeletalMeshEditorSubsystem* SkeletalMeshEditorSubsystem = GEditor->GetEditorSubsystem<USkeletalMeshEditorSubsystem>();

	return SkeletalMeshEditorSubsystem ? SkeletalMeshEditorSubsystem->ReimportAllCustomLODs(SkeletalMesh) : false;
}

void UDEPRECATED_EditorSkeletalMeshLibrary::GetLodBuildSettings(const USkeletalMesh* SkeletalMesh, const int32 LodIndex, FSkeletalMeshBuildSettings& OutBuildOptions)
{
	if (USkeletalMeshEditorSubsystem* SkeletalMeshEditorSubsystem = GEditor->GetEditorSubsystem<USkeletalMeshEditorSubsystem>())
	{
		SkeletalMeshEditorSubsystem->GetLodBuildSettings(SkeletalMesh, LodIndex, OutBuildOptions);
	}
}

void UDEPRECATED_EditorSkeletalMeshLibrary::SetLodBuildSettings(USkeletalMesh* SkeletalMesh, const int32 LodIndex, const FSkeletalMeshBuildSettings& BuildOptions)
{
	if (USkeletalMeshEditorSubsystem* SkeletalMeshEditorSubsystem = GEditor->GetEditorSubsystem<USkeletalMeshEditorSubsystem>())
	{
		SkeletalMeshEditorSubsystem->SetLodBuildSettings(SkeletalMesh, LodIndex, BuildOptions);
	}
}

