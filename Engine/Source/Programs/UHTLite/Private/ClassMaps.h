// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Stack.h"
#include "UObject/ErrorException.h"
#include "UnderlyingEnumType.h"

#include "UnrealSourceFile.h"

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
	None,
	Archive,
	StructuredArchiveRecord
};

struct FArchiveTypeDefinePair
{
	ESerializerArchiveType ArchiveType;
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

	bool Contains(const UField* Field) const
	{
		return DefinitionsByField.Contains(Field);
	}

	const TSharedRef<FUnrealTypeDefinitionInfo>* Find(const UField* Field) const
	{
		return DefinitionsByField.Find(Field);
	}

	const TSharedRef<FUnrealTypeDefinitionInfo>* FindByName(const FName Name) const
	{
		return DefinitionsByName.Find(Name);
	}

	const TSharedRef<FUnrealTypeDefinitionInfo>& operator[](const UField* Field) const
	{
		return DefinitionsByField[Field];
	}

private:

	TMap<UField*, TSharedRef<FUnrealTypeDefinitionInfo>> DefinitionsByField;
	TMap<FName, TSharedRef<FUnrealTypeDefinitionInfo>> DefinitionsByName;
};

// Wrapper class around ClassDeclarations map so we can control access in a threadsafe manner
struct FClassDeclarations
{
	void AddIfMissing(FName Name, TUniqueFunction<TSharedRef<FClassDeclarationMetaData>()>&& DeclConstructFunc);
	FClassDeclarationMetaData* Find(FName Name) const;
	FClassDeclarationMetaData& FindChecked(FName Name) const;

private:
	TMap<FName, TSharedRef<FClassDeclarationMetaData>> ClassDeclarations;

	mutable FRWLock ClassDeclLock;
};

// Wrapper class around SourceFiles map so we can quickly get a list of source files for a given package
struct FUnrealSourceFiles
{
	void Add(FString&& Filename, TSharedRef<FUnrealSourceFile> SourceFile)
	{
		TSharedRef<FUnrealSourceFile>& Value = SourceFilesByString.FindOrAdd(MoveTemp(Filename), SourceFile);
		if (Value != SourceFile)
		{
			FError::Throwf(TEXT("Duplicate filename found with different path '%s'."), *Value.Get().GetFilename());
		}
		SourceFilesByPackage.FindOrAdd(SourceFile->GetPackage()).Add(&SourceFile.Get());
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
