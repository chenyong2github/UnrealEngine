// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimBlueprintExtension.h"
#include "Animation/AnimSubsystem_PropertyAccess.h"
#include "IPropertyAccessCompiler.h"
#include "AnimBlueprintExtension_PropertyAccess.generated.h"

class IAnimBlueprintCompilerCreationContext;
class IAnimBlueprintCompilationBracketContext;
class IAnimBlueprintGeneratedClassCompiledData;
class UAnimBlueprintExtension_PropertyAccess;
enum class EPropertyAccessBatchType : uint8;

// Delegate called when the library is compiled (whether successfully or not)
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPostLibraryCompiled, IAnimBlueprintCompilationBracketContext& /*InCompilationContext*/, IAnimBlueprintGeneratedClassCompiledData& /*OutCompiledData*/)

UCLASS()
class ANIMGRAPH_API UAnimBlueprintExtension_PropertyAccess : public UAnimBlueprintExtension
{
	GENERATED_BODY()

	friend class UAnimBlueprintExtension_Base;

public:
	// Add a copy to the property access library we are compiling
	// @return an integer handle to the pending copy. This can be resolved to a true copy index by calling MapCopyIndex
	int32 AddCopy(TArrayView<FString> InSourcePath, TArrayView<FString> InDestPath, EPropertyAccessBatchType InBatchType, UObject* InObject = nullptr);

	// Delegate called when the library is compiled (whether successfully or not)
	FSimpleMulticastDelegate& OnPreLibraryCompiled() { return OnPreLibraryCompiledDelegate; }

	// Delegate called when the library is compiled (whether successfully or not)
	FOnPostLibraryCompiled& OnPostLibraryCompiled() { return OnPostLibraryCompiledDelegate; }

	// Maps the initial integer copy handle to a true handle, post compilation
	int32 MapCopyIndex(int32 InIndex) const;

private:
	// UAnimBlueprintExtension interface
	virtual void HandleStartCompilingClass(const UClass* InClass, IAnimBlueprintCompilationBracketContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData) override;
	virtual void HandleFinishCompilingClass(const UClass* InClass, IAnimBlueprintCompilationBracketContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData) override;

private:	
	// Property access library compiler
	TUniquePtr<IPropertyAccessLibraryCompiler> PropertyAccessLibraryCompiler;

	// Delegate called before the library is compiled
	FSimpleMulticastDelegate OnPreLibraryCompiledDelegate;

	// Delegate called when the library is compiled (whether successfully or not)
	FOnPostLibraryCompiled OnPostLibraryCompiledDelegate;

	UPROPERTY()
	FAnimSubsystem_PropertyAccess Subsystem;
};