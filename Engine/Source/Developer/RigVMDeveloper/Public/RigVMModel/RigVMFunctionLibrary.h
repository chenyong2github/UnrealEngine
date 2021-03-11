// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMGraph.h"
#include "RigVMModel/Nodes/RigVMLibraryNode.h"
#include "RigVMModel/Nodes/RigVMFunctionReferenceNode.h"
#include "RigVMFunctionLibrary.generated.h"

USTRUCT(BlueprintType)
struct RIGVMDEVELOPER_API FRigVMFunctionReferenceArray
{
	GENERATED_BODY()

	// Resets the data structure and maintains all storage.
	void Reset() { FunctionReferences.Reset();  }

	// Returns true if a given function reference index is valid.
	bool IsValidIndex(int32 InIndex) const { return FunctionReferences.IsValidIndex(InIndex); }

	// Returns the number of reference functions
	FORCEINLINE int32 Num() const { return FunctionReferences.Num(); }

	// const accessor for an function reference given its index
	FORCEINLINE const TSoftObjectPtr<URigVMFunctionReferenceNode>& operator[](int32 InIndex) const { return FunctionReferences[InIndex]; }

	UPROPERTY()
	TArray< TSoftObjectPtr<URigVMFunctionReferenceNode> > FunctionReferences;
};

/**
 * The Function Library is a graph used only to store
 * the sub graphs used for functions.
 */
UCLASS(BlueprintType)
class RIGVMDEVELOPER_API URigVMFunctionLibrary : public URigVMGraph
{
	GENERATED_BODY()

public:

	// Default constructor
	URigVMFunctionLibrary();

	// URigVMGraph interface
	virtual FString GetNodePath() const override;
	virtual URigVMFunctionLibrary* GetDefaultFunctionLibrary() const override;
	// end URigVMGraph interface

	// Returns all of the stored functions
	UFUNCTION(BlueprintCallable, Category = RigVMGraph)
	TArray<URigVMLibraryNode*> GetFunctions() const;

	// Finds a function by name
	UFUNCTION(BlueprintCallable, Category = RigVMGraph)
	URigVMLibraryNode* FindFunction(const FName& InFunctionName) const;

	// Returns all references for a given function name
	UFUNCTION(BlueprintCallable, Category = RigVMGraph)
	TArray< TSoftObjectPtr<URigVMFunctionReferenceNode> > GetReferencesForFunction(const FName& InFunctionName);

	// Returns all references for a given function name
	UFUNCTION(BlueprintCallable, Category = RigVMGraph)
	TArray< FString > GetReferencePathsForFunction(const FName& InFunctionName);

	// Returns a function that has been previously localized based on the provided function to localize.
	// We maintain meta data on what functions have been created locally based on which other ones,
	// and use this method to avoid redundant localizations.
	URigVMLibraryNode* FindPreviouslyLocalizedFunction(URigVMLibraryNode* InFunctionToLocalize);

private:

	UPROPERTY()
	TMap< URigVMLibraryNode*, FRigVMFunctionReferenceArray > FunctionReferences;

	// A map which stores a library node per original pathname.
	// The source pathname is the full path of the source function that was localized
	// to the local copy stored in the value of the pair.
	UPROPERTY()
	TMap< FString, URigVMLibraryNode* > LocalizedFunctions;

	friend class URigVMController;
	friend class URigVMCompiler;
};

