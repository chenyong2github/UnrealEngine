// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimBlueprintCompilerHandler_PropertyAccess.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "IAnimBlueprintGeneratedClassCompiledData.h"
#include "IAnimBlueprintCompilerCreationContext.h"
#include "IAnimBlueprintCompilationContext.h"
#include "IAnimBlueprintCompilationBracketContext.h"

FAnimBlueprintCompilerHandler_PropertyAccess::FAnimBlueprintCompilerHandler_PropertyAccess(IAnimBlueprintCompilerCreationContext& InCreationContext)
{
	InCreationContext.OnStartCompilingClass().AddRaw(this, &FAnimBlueprintCompilerHandler_PropertyAccess::StartCompilingClass);
	InCreationContext.OnFinishCompilingClass().AddRaw(this, &FAnimBlueprintCompilerHandler_PropertyAccess::FinishCompilingClass);
}

int32 FAnimBlueprintCompilerHandler_PropertyAccess::AddCopy(TArrayView<FString> InSourcePath, TArrayView<FString> InDestPath, EPropertyAccessBatchType InBatchType, UObject* InObject)
{
	return PropertyAccessLibraryCompiler.AddCopy(InSourcePath, InDestPath, InBatchType, InObject);
}

void FAnimBlueprintCompilerHandler_PropertyAccess::StartCompilingClass(const UClass* InClass, IAnimBlueprintCompilationBracketContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData)
{
	PropertyAccessLibraryCompiler.Setup(InClass, &OutCompiledData.GetPropertyAccessLibrary());
	PropertyAccessLibraryCompiler.BeginCompilation(InClass);
}

void FAnimBlueprintCompilerHandler_PropertyAccess::FinishCompilingClass(const UClass* InClass, IAnimBlueprintCompilationBracketContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData)
{
	OnPreLibraryCompiledDelegate.Broadcast();

	if(!PropertyAccessLibraryCompiler.FinishCompilation())
	{
		PropertyAccessLibraryCompiler.IterateErrors([&InCompilationContext](const FText& InErrorText, UObject* InObject)
		{
			// Output any property access errors as warnings
			if(InObject)
			{
				InCompilationContext.GetMessageLog().Warning(*InErrorText.ToString(), InObject);
			}
			else
			{
				InCompilationContext.GetMessageLog().Warning(*InErrorText.ToString());
			}
		});
	}

	OnPostLibraryCompiledDelegate.Broadcast(OutCompiledData);
}

int32 FAnimBlueprintCompilerHandler_PropertyAccess::MapCopyIndex(int32 InIndex) const
{
	return PropertyAccessLibraryCompiler.MapCopyIndex(InIndex);
}