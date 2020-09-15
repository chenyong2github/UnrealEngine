// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "UObject/ObjectMacros.h"

class UObject;
struct FPropertyAccessLibrary;

// The various batching of property copy
UENUM()
enum class EPropertyAccessBatchType : uint8
{
	// Copies designed to be called one at a time via ProcessCopy
	Unbatched,

	// Copies designed to be processed in one call to ProcessCopies
	Batched,
};

// A helper used to compile a property access library
class IPropertyAccessLibraryCompiler
{
public:
	virtual ~IPropertyAccessLibraryCompiler() {}

	// Begin compilation - reset the library to its default state
	virtual void BeginCompilation(const UClass* InClass) = 0;

	// Add a copy to the property access library we are compiling
	// @return an integer handle to the pending copy. This can be resolved to a true copy index by calling MapCopyIndex
	virtual int32 AddCopy(TArrayView<FString> InSourcePath, TArrayView<FString> InDestPath, EPropertyAccessBatchType InBatchType, UObject* InAssociatedObject = nullptr) = 0;

	// Post-process the library to finish compilation. @return true if compilation succeeded.
	virtual bool FinishCompilation() = 0;

	// Iterate any errors we have with compilation
	virtual void IterateErrors(TFunctionRef<void(const FText&, UObject*)> InFunction) const = 0;

	// Maps the initial integer copy handle to a true handle, post compilation
	virtual int32 MapCopyIndex(int32 InIndex) const = 0;
};