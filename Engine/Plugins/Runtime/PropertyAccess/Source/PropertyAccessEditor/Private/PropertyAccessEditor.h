// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "UObject/Object.h"
#include "IPropertyAccessEditor.h"
#include "IPropertyAccessCompiler.h"

struct FEdGraphPinType;

namespace PropertyAccess
{
	/** Resolve a property path to a structure, returning the leaf property and array index if any. @return true if resolution succeeded */
	extern EPropertyAccessResolveResult ResolveLeafProperty(const UStruct* InStruct, TArrayView<FString> InPath, FProperty*& OutProperty, int32& OutArrayIndex);

	// Get the compatibility of the two supplied properties. Ordering matters for promotion (A->B).
	extern EPropertyAccessCompatibility GetPropertyCompatibility(const FProperty* InPropertyA, const FProperty* InPropertyB);

	// Get the compatibility of the two supplied pins. Ordering matters for promotion (A->B).
	extern EPropertyAccessCompatibility GetPinTypeCompatibility(const FEdGraphPinType& InPinTypeA, const FEdGraphPinType& InPinTypeB);

	// Makes a string path from a binding chain
	extern void MakeStringPath(const TArray<FBindingChainElement>& InBindingChain, TArray<FString>& OutStringPath);
}

// A helper structure used to compile a property access library
class FPropertyAccessLibraryCompiler : public IPropertyAccessLibraryCompiler
{
public:
	FPropertyAccessLibraryCompiler();

	// IPropertyAccessLibraryCompiler interface
	virtual void BeginCompilation(const UClass* InClass) override;
	virtual int32 AddCopy(TArrayView<FString> InSourcePath, TArrayView<FString> InDestPath, EPropertyAccessBatchType InBatchType, UObject* InAssociatedObject = nullptr) override;
	virtual bool FinishCompilation() override;
	virtual void IterateErrors(TFunctionRef<void(const FText&, UObject*)> InFunction) const override;
	virtual int32 MapCopyIndex(int32 InIndex) const override;

	// Set up the compiler ready for compilation
	void Setup(const UClass* InClass, FPropertyAccessLibrary* InLibrary);

public:
	// Stored copy info for processing in FinishCompilation()
	struct FQueuedCopy
	{
		TArray<FString> SourcePath;
		TArray<FString> DestPath;
		EPropertyAccessBatchType BatchType;
		FText SourceErrorText;
		FText DestErrorText;
		EPropertyAccessResolveResult SourceResult = EPropertyAccessResolveResult::Failed;
		EPropertyAccessResolveResult DestResult = EPropertyAccessResolveResult::Failed;
		UObject* AssociatedObject = nullptr;
		int32 BatchIndex = INDEX_NONE;
	};

protected:
	friend struct FPropertyAccessEditorSystem;

	// The library we are compiling
	FPropertyAccessLibrary* Library;

	// The class we are compiling the library for
	const UClass* Class;

	// All copies to process in FinishCompilation()
	TArray<FQueuedCopy> QueuedCopies;

	// Copy map
	TMap<int32, int32> CopyMap;
};
