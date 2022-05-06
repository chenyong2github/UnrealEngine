// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteDisplacedMeshFactory.h"
#include "NaniteDisplacedMesh.h"

#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/Selection.h"
#include "Engine/StaticMesh.h"

#define LOCTEXT_NAMESPACE "NaniteDisplacedMeshEditor"

UNaniteDisplacedMeshFactory::UNaniteDisplacedMeshFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UNaniteDisplacedMesh::StaticClass();
}

UNaniteDisplacedMesh* UNaniteDisplacedMeshFactory::StaticFactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return static_cast<UNaniteDisplacedMesh*>(NewObject<UNaniteDisplacedMesh>(InParent, Class, Name, Flags | RF_Transactional | RF_Public | RF_Standalone));
}

UObject* UNaniteDisplacedMeshFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UNaniteDisplacedMesh* NewNaniteDisplacedMesh = StaticFactoryCreateNew(Class, InParent, Name, Flags, Context, Warn);
	NewNaniteDisplacedMesh->MarkPackageDirty();
	return NewNaniteDisplacedMesh;
}

#undef LOCTEXT_NAMESPACE