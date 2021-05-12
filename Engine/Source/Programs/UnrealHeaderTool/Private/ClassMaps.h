// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Stack.h"
#include "UObject/ErrorException.h"

#include "UnrealSourceFile.h"
#include "UObject/ErrorException.h"
#include "UnrealTypeDefinitionInfo.h"

class UField;
class UClass;
class FProperty;
class UPackage;
class UEnum;
class FArchive;
struct FManifestModule;
class FUnrealSourceFile;
class FUnrealTypeDefinitionInfo;

// Helper class to support freezing of the container
struct FFreezableContainer
{
public:
	void Freeze()
	{
		bFrozen = true;
	}

protected:
	bool bFrozen = false;
};

// Wrapper class around TypeDefinition map so we can maintain a parallel by name map
struct FTypeDefinitionInfoMap : public FFreezableContainer
{

	//NOTE: UObjects are frozen after the preparsing phase
	void Add(UObject* Object, TSharedRef<FUnrealTypeDefinitionInfo>&& Definition)
	{
		check(!bFrozen);
		DefinitionsByUObject.Add(Object, Definition);
		DefinitionsByName.Add(Object->GetFName(), MoveTemp(Definition));
	}

	void Add(UObject* Object, TSharedRef<FUnrealTypeDefinitionInfo>& Definition)
	{
		check(!bFrozen);
		DefinitionsByUObject.Add(Object, Definition);
		DefinitionsByName.Add(Object->GetFName(), Definition);
	}

	bool Contains(const UObject* Object) { check(bFrozen); return DefinitionsByUObject.Contains(Object); }
	TSharedRef<FUnrealTypeDefinitionInfo>* Find(const UObject* Object) { check(bFrozen); return DefinitionsByUObject.Find(Object); }
	TSharedRef<FUnrealTypeDefinitionInfo>& operator[](const UObject* Object) { check(bFrozen); return DefinitionsByUObject[Object]; }
	FUnrealTypeDefinitionInfo& FindChecked(const UObject* Object)
	{
		check(bFrozen);
		TSharedRef<FUnrealTypeDefinitionInfo>* TypeDef = DefinitionsByUObject.Find(Object);
		check(TypeDef);
		return **TypeDef;
	}
	TSharedRef<FUnrealTypeDefinitionInfo>* FindByName(const FName Name) { check(bFrozen); return DefinitionsByName.Find(Name); }
	FUnrealTypeDefinitionInfo& FindByNameChecked(const FName Name)
	{ 
		check(bFrozen); 
		TSharedRef<FUnrealTypeDefinitionInfo>* TypeDef = DefinitionsByName.Find(Name);
		check(TypeDef);
		return **TypeDef;
	}

	template <typename Lambda>
	void ForAllTypes(Lambda&& InLambda)
	{
		for (const TPair<UObject*, TSharedRef<FUnrealTypeDefinitionInfo>>& KVP : DefinitionsByUObject)
		{
			InLambda(*KVP.Value);
		}
	}

	//NOTE: Currently UFunctions are created during the parsing phase and can not be frozen
	void Add(UFunction* Object, TSharedRef<FUnrealTypeDefinitionInfo>&& Definition)
	{
		DefinitionsByUObject.Add(Object, Definition);
		// At this point, do not add the name
		//DefinitionsByName.Add(Object->GetFName(), Definition);
	}

	//NOTE: FFields (properties) are not frozen since they are added during the parsing phase
	void Add(FField* Field, TSharedRef<FUnrealTypeDefinitionInfo>&& Definition)
	{
		DefinitionsByFField.Add(Field, MoveTemp(Definition));
	}
	bool Contains(const FField* Field) { return DefinitionsByFField.Contains(Field); }
	TSharedRef<FUnrealTypeDefinitionInfo>* Find(const FField* Field)
	{
		return DefinitionsByFField.Find(Field);
	}
	FUnrealTypeDefinitionInfo& FindChecked(const FField* Field)
	{
		TSharedRef<FUnrealTypeDefinitionInfo>* TypeDef = DefinitionsByFField.Find(Field);
		check(TypeDef);
		return **TypeDef;
	}
	TSharedRef<FUnrealTypeDefinitionInfo>& operator[](const FField* Field) { return DefinitionsByFField[Field]; }

private:

	TMap<UObject*, TSharedRef<FUnrealTypeDefinitionInfo>> DefinitionsByUObject;
	TMap<FField*, TSharedRef<FUnrealTypeDefinitionInfo>> DefinitionsByFField;
	TMap<FName, TSharedRef<FUnrealTypeDefinitionInfo>> DefinitionsByName;
};

// Wrapper class around SourceFiles map so we can quickly get a list of source files for a given package
struct FUnrealSourceFiles : public FFreezableContainer
{
	TSharedRef<FUnrealSourceFile>* AddByHash(uint32 Hash, FString&& Filename, TSharedRef<FUnrealSourceFile> SourceFile)
	{
		check(!bFrozen);
		TSharedRef<FUnrealSourceFile>* Existing = SourceFilesByString.FindByHash(Hash, Filename);
		AllSourceFiles.Add(&SourceFile.Get());
		SourceFilesByString.AddByHash(Hash, MoveTemp(Filename), MoveTemp(SourceFile));
		return Existing;
	}
	const TSharedRef<FUnrealSourceFile>* Find(const FString& Id) const 
	{
		check(bFrozen);
		return SourceFilesByString.Find(Id);
	}
	const TArray<FUnrealSourceFile*>& GetAllSourceFiles() const
	{
		check(bFrozen);
		return AllSourceFiles;
	}

private:
	// A map of all source files indexed by string.
	TMap<FString, TSharedRef<FUnrealSourceFile>> SourceFilesByString;

	// Total collection of sources
	TArray<FUnrealSourceFile*> AllSourceFiles;
};

extern FUnrealSourceFiles GUnrealSourceFilesMap;
extern FTypeDefinitionInfoMap GTypeDefinitionInfoMap;
