// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusNodeGraphNotify.h"

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

#include "OptimusNodeGraph.generated.h"

class UOptimusActionStack;
class UOptimusNode;
class UOptimusNodeGraph;
class UOptimusNodeLink;
class UOptimusNodePin;

UENUM()
enum class EOptimusNodeGraphType : uint8
{
	Setup,
	Update,
	ExternalTrigger,
};


UCLASS()
class OPTIMUSCORE_API UOptimusNodeGraph 
	: public UObject
{
	GENERATED_BODY()

public:
	FString GetGraphPath() const;

	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	EOptimusNodeGraphType GetGraphType() const { return GraphType; }

	/// @brief Returns the modify event object that can listened to in case there are changes
	/// to the graph that need to be reacted to.
	/// @return The node graph event object.
	FOptimusNodeGraphEvent &OnModify();

#if WITH_EDITOR
	// Editor/python functions. These all obey undo/redo.

	// TODO: Add magic connection from a pin.
	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	UOptimusNode* AddNode(
		const UClass* InNodeClass,
		const FVector2D& InPosition
	);

	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	bool RemoveNode(
		UOptimusNode* InNode
	);

	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	bool RemoveNodes(
		const TArray<UOptimusNode*> &InNodes
	);

	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	bool AddLink(
		UOptimusNodePin* InNodeOutputPin, UOptimusNodePin* InNodeInputPin
	);

	/// @brief Removes a single link between two nodes.
	// FIXME: Use UOptimusNodeLink instead.
	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	bool RemoveLink(
		UOptimusNodePin* InNodeOutputPin, UOptimusNodePin* InNodeInputPin
	);

	/// @brief Removes all links to the given pin, whether it's an input or an output pin.
	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	bool RemoveAllLinks(
		UOptimusNodePin* InNodePin
	);




#endif

	// Direct edit functions. Used by the actions.
	UOptimusNode* AddNodeDirect(
		const UClass* InNodeClass,
		FName InName = NAME_None,
		const FVector2D * InPosition = nullptr
		);

	bool AddNodeDirect(
		UOptimusNode* InNode
	);

	// Remove a node directly. Also removes the links, unless bFailIfLinks is set to true,
	// in which case this function fails before removing the node.
	bool RemoveNodeDirect(
		UOptimusNode* InNode,		
		bool bFailIfLinks = true);

	bool AddLinkDirect(UOptimusNodePin* InNodeOutputPin, UOptimusNodePin* InNodeInputPin);

	bool RemoveLinkDirect(UOptimusNodePin* InNodeOutputPin, UOptimusNodePin* InNodeInputPin);

	bool RemoveAllLinksToPinDirect(UOptimusNodePin* InNodePin);

	bool RemoveAllLinksToNodeDirect(UOptimusNode* InNode);

	const TArray< UOptimusNode*>& GetAllNodes() const { return Nodes; }
	const TArray< UOptimusNodeLink*>& GetAllLinks() const { return Links; }

	UOptimusActionStack* GetActionStack() const;
	
protected:
	friend class UOptimusDeformer;
	friend class UOptimusNode;

	void SetGraphType(EOptimusNodeGraphType InType)
	{
		GraphType = InType;
	}

	void Notify(EOptimusNodeGraphNotifyType InNotifyType, UObject *InSubject);

	// The type of graph this represents.
	UPROPERTY(BlueprintReadOnly, Category=Overview)
	EOptimusNodeGraphType GraphType;

private:
	void RemoveLinkByIndex(int32 LinkIndex);
	TArray<int32> GetAllLinkIndexesToNode(UOptimusNode* InNode);
	TArray<int32> GetAllLinkIndexesToPin(UOptimusNodePin* InNodePin);

	UPROPERTY()
	TArray<UOptimusNode*> Nodes;

	// FIXME: Use a map.
	UPROPERTY()
	TArray<UOptimusNodeLink*> Links;


	FOptimusNodeGraphEvent ModifiedEvent;
};
