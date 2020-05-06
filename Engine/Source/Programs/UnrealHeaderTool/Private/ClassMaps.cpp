// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClassMaps.h"
#include "UnrealHeaderTool.h"
#include "UnrealTypeDefinitionInfo.h"

FUnrealSourceFiles GUnrealSourceFilesMap;
FTypeDefinitionInfoMap GTypeDefinitionInfoMap;
TMap<const UPackage*, TArray<UField*>> GPackageSingletons;
FCriticalSection GPackageSingletonsCriticalSection;
FPublicSourceFileSet GPublicSourceFileSet;
TMap<FProperty*, FString> GArrayDimensions;
TMap<UPackage*,  const FManifestModule*> GPackageToManifestModuleMap;
TMap<void*, uint32> GGeneratedCodeHashes;
FRWLock GGeneratedCodeHashesLock;
TMap<UEnum*,  EUnderlyingEnumType> GEnumUnderlyingTypes;
FClassDeclarations GClassDeclarations;
TSet<FProperty*> GUnsizedProperties;
TSet<UField*> GEditorOnlyDataTypes;
TMap<UStruct*, TTuple<TSharedRef<FUnrealSourceFile>, int32>> GStructToSourceLine;
TMap<UClass*, FArchiveTypeDefinePair> GClassSerializerMap;
TSet<FProperty*> GPropertyUsesMemoryImageAllocator;

void FClassDeclarations::AddIfMissing(FName Name, TUniqueFunction<TSharedRef<FClassDeclarationMetaData>()>&& DeclConstructFunc)
{
	FRWScopeLock Lock(ClassDeclLock, SLT_Write);
	if (!ClassDeclarations.Contains(Name))
	{
		TSharedRef<FClassDeclarationMetaData> ClassDecl = DeclConstructFunc();
		ClassDeclarations.Add(Name, MoveTemp(ClassDecl));
	}
}

FClassDeclarationMetaData* FClassDeclarations::Find(FName Name)
{
	FRWScopeLock Lock(ClassDeclLock, SLT_ReadOnly);
	if (TSharedRef<FClassDeclarationMetaData>* ClassDecl = ClassDeclarations.Find(Name))
	{
		return &ClassDecl->Get();
	}
	return nullptr;
}

FClassDeclarationMetaData& FClassDeclarations::FindChecked(FName Name)
{
	FRWScopeLock Lock(ClassDeclLock, SLT_ReadOnly);
	return ClassDeclarations.FindChecked(Name).Get();
}
