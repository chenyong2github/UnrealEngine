// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/DeferredRegistry.h"
#include "UObject/UObjectHash.h"
#include "Templates/Casts.h"

#if WITH_RELOAD

void ReloadRenameObject(UScriptStruct& Object, const TCHAR* RenamePrefix)
{
	// Make sure the old struct is not used by anything
	Object.ClearFlags(RF_Standalone | RF_Public);
	Object.RemoveFromRoot();
	const FName OldRename = MakeUniqueObjectName(GetTransientPackage(), Object.GetClass(), *FString::Printf(TEXT("%s_%s"), RenamePrefix, *Object.GetName()));
	Object.Rename(*OldRename.ToString(), GetTransientPackage());
}

void ReloadRenameObject(UEnum& Object, const TCHAR* RenamePrefix)
{
	// Make sure the old struct is not used by anything
	Object.ClearFlags(RF_Standalone | RF_Public);
	Object.RemoveFromRoot();
	const FName OldRename = MakeUniqueObjectName(GetTransientPackage(), Object.GetClass(), *FString::Printf(TEXT("%s_%s"), RenamePrefix, *Object.GetName()));
	Object.Rename(*OldRename.ToString(), GetTransientPackage());

	Object.RemoveNamesFromMasterList();
}

void ReloadRenameObject(UClass& Class, const TCHAR* RenamePrefix)
{
	FString NameWithoutPrefix = UObjectBase::RemoveClassPrefix(*Class.GetName());

	// Rename the old class and move it to transient package
	Class.RemoveFromRoot();
	Class.ClearFlags(RF_Standalone | RF_Public);
	Class.GetDefaultObject()->RemoveFromRoot();
	Class.GetDefaultObject()->ClearFlags(RF_Standalone | RF_Public);
	const FName OldClassRename = MakeUniqueObjectName(GetTransientPackage(), Class.GetClass(), *FString::Printf(TEXT("%s_%s"), RenamePrefix, *NameWithoutPrefix));
	Class.Rename(*OldClassRename.ToString(), GetTransientPackage());
	Class.SetFlags(RF_Transient);
	Class.AddToRoot();

	// Make sure enums de-register their names BEFORE we create the new class, otherwise there will be name conflicts
	TArray<UObject*> ClassSubobjects;
	GetObjectsWithOuter(&Class, ClassSubobjects);
	for (auto ClassSubobject : ClassSubobjects)
	{
		if (auto Enum = dynamic_cast<UEnum*>(ClassSubobject))
		{
			Enum->RemoveNamesFromMasterList();
		}
	}

	// Reset singletons for any child fields
#if WITH_LIVE_CODING
	for (UField* Field = Class.Children; Field != nullptr; Field = Field->Next)
	{
		UFunction* Function = Cast<UFunction>(Field);
		if (Function != nullptr && Function->SingletonPtr != nullptr)
		{
			*Function->SingletonPtr = nullptr;
		}
	}
#endif
}

#endif // WITH_RELOAD
