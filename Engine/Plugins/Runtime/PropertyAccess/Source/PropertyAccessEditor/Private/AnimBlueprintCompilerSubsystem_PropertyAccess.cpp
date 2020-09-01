// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimBlueprintCompilerSubsystem_PropertyAccess.h"
#include "AnimBlueprintClassSubsystem_PropertyAccess.h"
#include "Animation/AnimBlueprintGeneratedClass.h"

void FPropertyAccessLibraryCompiler_AnimBlueprint::BeginCompilation(UClass* InClass)
{
	Class = InClass;

	UAnimBlueprintClassSubsystem_PropertyAccess* PropertyAccessSubsystem = Cast<UAnimBlueprintClassSubsystem_PropertyAccess>(CastChecked<UAnimBlueprintGeneratedClass>(InClass)->GetSubsystem(UAnimBlueprintClassSubsystem_PropertyAccess::StaticClass()));
	if(PropertyAccessSubsystem)
	{
		Library = &PropertyAccessSubsystem->GetLibrary();
	}
}

int32 UAnimBlueprintCompilerSubsystem_PropertyAccess::AddCopy(TArrayView<FString> InSourcePath, TArrayView<FString> InDestPath, EPropertyAccessBatchType InBatchType, UObject* InObject)
{
	return PropertyAccessLibraryCompiler.AddCopy(InSourcePath, InDestPath, InBatchType, InObject);
}

void UAnimBlueprintCompilerSubsystem_PropertyAccess::StartCompilingClass(UClass* InClass)
{
	PropertyAccessLibraryCompiler.BeginCompilation(InClass);
}

void UAnimBlueprintCompilerSubsystem_PropertyAccess::FinishCompilingClass(UClass* InClass)
{
	OnPreLibraryCompiledDelegate.Broadcast();

	if(!PropertyAccessLibraryCompiler.FinishCompilation())
	{
		PropertyAccessLibraryCompiler.IterateErrors([this](const FText& InErrorText, UObject* InObject)
		{
			// Output any property access errors as warnings
			if(InObject)
			{
				GetMessageLog().Warning(*InErrorText.ToString(), InObject);
			}
			else
			{
				GetMessageLog().Warning(*InErrorText.ToString());
			}
		});
	}

	OnPostLibraryCompiledDelegate.Broadcast();
}

void UAnimBlueprintCompilerSubsystem_PropertyAccess::GetRequiredClassSubsystems(TArray<TSubclassOf<UAnimBlueprintClassSubsystem>>& OutSubsystemClasses) const
{
	OutSubsystemClasses.Add(UAnimBlueprintClassSubsystem_PropertyAccess::StaticClass());
}

int32 UAnimBlueprintCompilerSubsystem_PropertyAccess::MapCopyIndex(int32 InIndex) const
{
	return PropertyAccessLibraryCompiler.MapCopyIndex(InIndex);
}