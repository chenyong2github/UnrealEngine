#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IOptimusNodeGraphCollectionOwner.generated.h"


class UOptimusNodeGraph;
class UOptimusNode;
class UOptimusNodePin;
class FString;

UINTERFACE()
class OPTIMUSCORE_API UOptimusNodeGraphCollectionOwner : 
	public UInterface
{
	GENERATED_BODY()
};

/**
 * Interface that provides a mechanism to identify and work with node graph owners.
 */
class OPTIMUSCORE_API IOptimusNodeGraphCollectionOwner
{
	GENERATED_BODY()

public:
	/// Takes a dot-separated path string and attempts to resolve it to a specific graph,
	/// relative to this graph collection owner.
	/// @return The node graph found from this path, or nullptr if nothing was found.
	virtual UOptimusNodeGraph* ResolveGraphPath(const FString& InPath) = 0;

	/// Takes a dot-separated path string and attempts to resolve it to a specific node,
	/// relative to this graph collection owner.
	/// @return The node found from this path, or nullptr if nothing was found.
	virtual UOptimusNode* ResolveNodePath(const FString& InPath) = 0;

	/// Takes a dot-separated path string and attempts to resolve it to a specific pin on a node,
	/// relative to this graph collection owner.
	/// @return The node found from this path, or nullptr if nothing was found.
	virtual UOptimusNodePin* ResolvePinPath(const FString& InPinPath) = 0;

	/// Returns all immediately owned node graphs.
	virtual const TArray<UOptimusNodeGraph*> &GetGraphs() = 0;
};
