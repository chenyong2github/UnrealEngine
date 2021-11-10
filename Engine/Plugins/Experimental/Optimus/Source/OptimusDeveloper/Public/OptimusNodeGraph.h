// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusCoreNotify.h"
#include "OptimusDataType.h"
#include "Templates/SubclassOf.h"

#include "CoreMinimal.h"

#include "OptimusNodeGraph.generated.h"

class UOptimusVariableDescription;
class UOptimusResourceDescription;
class UOptimusComputeDataInterface;
class IOptimusNodeGraphCollectionOwner;
class UOptimusActionStack;
class UOptimusNode;
class UOptimusNodeGraph;
class UOptimusNodeLink;
class UOptimusNodePin;
enum class EOptimusNodePinDirection : uint8;
template<typename T> class TFunction;

UENUM()
enum class EOptimusNodeGraphType
{
	Setup,
	Update,
	ExternalTrigger,
	Transient
};


UCLASS()
class OPTIMUSDEVELOPER_API UOptimusNodeGraph
	: public UObject
{
	GENERATED_BODY()

public:
	FString GetGraphPath() const;

	/// Returns the graph collection that owns this particular graph.
	IOptimusNodeGraphCollectionOwner* GetOwnerCollection() const;

	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	EOptimusNodeGraphType GetGraphType() const { return GraphType; }

	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	int32 GetGraphIndex() const;


	/// @brief Returns the modify event object that can listened to in case there are changes
	/// to the graph that need to be reacted to.
	/// @return The node core event object.
	FOptimusGraphNotifyDelegate &GetNotifyDelegate();

#if WITH_EDITOR
	// Editor/python functions. These all obey undo/redo.

	// TODO: Add magic connection from a pin.
	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	UOptimusNode* AddNode(
		const TSubclassOf<UOptimusNode> InNodeClass,
		const FVector2D& InPosition
	);

	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	UOptimusNode* AddValueNode(
		FOptimusDataTypeRef InDataTypeRef,
		const FVector2D& InPosition
	);

	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	UOptimusNode* AddDataInterfaceNode(
		const TSubclassOf<UOptimusComputeDataInterface> InDataInterfaceClass,
		const FVector2D& InPosition
	);

	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	UOptimusNode* AddResourceGetNode(
	    UOptimusResourceDescription *InResourceDesc,
	    const FVector2D& InPosition);

	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	UOptimusNode* AddResourceSetNode(
	    UOptimusResourceDescription* InResourceDesc,
	    const FVector2D& InPosition);

	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	UOptimusNode* AddVariableGetNode(
	    UOptimusVariableDescription* InVariableDesc,
	    const FVector2D& InPosition
	    );

	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	bool RemoveNode(
		UOptimusNode* InNode
	);

	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	bool RemoveNodes(
		const TArray<UOptimusNode*>& InNodes
	);
	bool RemoveNodes(
		const TArray<UOptimusNode*>& InNodes,
		const FString& InActionName
	);

	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	UOptimusNode* DuplicateNode(
		UOptimusNode* InNode,
	    const FVector2D& InPosition
	);

	/// Duplicate a collection of nodes from the same graph, using the InPosition position
	/// to be the top-left origin of the pasted nodes.
	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	bool DuplicateNodes(
		const TArray<UOptimusNode*> &InNodes,
		const FVector2D& InPosition
	);
	bool DuplicateNodes(
		const TArray<UOptimusNode*> &InNodes,
		const FVector2D& InPosition,
		const FString& InActionName
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

	// Node Packaging
	/** Takes a custom kernel and converts to a packaged function. If the given node is not a
	 *  custom kernel or cannot be converted, a nullptr is returned.
	 */
	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	UOptimusNode* ConvertCustomKernelToFunction(UOptimusNode *InCustomKernel);
	
	/** Takes a kernel function and unpackages to a custom kernel. If the given node is not a 
	 *  kernel function or cannot be converted, a nullptr is returned.
	 */
	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	UOptimusNode* ConvertFunctionToCustomKernel(UOptimusNode *InKernelFunction);

	/** Returns true if the node in question is a custom kernel node that can be converted to
	  * a kernel function.
	  */
	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	bool IsCustomKernel(UOptimusNode *InNode) const;
	
	/** Returns true if the node in question is a kernel function node that can be converted to
	  * a custom kernel.
	  */
	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	bool IsKernelFunction(UOptimusNode *InNode) const;

	
#endif

	TArray<UOptimusNodePin *> GetConnectedPins(const UOptimusNodePin* InNodePin) const;
	TArray<const UOptimusNodeLink *> GetPinLinks(const UOptimusNodePin* InNodePin) const;

	/// Check to see if connecting these two pins will form a graph cycle.
	/// @param InNodeOutputPin The output pin to connect from.
	/// @param InNodeInputPin The input pin to connect into.
	/// @return True if connecting these two pins will result in a graph cycle.
	bool DoesLinkFormCycle(
		const UOptimusNodePin* InNodeOutputPin, 
		const UOptimusNodePin* InNodeInputPin) const;

	const TArray<UOptimusNode*>& GetAllNodes() const { return Nodes; }
	const TArray<UOptimusNodeLink*>& GetAllLinks() const { return Links; }

	UOptimusActionStack* GetActionStack() const;
	
protected:
	friend class UOptimusDeformer;
	friend class UOptimusNode;
	friend class UOptimusNodePin;
	friend class UOptimusClipboardContent;
	friend struct FOptimusNodeGraphAction_AddNode;
	friend struct FOptimusNodeGraphAction_DuplicateNode;
	friend struct FOptimusNodeGraphAction_RemoveNode;
	friend struct FOptimusNodeGraphAction_AddRemoveLink;
	friend struct FOptimusNodeGraphAction_PackageKernelFunction;
	friend struct FOptimusNodeGraphAction_UnpackageKernelFunction;

	// Direct edit functions. Used by the actions.
	UOptimusNode* CreateNodeDirect(
		const UClass* InNodeClass,
		FName InName,
		TFunction<bool(UOptimusNode*)> InConfigureNodeFunc
		);

	bool AddNodeDirect(
		UOptimusNode* InNode
	);

	// Remove a node directly. If a node still has connections this call will fail. 
	bool RemoveNodeDirect(
		UOptimusNode* InNode,		
		bool bFailIfLinks = true);

	bool AddLinkDirect(UOptimusNodePin* InNodeOutputPin, UOptimusNodePin* InNodeInputPin);

	bool RemoveLinkDirect(UOptimusNodePin* InNodeOutputPin, UOptimusNodePin* InNodeInputPin);

	bool RemoveAllLinksToPinDirect(UOptimusNodePin* InNodePin);

	bool RemoveAllLinksToNodeDirect(UOptimusNode* InNode);
	
	// FIXME: Remove this.
	void SetGraphType(EOptimusNodeGraphType InType)
	{
		GraphType = InType;
	}

	void Notify(EOptimusGraphNotifyType InNotifyType, UObject *InSubject);

	// The type of graph this represents. 
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Overview)
	EOptimusNodeGraphType GraphType = EOptimusNodeGraphType::Transient;

private:
#if defined(WITH_EDITOR)
	UOptimusNode* AddNodeInternal(
		const TSubclassOf<UOptimusNode> InNodeClass,
		const FVector2D& InPosition,
		TFunction<void(UOptimusNode*)> InNodeConfigFunc
	);
#endif
	
	void RemoveLinkByIndex(int32 LinkIndex);

	/// Returns the indexes of all links that connect to the node. If a direction is specified
	/// then only links coming into the node for that direction will be added (e.g. if Input
	/// is specified, then only links going into the input pins will be considered).
	/// @param InNode The node to retrieve all link connections for.
	/// @param InDirection The pins the links should be connected into, or Unknown if not 
	/// direction is not important.
	/// @return A list of indexes into the Links array of links to the given node.
	TArray<int32> GetAllLinkIndexesToNode(
		const UOptimusNode* InNode, 
		EOptimusNodePinDirection InDirection
		) const;


	TArray<int32> GetAllLinkIndexesToNode(
	    const UOptimusNode* InNode
	    ) const;

		
	TArray<int32> GetAllLinkIndexesToPin(
		const UOptimusNodePin* InNodePin
		) const;

	UPROPERTY(NonTransactional)
	TArray<UOptimusNode*> Nodes;

	// FIXME: Use a map.
	UPROPERTY(NonTransactional)
	TArray<UOptimusNodeLink*> Links;

	FOptimusGraphNotifyDelegate GraphNotifyDelegate;
};
