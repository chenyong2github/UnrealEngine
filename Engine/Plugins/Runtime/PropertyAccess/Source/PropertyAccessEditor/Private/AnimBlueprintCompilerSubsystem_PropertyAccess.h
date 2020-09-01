// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimBlueprintCompilerSubsystem.h"
#include "IPropertyAccessCompilerSubsystem.h"
#include "PropertyAccessEditor.h"

#include "AnimBlueprintCompilerSubsystem_PropertyAccess.generated.h"

class FPropertyAccessLibraryCompiler_AnimBlueprint : public FPropertyAccessLibraryCompiler
{
public:
	// IPropertyAccessLibraryCompiler
	virtual void BeginCompilation(UClass* InClass) override;
};

UCLASS()
class UAnimBlueprintCompilerSubsystem_PropertyAccess : public UAnimBlueprintCompilerSubsystem, public IPropertyAccessCompilerSubsystem
{
	GENERATED_BODY()

public:
	// IAnimBlueprintCompilerSubsystem_PropertyAccess interface
	virtual int32 AddCopy(TArrayView<FString> InSourcePath, TArrayView<FString> InDestPath, EPropertyAccessBatchType InBatchType, UObject* InObject = nullptr) override;
	virtual FSimpleMulticastDelegate& OnPreLibraryCompiled() override { return OnPreLibraryCompiledDelegate; }
	virtual FSimpleMulticastDelegate& OnPostLibraryCompiled() override { return OnPostLibraryCompiledDelegate; }
	virtual int32 MapCopyIndex(int32 InIndex) const override;

private:
	// UAnimBlueprintCompilerSubsystem interface
	virtual void StartCompilingClass(UClass* InClass) override;
	virtual void FinishCompilingClass(UClass* InClass) override;
	virtual void GetRequiredClassSubsystems(TArray<TSubclassOf<UAnimBlueprintClassSubsystem>>& OutSubsystemClasses) const override;

private:
	// Property access library compiler
	FPropertyAccessLibraryCompiler_AnimBlueprint PropertyAccessLibraryCompiler;

	// Delegate called before the library is compiled
	FSimpleMulticastDelegate OnPreLibraryCompiledDelegate;

	// Delegate called when the library is compiled (whether successfully or not)
	FSimpleMulticastDelegate OnPostLibraryCompiledDelegate;
};