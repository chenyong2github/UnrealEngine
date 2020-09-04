// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Stack.h"
#include "UObject/ErrorException.h"
#include "UnderlyingEnumType.h"

#include "UnrealSourceFile.h"
#include "UObject/ErrorException.h"

class UField;
class UClass;
class FProperty;
class UPackage;
class UEnum;
class FClassDeclarationMetaData;
class FArchive;
struct FManifestModule;
class FUnrealSourceFile;
class FUnrealTypeDefinitionInfo;

enum class ESerializerArchiveType
{
	None = 0,

	Archive                 = 1,
	StructuredArchiveRecord = 2
};
ENUM_CLASS_FLAGS(ESerializerArchiveType)

struct FArchiveTypeDefinePair
{
	ESerializerArchiveType ArchiveType = ESerializerArchiveType::None;
	FString EnclosingDefine;
};

// Wrapper class around TypeDefinition map so we can maintain a parallel by name map
struct FTypeDefinitionInfoMap
{
	void Add(UField* Field, TSharedRef<FUnrealTypeDefinitionInfo>&& Definition)
	{
		DefinitionsByField.Add(Field, Definition);
		DefinitionsByName.Add(Field->GetFName(), MoveTemp(Definition));
	}
	bool Contains(const UField* Field) { return DefinitionsByField.Contains(Field); }
	TSharedRef<FUnrealTypeDefinitionInfo>* Find(const UField* Field) { return DefinitionsByField.Find(Field); }
	TSharedRef<FUnrealTypeDefinitionInfo>* FindByName(const FName Name) { return DefinitionsByName.Find(Name); }
	TSharedRef<FUnrealTypeDefinitionInfo>& operator[](const UField* Field) { return DefinitionsByField[Field]; }

private:

	TMap<UField*, TSharedRef<FUnrealTypeDefinitionInfo>> DefinitionsByField;
	TMap<FName, TSharedRef<FUnrealTypeDefinitionInfo>> DefinitionsByName;
};

// Wrapper class around ClassDeclarations map so we can control access in a threadsafe manner
struct FClassDeclarations
{
	void AddIfMissing(FName Name, TUniqueFunction<TSharedRef<FClassDeclarationMetaData>()>&& DeclConstructFunc);
	FClassDeclarationMetaData* Find(FName Name);
	FClassDeclarationMetaData& FindChecked(FName Name);

private:
	TMap<FName, TSharedRef<FClassDeclarationMetaData>> ClassDeclarations;

	FRWLock ClassDeclLock;
};

// Wrapper class around SourceFiles map so we can quickly get a list of source files for a given package
struct FUnrealSourceFiles
{
	void AddByHash(uint32 Hash, FString&& Filename, TSharedRef<FUnrealSourceFile> SourceFile)
	{
		SourceFilesByString.AddByHash(Hash, MoveTemp(Filename), MoveTemp(SourceFile));
		SourceFilesByPackage.FindOrAdd(SourceFile->GetPackage()).Add(&SourceFile.Get());
	}
	const TSharedRef<FUnrealSourceFile>* FindByHash(uint32 Hash, const FString& Filename) const
	{
		return SourceFilesByString.FindByHash(Hash, Filename);
	}
	const TSharedRef<FUnrealSourceFile>* Find(const FString& Id) const { return SourceFilesByString.Find(Id); }
	const TArray<FUnrealSourceFile*>* FindFilesForPackage(const UPackage* Package) const { return SourceFilesByPackage.Find(Package); }

private:
	// A map of all source files indexed by string.
	TMap<FString, TSharedRef<FUnrealSourceFile>> SourceFilesByString;

	// The list of source files per package. Stored as raw pointer since SourceFilesByString holds shared ref.
	TMap<UPackage*, TArray<FUnrealSourceFile*>> SourceFilesByPackage;
};

// Wrapper class around PublicSourceFile set so we can quickly get a list of source files for a given package
struct FPublicSourceFileSet
{
	void Add(FUnrealSourceFile* SourceFile)
	{
		SourceFileSet.Add(SourceFile);
		SourceFilesByPackage.FindOrAdd(SourceFile->GetPackage()).Add(SourceFile);
	}
	bool Contains(FUnrealSourceFile* SourceFile) const { return SourceFileSet.Contains(SourceFile); }
	const TArray<FUnrealSourceFile*>* FindFilesForPackage(const UPackage* Package) const { return SourceFilesByPackage.Find(Package); }

private:

	// The set of all public source files. Stored as raw pointer since FUnrealSourceFiles::SourceFilesByString holds shared ref.
	TSet<FUnrealSourceFile*> SourceFileSet;

	// The list of public source files per package. Stored as raw pointer since FUnrealSourceFiles::SourceFilesByString holds shared ref.
	TMap<UPackage*, TArray<FUnrealSourceFile*>> SourceFilesByPackage;
};

extern FUnrealSourceFiles GUnrealSourceFilesMap;
extern FTypeDefinitionInfoMap GTypeDefinitionInfoMap;
extern TMap<const UPackage*, TArray<UField*>> GPackageSingletons;
extern FCriticalSection GPackageSingletonsCriticalSection;
extern FPublicSourceFileSet GPublicSourceFileSet;
extern TMap<FProperty*, FString> GArrayDimensions;
extern TMap<UPackage*,  const FManifestModule*> GPackageToManifestModuleMap;
extern TMap<void*, uint32> GGeneratedCodeHashes;
extern FRWLock GGeneratedCodeHashesLock;
extern TMap<UEnum*, EUnderlyingEnumType> GEnumUnderlyingTypes;
extern FClassDeclarations GClassDeclarations;
extern TSet<FProperty*> GUnsizedProperties;
extern TSet<UField*> GEditorOnlyDataTypes;
extern TMap<UStruct*, TTuple<TSharedRef<FUnrealSourceFile>, int32>> GStructToSourceLine;
extern TMap<UClass*, FArchiveTypeDefinePair> GClassSerializerMap;
extern TSet<FProperty*> GPropertyUsesMemoryImageAllocator;

/** Types access specifiers. */
enum EAccessSpecifier
{
	ACCESS_NotAnAccessSpecifier = 0,
	ACCESS_Public,
	ACCESS_Private,
	ACCESS_Protected,
	ACCESS_Num,
};

inline FArchive& operator<<(FArchive& Ar, EAccessSpecifier& ObjectType)
{
	if (Ar.IsLoading())
	{
		int32 Value;
		Ar << Value;
		ObjectType = EAccessSpecifier(Value);
	}
	else if (Ar.IsSaving())
	{
		int32 Value = (int32)ObjectType;
		Ar << Value;
	}

	return Ar;
}
