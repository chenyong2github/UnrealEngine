// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimBlueprintExtension_PropertyAccess.h"
#include "IPropertyAccessEditor.h"
#include "Features/IModularFeatures.h"
#include "IPropertyAccessCompiler.h"
#include "IAnimBlueprintCompilationBracketContext.h"
#include "Kismet2/CompilerResultsLog.h"

int32 UAnimBlueprintExtension_PropertyAccess::AddCopy(TArrayView<FString> InSourcePath, TArrayView<FString> InDestPath, EPropertyAccessBatchType InBatchType, UObject* InObject)
{
	if(PropertyAccessLibraryCompiler.IsValid())
	{
		return PropertyAccessLibraryCompiler->AddCopy(InSourcePath, InDestPath, InBatchType, InObject);
	}
	return INDEX_NONE;
}

void UAnimBlueprintExtension_PropertyAccess::HandleStartCompilingClass(const UClass* InClass, IAnimBlueprintCompilationBracketContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData)
{
	IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");
	
	PropertyAccessLibraryCompiler = PropertyAccessEditor.MakePropertyAccessCompiler(FPropertyAccessLibraryCompilerArgs(Subsystem.Library, InClass));
	PropertyAccessLibraryCompiler->BeginCompilation();
}

void UAnimBlueprintExtension_PropertyAccess::HandleFinishCompilingClass(const UClass* InClass, IAnimBlueprintCompilationBracketContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData)
{
	OnPreLibraryCompiledDelegate.Broadcast();

	if(!PropertyAccessLibraryCompiler->FinishCompilation())
	{
		PropertyAccessLibraryCompiler->IterateErrors([&InCompilationContext](const FText& InErrorText, UObject* InObject)
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

	OnPostLibraryCompiledDelegate.Broadcast(InCompilationContext, OutCompiledData);
	
	PropertyAccessLibraryCompiler.Reset();
}

int32 UAnimBlueprintExtension_PropertyAccess::MapCopyIndex(int32 InIndex) const
{
	if(PropertyAccessLibraryCompiler.IsValid())
	{
		return PropertyAccessLibraryCompiler->MapCopyIndex(InIndex);
	}
	return INDEX_NONE;
}