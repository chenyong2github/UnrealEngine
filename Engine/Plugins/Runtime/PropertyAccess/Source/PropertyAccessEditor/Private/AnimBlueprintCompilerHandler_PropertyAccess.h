// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAnimBlueprintCompilerHandler.h"
#include "PropertyAccessCompilerHandler.h"
#include "PropertyAccessEditor.h"

class IAnimBlueprintCompilerCreationContext;
class IAnimBlueprintCompilationBracketContext;
class IAnimBlueprintGeneratedClassCompiledData;

class FAnimBlueprintCompilerHandler_PropertyAccess : public FPropertyAccessCompilerHandler
{
public:
	FAnimBlueprintCompilerHandler_PropertyAccess(IAnimBlueprintCompilerCreationContext& InCreationContext);

	// FPropertyAccessCompilerHandler interface
	virtual int32 AddCopy(TArrayView<FString> InSourcePath, TArrayView<FString> InDestPath, EPropertyAccessBatchType InBatchType, UObject* InObject = nullptr) override;
	virtual FSimpleMulticastDelegate& OnPreLibraryCompiled() override { return OnPreLibraryCompiledDelegate; }
	virtual FOnPostLibraryCompiled& OnPostLibraryCompiled() override { return OnPostLibraryCompiledDelegate; }
	virtual int32 MapCopyIndex(int32 InIndex) const override;

private:
	void StartCompilingClass(const UClass* InClass, IAnimBlueprintCompilationBracketContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData);
	void FinishCompilingClass(const UClass* InClass, IAnimBlueprintCompilationBracketContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData);

private:
	// Property access library compiler
	FPropertyAccessLibraryCompiler PropertyAccessLibraryCompiler;

	// Delegate called before the library is compiled
	FSimpleMulticastDelegate OnPreLibraryCompiledDelegate;

	// Delegate called when the library is compiled (whether successfully or not)
	FOnPostLibraryCompiled OnPostLibraryCompiledDelegate;
};