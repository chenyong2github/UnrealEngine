// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusCoreNotify.h"

#include "UObject/Object.h"
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

#include "OptimusNode.generated.h"

enum class EOptimusNodePinDirection : uint8;
enum class EOptimusNodePinStorageType : uint8;
class UOptimusActionStack;
class UOptimusNodeGraph;
class UOptimusNodePin;
struct FOptimusDataTypeRef;

// FIXME: This should really be a part of Array.h
template<typename T, typename Allocator>
static inline uint32 GetTypeHash(const TArray<T, Allocator>& A)
{
	uint32 Hash = GetTypeHash(A.Num());
	for (const auto& V : A)
	{
		Hash = HashCombine(Hash, GetTypeHash(V));
	}
	return Hash;
}


UCLASS(Abstract)
class OPTIMUSDEVELOPER_API UOptimusNode : public UObject
{
	GENERATED_BODY()
public:
	struct CategoryName
	{
		static const FName Attributes;
		static const FName Deformers;
		static const FName Meshes;
		static const FName Resources;
		static const FName Variables;
	};

	struct PropertyMeta
	{
		static const FName Input;
		static const FName Output;
		static const FName Resource;
	};
public:
	UOptimusNode();

	/// @brief Returns the node class category.
	/// @return The node class category.
	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	virtual FName GetNodeCategory() const PURE_VIRTUAL(, return NAME_None;);

	/// @brief Returns the node class name. This name is immutable for the given node class.
	/// @return The node class name.
	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	FName GetNodeName() const;

	/// @brief Returns the display name to use on the graphical node in the 
	/// @return 
	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	FText GetDisplayName() const;

	/// @brief Set the display name for this node.
	/// @param InDisplayName 
	/// @return 
	bool SetDisplayName(FText InDisplayName);

	/// @brief Returns the position in the graph UI that the node should be placed.
	/// @return The coordinates of the node's position.
	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	bool SetGraphPosition(const FVector2D& InPosition);

	/// @brief Returns the position in the graph UI that the node should be placed.
	/// @return The coordinates of the node's position.
	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	FVector2D GetGraphPosition() const { return GraphPosition; }

	/// @brief Set a new position of the node in the graph UI.
	/// @param InPosition The coordinates of the new position.
	/// @param bInNotify The call will notify interested parties about the position change.
	/// @return true if the position setting was successful (i.e. the coordinates are valid).
	bool SetGraphPositionDirect(const FVector2D &InPosition, bool bInNotify = true);

	/// @brief Returns the absolute path of the node. This can be passed to the root
	/// IOptimusNodeGraphCollectionOwner object to resolve to a node object.
	/// @return The absolute path of this node, rooted within the deformer.
	FString GetNodePath() const;

	/// Returns the owning node graph of this node.
	UOptimusNodeGraph *GetOwningGraph() const;

	const TArray<UOptimusNodePin*>& GetPins() const { return Pins; }


	/// Find the pin associated with the given dot-separated pin path.
	/// @param InPinPath The path of the pin.
	/// @return The pin object, if found, otherwise nullptr.
	UOptimusNodePin* FindPin(const FString &InPinPath) const;

	/// Find the pin from the given path array.
	UOptimusNodePin* FindPinFromPath(const TArray<FName>& InPinPath) const;

	/// Find the pin associated with the given FProperty object(s).
	/// @param InRootProperty The property representing the pin root we're interested in.
	/// @param InSubProperty The property representing the actual pin the value changed on.
	/// @return The pin object, if found, otherwise nullptr.
	UOptimusNodePin* FindPinFromProperty(
	    const FProperty* InRootProperty,
	    const FProperty* InSubProperty
		) const;

	/// @brief Returns the class of all non-deprecated UOptimusNodeBase nodes that are defined, 
	/// in no particular order.
	/// @return List of all classes that derive from UOptimusNodeBase.
	static TArray<UClass*> GetAllNodeClasses();

	/// Called just after the node is created, either via direct creation or deletion undo.
	/// By default it creates the pins representing connectable properties.
	void PostCreateNode();

protected:
	friend class UOptimusNodeGraph;
	friend class UOptimusNodePin;

	// Return the action stack for this node.
	UOptimusActionStack* GetActionStack() const;

	// Node layout data
	UPROPERTY()
	FVector2D GraphPosition;

	virtual void CreatePins();

	UOptimusNodePin* CreatePinFromDataType(
		FName InName,
	    EOptimusNodePinDirection InDirection,
	    EOptimusNodePinStorageType InStorageType,
		FOptimusDataTypeRef InDataType,
	    UOptimusNodePin* InParentPin = nullptr
		);

	void SetPinExpanded(const UOptimusNodePin* InPin, bool bInExpanded);
	bool GetPinExpanded(const UOptimusNodePin* InPin) const;

private:
	void Notify(
		EOptimusGraphNotifyType InNotifyType
	);

	void CreatePinsFromStructLayout(
		const UStruct *InStruct, 
		UOptimusNodePin *InParentPin = nullptr
		);

	UOptimusNodePin* CreatePinFromProperty(
	    EOptimusNodePinDirection InDirection,
		const FProperty* InProperty,
		UOptimusNodePin* InParentPin = nullptr
	);

	UPROPERTY()
	FText DisplayName;

	// The list of pins. 
	UPROPERTY()
	TArray<UOptimusNodePin *> Pins;

	UPROPERTY()
	TSet<FName> ExpandedPins;

	/// Cached pin lookups
	mutable TMap<TArray<FName>, UOptimusNodePin*> CachedPinLookup;

	/// List of all cached nodes classes. Initialized once and then never changes.
	static TArray<UClass*> CachedNodesClasses;
};
