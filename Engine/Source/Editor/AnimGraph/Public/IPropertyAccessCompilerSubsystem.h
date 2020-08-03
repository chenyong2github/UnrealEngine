// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Containers/ArrayView.h"
#include "Delegates/Delegate.h"
#include "IPropertyAccessCompilerSubsystem.generated.h"

enum class EPropertyAccessBatchType : uint8;

UINTERFACE(MinimalAPI)
class UPropertyAccessCompilerSubsystem : public UInterface
{
	GENERATED_BODY()
};

class IPropertyAccessCompilerSubsystem
{
	GENERATED_BODY()

public:
	// Add a copy to the property access library we are compiling
	// @return an integer handle to the pending copy. This can be resolved to a true copy index by calling MapCopyIndex
	virtual int32 AddCopy(TArrayView<FString> InSourcePath, TArrayView<FString> InDestPath, EPropertyAccessBatchType InBatchType, UObject* InObject = nullptr) = 0;

	// Delegate called when the library is compiled (whether successfully or not)
	virtual FSimpleMulticastDelegate& OnPreLibraryCompiled() = 0;

	// Delegate called when the library is compiled (whether successfully or not)
	virtual FSimpleMulticastDelegate& OnPostLibraryCompiled() = 0;

	// Maps the initial integer copy handle to a true handle, post compilation
	virtual int32 MapCopyIndex(int32 InIndex) const = 0;
};