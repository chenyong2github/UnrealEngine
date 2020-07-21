// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusNodeGraphNotify.h"

#include "UObject/Object.h"
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

#include "OptimusNode.generated.h"

enum class EOptimusNodePinDirection : uint8;
class UOptimusNodeGraph;
class UOptimusNodePin;

// FIXME: This should really be a part of Array.h
template<typename T, typename Allocator>
static inline uint32 GetTypeHash(const TArray<T, Allocator>& A)
{
	uint32 Seed = A.Num();
	for (const auto& V : A)
	{
		Seed ^= GetTypeHash(V) + 0x9e3779b9UL + (Seed << 6) + (Seed >> 2);
	}
	return Seed;
}



UCLASS(abstract)
class OPTIMUSCORE_API UOptimusNode : public UObject
{
	GENERATED_BODY()
public:
	struct CategoryName
	{
		static const FName Attributes;
		static const FName Deformers;
		static const FName Events;
		static const FName Meshes;
	};

	struct PropertyMeta
	{
		static const FName Input;
		static const FName Output;
	};
public:
	UOptimusNode();

	/// @brief Returns the node class category.
	/// @return The node class category.
	virtual FName GetNodeCategory() const PURE_VIRTUAL(, return NAME_None;);

	/// @brief Returns the node class name. This name is immutable for the given node class.
	/// @return The node class name.
	FName GetNodeName() const;

	/// @brief Returns the display name to use on the graphical node in the 
	/// @return 
	FText GetDisplayName() const;

	/// @brief Set the display name for this node.
	/// @param InDisplayName 
	/// @return 
	bool SetDisplayName(FText InDisplayName);

	/// @brief Returns the position in the graph UI that the node should be placed.
	/// @return The coordinates of the node's position.
	FVector2D GetGraphPosition() const { return GraphPosition; }

	/// @brief Set a new position of the node in the graph UI.
	/// @param InPosition The coordinates of the new position.
	/// @return true if the position setting was successful (i.e. the coordinates are valid).
	bool SetGraphPosition(const FVector2D &InPosition);

	/// @brief Returns the absolute path of the node. This can be passed to the root
	/// IOptimusNodeGraphCollectionOwner object to resolve to a node object.
	/// @return The absolute path of this node, rooted within the deformer.
	FString GetNodePath() const;

	/// Returns the owning node graph of this node.
	UOptimusNodeGraph *GetOwningGraph() const;

	const TArray<UOptimusNodePin*>& GetPins() const { return Pins; }

	/// @brief Find the 
	/// @param InPinPath 
	/// @return 
	UOptimusNodePin* FindPin(const FString &InPinPath);

	/// @brief Returns the class of all non-deprecated UOptimusNodeBase nodes that are defined, 
	/// in no particular order.
	/// @return List of all classes that derive from UOptimusNodeBase.
	static TArray<UClass*> GetAllNodeClasses();

protected:
	friend class UOptimusNodeGraph;

	// Node layout data
	UPROPERTY()
	FVector2D GraphPosition;

private:
	void Notify(
		EOptimusNodeGraphNotifyType InNotifyType
	);

	void CreatePinsFromStructLayout(
		UStruct *InStruct, 
		UOptimusNodePin *InParentPin = nullptr
		);

	UOptimusNodePin* CreatePinFromProperty(
		const FProperty* InProperty,
		UOptimusNodePin* InParentPin,
		EOptimusNodePinDirection InDirection
	);

	UPROPERTY()
	FText DisplayName;

	// The list of pins. These are not persisted but are instead always constructed on creation.
	TArray<UOptimusNodePin *> Pins;

	/// Cached pin lookups
	mutable TMap<TArray<FName>, UOptimusNodePin*> CachedPinLookup;

	/// List of all cached nodes classes. Initialized once and then never changes.
	static TArray<UClass*> CachedNodesClasses;
};
