// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNodeGraph.h"

#include "OptimusDeformer.h"
#include "OptimusNode.h"
#include "OptimusNodeLink.h"
#include "OptimusNodePin.h"
#include "OptimusActionStack.h"
#include "OptimusDeveloperModule.h"
#include "OptimusHelpers.h"
#include "Actions/OptimusNodeGraphActions.h"
#include "Nodes/OptimusNode_GetResource.h"
#include "Nodes/OptimusNode_GetVariable.h"
#include "Nodes/OptimusNode_SetResource.h"

#include "Containers/Queue.h"
#include "Nodes/OptimusNode_ConstantValue.h"
#include "Nodes/OptimusNode_DataInterface.h"
#include "Nodes/OptimusNode_ComputeKernelFunction.h"
#include "Templates/Function.h"
#include "UObject/Package.h"

#include <limits>

#include "Nodes/OptimusNode_ComputeKernelFunction.h"
#include "Nodes/OptimusNode_CustomComputeKernel.h"


FString UOptimusNodeGraph::GetGraphPath() const
{
	// TBD: Remove this once we have function nodes.
	ensure(GetOuter()->IsA<UOptimusDeformer>());

	return GetName();
}


IOptimusNodeGraphCollectionOwner* UOptimusNodeGraph::GetOwnerCollection() const
{
	return Cast<IOptimusNodeGraphCollectionOwner>(GetOuter());
}


int32 UOptimusNodeGraph::GetGraphIndex() const
{
	ensure(GetOuter()->IsA<UOptimusDeformer>());
	
	UOptimusDeformer* Deformer = Cast<UOptimusDeformer>(GetOuter());
	const TArray<UOptimusNodeGraph*> &Graphs = Deformer->GetGraphs();

	return Graphs.IndexOfByKey(this);
}


FOptimusGraphNotifyDelegate& UOptimusNodeGraph::GetNotifyDelegate()
{
	return GraphNotifyDelegate;
}

UOptimusNode* UOptimusNodeGraph::AddNodeInternal(
	const TSubclassOf<UOptimusNode> InNodeClass,
	const FVector2D& InPosition,
	TFunction<void(UOptimusNode*)> InNodeConfigFunc
	)
{
	FOptimusNodeGraphAction_AddNode *AddNodeAction = new FOptimusNodeGraphAction_AddNode(
		this, InNodeClass, 
		[InNodeConfigFunc, InPosition](UOptimusNode *InNode) {
			if (InNodeConfigFunc)
			{
				InNodeConfigFunc(InNode);
			}
			return InNode->SetGraphPositionDirect(InPosition); 
		});
	if (!GetActionStack()->RunAction(AddNodeAction))
	{
		return nullptr;
	}

	return AddNodeAction->GetNode(GetActionStack()->GetGraphCollectionRoot());
}

UOptimusNode* UOptimusNodeGraph::AddNode(
	const TSubclassOf<UOptimusNode> InNodeClass, 
	const FVector2D& InPosition
	)
{
	return AddNodeInternal(InNodeClass, InPosition, /*InNodeConfigFunc*/{});
}


UOptimusNode* UOptimusNodeGraph::AddValueNode(
	FOptimusDataTypeRef InDataTypeRef,
	const FVector2D& InPosition
	)
{
	UClass *ValueNodeClass = UOptimusNode_ConstantValueGeneratorClass::GetClassForType(GetPackage(), InDataTypeRef);
	return AddNodeInternal(ValueNodeClass, InPosition, /*InNodeConfigFunc*/{});
}


UOptimusNode* UOptimusNodeGraph::AddDataInterfaceNode(
	const TSubclassOf<UOptimusComputeDataInterface> InDataInterfaceClass,
	const FVector2D& InPosition
	)
{
	return AddNodeInternal(UOptimusNode_DataInterface::StaticClass(), InPosition,
		[InDataInterfaceClass](UOptimusNode *InNode)
		{
			Cast<UOptimusNode_DataInterface>(InNode)->SetDataInterfaceClass(InDataInterfaceClass);			
		});
}


UOptimusNode* UOptimusNodeGraph::AddResourceGetNode(
	UOptimusResourceDescription* InResourceDesc, 
	const FVector2D& InPosition
	)
{
	return AddNodeInternal(UOptimusNode_GetResource::StaticClass(), InPosition,
		[InResourceDesc](UOptimusNode *InNode)
		{
			Cast<UOptimusNode_GetResource>(InNode)->SetResourceDescription(InResourceDesc);			
		});
}


UOptimusNode* UOptimusNodeGraph::AddResourceSetNode(
	UOptimusResourceDescription* InResourceDesc, 
	const FVector2D& InPosition
	)
{
	return AddNodeInternal(UOptimusNode_SetResource::StaticClass(), InPosition,
		[InResourceDesc](UOptimusNode *InNode)
		{
			Cast<UOptimusNode_SetResource>(InNode)->SetResourceDescription(InResourceDesc);			
		});
}


UOptimusNode* UOptimusNodeGraph::AddVariableGetNode(
	UOptimusVariableDescription* InVariableDesc, 
	const FVector2D& InPosition
	)
{
	return AddNodeInternal(UOptimusNode_GetVariable::StaticClass(), InPosition,
		[InVariableDesc](UOptimusNode *InNode)
		{
			Cast<UOptimusNode_GetVariable>(InNode)->SetVariableDescription(InVariableDesc);			
		});
}


bool UOptimusNodeGraph::RemoveNode(UOptimusNode* InNode)
{
	if (!InNode)
	{
		return false;
	}

	return RemoveNodes({InNode});
}


bool UOptimusNodeGraph::RemoveNodes(const TArray<UOptimusNode*> &InNodes)
{
	return RemoveNodes(InNodes, TEXT("Remove"));
}

bool UOptimusNodeGraph::RemoveNodes(const TArray<UOptimusNode*>& InNodes, const FString& InActionName)
{
	// Validate the input set.
	if (InNodes.Num() == 0)
	{
		return false;
	}

	for (UOptimusNode* Node : InNodes)
	{
		if (Node == nullptr || Node->GetOwningGraph() != this)
		{
			return false;
		}
	}

	FOptimusCompoundAction* Action = new FOptimusCompoundAction;
	if (InNodes.Num() == 1)
	{
		Action->SetTitlef(TEXT("%s Node"), *InActionName);
	}
	else
	{
		Action->SetTitlef(TEXT("%s %d Nodes"), *InActionName, InNodes.Num());
	}

	TSet<int32> AllLinkIndexes;

	// Get all unique links for all the given nodes and remove them *before* we remove the nodes.
	for (const UOptimusNode* Node : InNodes)
	{
		AllLinkIndexes.Append(GetAllLinkIndexesToNode(Node));
	}

	for (const int32 LinkIndex : AllLinkIndexes)
	{
		Action->AddSubAction<FOptimusNodeGraphAction_RemoveLink>(Links[LinkIndex]);
	}

	for (UOptimusNode* Node : InNodes)
	{
		Action->AddSubAction<FOptimusNodeGraphAction_RemoveNode>(Node);
	}

	return GetActionStack()->RunAction(Action);
}


UOptimusNode* UOptimusNodeGraph::DuplicateNode(
	UOptimusNode* InNode,
	const FVector2D& InPosition
	)
{
	if (!InNode)
	{
		return nullptr;
	}
	
	const FName NodeName = Optimus::GetUniqueNameForScopeAndClass(this, UOptimusNode::StaticClass(), InNode->GetFName());
	
	FOptimusNodeGraphAction_DuplicateNode *DuplicateNodeAction = new FOptimusNodeGraphAction_DuplicateNode(
		this, InNode, NodeName,
		[InPosition](UOptimusNode *InNode) {
			return InNode->SetGraphPositionDirect(InPosition); 
		});
	if (!GetActionStack()->RunAction(DuplicateNodeAction))
	{
		return nullptr;
	}

	return DuplicateNodeAction->GetNode(GetActionStack()->GetGraphCollectionRoot());
}


bool UOptimusNodeGraph::DuplicateNodes(
	const TArray<UOptimusNode*> &InNodes,
	const FVector2D& InPosition
	)
{
	return DuplicateNodes(InNodes, InPosition, TEXT("Duplicate"));
}

bool UOptimusNodeGraph::DuplicateNodes(
	const TArray<UOptimusNode*>& InNodes,
	const FVector2D& InPosition,
	const FString& InActionName
	)
{
	// Make sure all the nodes come from the same graph.
	UOptimusNodeGraph* SourceGraph = nullptr;
	for (const UOptimusNode* Node: InNodes)
	{
		if (SourceGraph == nullptr)
		{
			SourceGraph = Node->GetOwningGraph();
		}
		else if (SourceGraph != Node->GetOwningGraph())
		{
			UE_LOG(LogOptimusDeveloper, Warning, TEXT("Nodes to duplicate have to all belong to the same graph."));
			return false;
		}
	}
	

	if (!ensure(SourceGraph != nullptr))
	{
		return false;
	}

	// Figure out the non-clashing names to use, to avoid collisions during actual execution.
	TSet<FName> ExistingObjects;
	for (const UOptimusNode* Node: Nodes)
	{
		if (ensure(Node != nullptr))
		{
			ExistingObjects.Add(Node->GetFName());
		}
	}

	auto MakeUniqueNodeName = [&ExistingObjects](FName InName)
	{
		while(ExistingObjects.Contains(InName))
		{
			InName.SetNumber(InName.GetNumber() + 1);
		}
		ExistingObjects.Add(InName);
		return InName;
	};

	using FloatType = decltype(FVector2D::X);
	FVector2D TopLeft{std::numeric_limits<FloatType>::max()};
	TMap<UOptimusNode*, FName> NewNodeNameMap;
	for (UOptimusNode* Node: InNodes)
	{
		TopLeft = FVector2D::Min(TopLeft, Node->GraphPosition);
		NewNodeNameMap.Add(Node, MakeUniqueNodeName(Node->GetFName()));
	}
	FVector2D NodeOffset = InPosition - TopLeft;

	/// Collect the links between these existing nodes. 
	TArray<TPair<FString, FString>> NodeLinks;
	const FString GraphPath = GetGraphPath();
	for (const UOptimusNodeLink* Link: SourceGraph->GetAllLinks())
	{
		const UOptimusNode *OutputNode = Link->GetNodeOutputPin()->GetNode();
		const UOptimusNode *InputNode = Link->GetNodeInputPin()->GetNode();

		if (NewNodeNameMap.Contains(OutputNode) && NewNodeNameMap.Contains(InputNode))
		{
			// FIXME: This should be a utility function, along with all the other path creation
			// functions.
			FString NodeOutputPinPath = FString::Printf(TEXT("%s/%s.%s"),
				*GraphPath, *NewNodeNameMap[OutputNode].ToString(), *Link->GetNodeOutputPin()->GetUniqueName().ToString());
			FString NodeInputPinPath = FString::Printf(TEXT("%s/%s.%s"),
				*GraphPath, *NewNodeNameMap[InputNode].ToString(), *Link->GetNodeInputPin()->GetUniqueName().ToString());

			NodeLinks.Add(MakeTuple(NodeOutputPinPath, NodeInputPinPath));
		}
	}

	FOptimusCompoundAction *Action = new FOptimusCompoundAction;
	if (InNodes.Num() == 1)
	{
		Action->SetTitlef(TEXT("%s Node"), *InActionName);
	}
	else
	{
		Action->SetTitlef(TEXT("%s %d Nodes"), *InActionName, InNodes.Num());
	}

	// Duplicate the nodes and place them correctly
	for (UOptimusNode* Node: InNodes)
	{
		FOptimusNodeGraphAction_DuplicateNode *DuplicateNodeAction = new FOptimusNodeGraphAction_DuplicateNode(
			this, Node, NewNodeNameMap[Node],
			[InPosition, Node, NodeOffset](UOptimusNode *InNode) {
				return InNode->SetGraphPositionDirect(Node->GraphPosition + NodeOffset); 
		});
		
		Action->AddSubAction(DuplicateNodeAction);
	}

	// Add any links that the nodes may have had.
	for (const TTuple<FString, FString>& LinkInfo: NodeLinks)
	{
		Action->AddSubAction<FOptimusNodeGraphAction_AddLink>(LinkInfo.Key, LinkInfo.Value);
	}

	return GetActionStack()->RunAction(Action);
}


bool UOptimusNodeGraph::AddLink(UOptimusNodePin* InNodeOutputPin, UOptimusNodePin* InNodeInputPin)
{
	if (!InNodeOutputPin || !InNodeInputPin)
	{
		return false;
	}

	if (!InNodeOutputPin->CanCannect(InNodeInputPin))
	{
		// FIXME: We should be able to report back the failure reason.
		return false;
	}

	// Swap them if they're the wrong order -- a genuine oversight.
	if (InNodeOutputPin->GetDirection() == EOptimusNodePinDirection::Input)
	{
		Swap(InNodeOutputPin, InNodeInputPin);
	}

	// Check to see if there's an existing link on the _input_ pin. Output pins can have any
	// number of connections coming out.
	TArray<int32> PinLinks = GetAllLinkIndexesToPin(InNodeInputPin);

	// This shouldn't happen, but we'll cover for it anyway.
	checkSlow(PinLinks.Num() <= 1);

	FOptimusCompoundAction* Action = new FOptimusCompoundAction;
		
	for (int32 LinkIndex : PinLinks)
	{
		Action->AddSubAction<FOptimusNodeGraphAction_RemoveLink>(Links[LinkIndex]);
	}

	FOptimusNodeGraphAction_AddLink  *AddLinkAction = new FOptimusNodeGraphAction_AddLink(InNodeOutputPin, InNodeInputPin);

	Action->SetTitle(AddLinkAction->GetTitle());
	Action->AddSubAction(AddLinkAction);

	return GetActionStack()->RunAction(Action);
}


bool UOptimusNodeGraph::RemoveLink(UOptimusNodePin* InNodeOutputPin, UOptimusNodePin* InNodeInputPin)
{
	if (!InNodeOutputPin || !InNodeInputPin)
	{
		return false;
	}
	
	// Passing in pins of the same direction is a blatant fail.
	if (!ensure(InNodeOutputPin->GetDirection() != InNodeInputPin->GetDirection()))
	{
		return false;
	}

	// Swap them if they're the wrong order -- a genuine oversight.
	if (InNodeOutputPin->GetDirection() == EOptimusNodePinDirection::Input)
	{
		Swap(InNodeOutputPin, InNodeInputPin);
	}

	for (UOptimusNodeLink* Link: Links)
	{
		if (Link->GetNodeOutputPin() == InNodeOutputPin && Link->GetNodeInputPin() == InNodeInputPin)
		{
			return GetActionStack()->RunAction<FOptimusNodeGraphAction_RemoveLink>(Link);
		}
	}

	return false;
}


bool UOptimusNodeGraph::RemoveAllLinks(UOptimusNodePin* InNodePin)
{
	if (!InNodePin)
	{
		return false;
	}

	TArray<int32> LinksToRemove = GetAllLinkIndexesToPin(InNodePin);
	if (LinksToRemove.Num() == 0)
	{
		return false;
	}

	FOptimusCompoundAction* Action = new FOptimusCompoundAction;
	if (LinksToRemove.Num() == 1)
	{
		Action->SetTitlef(TEXT("Remove Link"));
	}
	else
	{
		Action->SetTitlef(TEXT("Remove %d Links"), LinksToRemove.Num());
	}

	for (int32 LinkIndex : LinksToRemove)
	{
		Action->AddSubAction<FOptimusNodeGraphAction_RemoveLink>(Links[LinkIndex]);
	}

	return GetActionStack()->RunAction(Action);
}


UOptimusNode* UOptimusNodeGraph::ConvertCustomKernelToFunction(UOptimusNode* InCustomKernel)
{
	UOptimusNode_CustomComputeKernel* CustomKernelNode = Cast<UOptimusNode_CustomComputeKernel>(InCustomKernel);
	if (!CustomKernelNode)
	{
		UE_LOG(LogOptimusDeveloper, Error, TEXT("%s: Not a custom kernel node."), *InCustomKernel->GetName());
		return nullptr;
	}

	// The node has to have at least one input and one output binding.
	if (CustomKernelNode->InputBindings.IsEmpty() || CustomKernelNode->OutputBindings.IsEmpty())
	{
		UE_LOG(LogOptimusDeveloper, Error, TEXT("%s: Need at least one input binding and one output binding."), *CustomKernelNode->GetName());
		return nullptr;
	}

	// FIXME: We need to have a "compiled" state on the node, so that we know it's been successfully compiled.
	if (CustomKernelNode->GetDiagnosticLevel() == EOptimusDiagnosticLevel::Error)
	{
		UE_LOG(LogOptimusDeveloper, Error, TEXT("%s: Node has an error on it."), *CustomKernelNode->GetName());
		return nullptr;
	}
	
	FOptimusCompoundAction *Action = new FOptimusCompoundAction(TEXT("Create Kernel Function"));

	// Remove all links from the old node but keep their paths so that we can re-connect once the
	// packaged node has been created with the same pins.
	TArray<TPair<FString, FString>> LinkPaths;
	for (const int32 LinkIndex : GetAllLinkIndexesToNode(CustomKernelNode))
	{
		UOptimusNodeLink* Link = Links[LinkIndex];
		LinkPaths.Emplace(Link->GetNodeOutputPin()->GetPinPath(), Links[LinkIndex]->GetNodeInputPin()->GetPinPath());
		Action->AddSubAction<FOptimusNodeGraphAction_RemoveLink>(Link);
	}

	Action->AddSubAction<FOptimusNodeGraphAction_RemoveNode>(CustomKernelNode);

	FOptimusNodeGraphAction_PackageKernelFunction* PackageNodeAction = new FOptimusNodeGraphAction_PackageKernelFunction(CustomKernelNode, CustomKernelNode->GetFName()); 
	Action->AddSubAction(PackageNodeAction);

	for (const TPair<FString, FString>& LinkInfo: LinkPaths)
	{
		Action->AddSubAction<FOptimusNodeGraphAction_AddLink>(LinkInfo.Key, LinkInfo.Value);
	}

	if (!GetActionStack()->RunAction(Action))
	{
		return nullptr;
	}
	
	return PackageNodeAction->GetNode(GetActionStack()->GetGraphCollectionRoot());
}


UOptimusNode* UOptimusNodeGraph::ConvertFunctionToCustomKernel(UOptimusNode* InKernelFunction)
{
	UOptimusNode_ComputeKernelFunction* KernelFunctionNode = Cast<UOptimusNode_ComputeKernelFunction>(InKernelFunction);
	if (!KernelFunctionNode)
	{
		UE_LOG(LogOptimusDeveloper, Error, TEXT("%s: Not a kernel function node."), *InKernelFunction->GetName());
		return nullptr;
	}

	FOptimusCompoundAction *Action = new FOptimusCompoundAction(TEXT("Unpack Kernel Function"));

	// Remove all links from the old node but keep their paths so that we can re-connect once the
	// packaged node has been created with the same pins.
	TArray<TPair<FString, FString>> LinkPaths;
	for (const int32 LinkIndex : GetAllLinkIndexesToNode(KernelFunctionNode))
	{
		UOptimusNodeLink* Link = Links[LinkIndex];
		LinkPaths.Emplace(Link->GetNodeOutputPin()->GetPinPath(), Links[LinkIndex]->GetNodeInputPin()->GetPinPath());
		Action->AddSubAction<FOptimusNodeGraphAction_RemoveLink>(Link);
	}

	Action->AddSubAction<FOptimusNodeGraphAction_RemoveNode>(KernelFunctionNode);

	FOptimusNodeGraphAction_UnpackageKernelFunction* UnpackageNodeAction = new FOptimusNodeGraphAction_UnpackageKernelFunction(KernelFunctionNode, KernelFunctionNode->GetFName()); 
	Action->AddSubAction(UnpackageNodeAction);

	for (const TPair<FString, FString>& LinkInfo: LinkPaths)
	{
		Action->AddSubAction<FOptimusNodeGraphAction_AddLink>(LinkInfo.Key, LinkInfo.Value);
	}

	if (!GetActionStack()->RunAction(Action))
	{
		return nullptr;
	}
	
	return UnpackageNodeAction->GetNode(GetActionStack()->GetGraphCollectionRoot());
}


bool UOptimusNodeGraph::IsCustomKernel(UOptimusNode* InNode) const
{
	return Cast<UOptimusNode_CustomComputeKernel>(InNode) != nullptr;
}


bool UOptimusNodeGraph::IsKernelFunction(UOptimusNode* InNode) const
{
	return Cast<UOptimusNode_ComputeKernelFunction>(InNode) != nullptr;
}



UOptimusNode* UOptimusNodeGraph::CreateNodeDirect(
	const UClass* InNodeClass,
	FName InName,
	TFunction<bool(UOptimusNode*)> InConfigureNodeFunc
)
{
	check(InNodeClass->IsChildOf(UOptimusNode::StaticClass()));

	UOptimusNode* NewNode = NewObject<UOptimusNode>(this, InNodeClass, InName, RF_Transactional);

	// Configure the node as needed.
	if (InConfigureNodeFunc)
	{
		// Suppress notifications for this node while we're calling its configure callback. 
		TGuardValue<bool> SuppressNotifications(NewNode->bSendNotifications, false);
		
		if (!InConfigureNodeFunc(NewNode))
		{
			NewNode->Rename(nullptr, GetTransientPackage());
			return nullptr;
		}
	}

	NewNode->PostCreateNode();

	AddNodeDirect(NewNode);

	return NewNode;
}


bool UOptimusNodeGraph::AddNodeDirect(UOptimusNode* InNode)
{
	if (InNode == nullptr)
	{
		return false;
	}

	// Re-parent this node if it's not owned directly by us.
	if (InNode->GetOuter() != this)
	{
		UOptimusNodeGraph* OtherGraph = Cast<UOptimusNodeGraph>(InNode->GetOuter());

		// We can't re-parent this node if it still has links.
		if (OtherGraph && OtherGraph->GetAllLinkIndexesToNode(InNode).Num() != 0)
		{
			return false;
		}

		InNode->Rename(nullptr, this);
	}

	Nodes.Add(InNode);

	Notify(EOptimusGraphNotifyType::NodeAdded, InNode);

	(void)InNode->MarkPackageDirty();

	return true;
}


bool UOptimusNodeGraph::RemoveNodeDirect(
	UOptimusNode* InNode, 
	bool bFailIfLinks
	)
{
	int32 NodeIndex = Nodes.IndexOfByKey(InNode);

	// We should always have a node, unless the bookkeeping went awry.
	check(NodeIndex != INDEX_NONE);
	if (NodeIndex == INDEX_NONE)
	{
		return false;
	}

	// There should be no links to this node.
	if (bFailIfLinks)
	{
		TArray<int32> LinkIndexes = GetAllLinkIndexesToNode(InNode);
		if (LinkIndexes.Num() != 0)
		{
			return false;
		}
	}
	else
	{ 
		RemoveAllLinksToNodeDirect(InNode);
	}

	Nodes.RemoveAt(NodeIndex);

	Notify(EOptimusGraphNotifyType::NodeRemoved, InNode);

	// Unparent this node to a temporary storage and mark it for kill.
	InNode->Rename(nullptr, GetTransientPackage());

	return true;
}


bool UOptimusNodeGraph::AddLinkDirect(UOptimusNodePin* NodeOutputPin, UOptimusNodePin* NodeInputPin)
{
	if (!ensure(NodeOutputPin != nullptr && NodeInputPin != nullptr))
	{
		return false;
	}

	if (!ensure(
		NodeOutputPin->GetDirection() == EOptimusNodePinDirection::Output &&
		NodeInputPin->GetDirection() == EOptimusNodePinDirection::Input))
	{
		return false;
	}

	if (NodeOutputPin == NodeInputPin || NodeOutputPin->GetNode() == NodeInputPin->GetNode())
	{
		return false;
	}

	// Does this link already exist?
	for (const UOptimusNodeLink* Link : Links)
	{
		if (Link->GetNodeOutputPin() == NodeOutputPin && Link->GetNodeInputPin() == NodeInputPin)
		{
			return false;
		}
	}

	UOptimusNodeLink* NewLink = NewObject<UOptimusNodeLink>(this);
	NewLink->NodeOutputPin = NodeOutputPin;
	NewLink->NodeInputPin = NodeInputPin;
	Links.Add(NewLink);

	Notify(EOptimusGraphNotifyType::LinkAdded, NewLink);

	NewLink->MarkPackageDirty();

	return true;
}


bool UOptimusNodeGraph::RemoveLinkDirect(UOptimusNodePin* InNodeOutputPin, UOptimusNodePin* InNodeInputPin)
{
	if (!InNodeOutputPin || !InNodeInputPin)
	{
		return false;
	}

	check(InNodeOutputPin->GetDirection() == EOptimusNodePinDirection::Output);
	check(InNodeInputPin->GetDirection() == EOptimusNodePinDirection::Input);

	if (InNodeOutputPin->GetDirection() != EOptimusNodePinDirection::Output ||
		InNodeInputPin->GetDirection() != EOptimusNodePinDirection::Input)
	{
		return false;
	}

	for (int32 LinkIndex = 0; LinkIndex < Links.Num(); LinkIndex++)
	{
		const UOptimusNodeLink* Link = Links[LinkIndex];

		if (Link->GetNodeOutputPin() == InNodeOutputPin && Link->GetNodeInputPin() == InNodeInputPin)
		{
			RemoveLinkByIndex(LinkIndex);
			return true;
		}
	}

	return false;
}


bool UOptimusNodeGraph::RemoveAllLinksToPinDirect(UOptimusNodePin* InNodePin)
{
	if (!InNodePin)
	{
		return false;
	}

	TArray<int32> LinksToRemove = GetAllLinkIndexesToPin(InNodePin);

	if (LinksToRemove.Num() == 0)
	{
		return false;
	}

	// Remove the links in reverse order so that we pop off the highest index first.
	for (int32 i = LinksToRemove.Num(); i-- > 0; /**/)
	{
		RemoveLinkByIndex(LinksToRemove[i]);
	}

	return true;
}


bool UOptimusNodeGraph::RemoveAllLinksToNodeDirect(UOptimusNode* InNode)
{
	if (!InNode)
	{
		return false;
	}

	TArray<int32> LinksToRemove = GetAllLinkIndexesToNode(InNode);

	if (LinksToRemove.Num() == 0)
	{
		return false;
	}

	// Remove the links in reverse order so that we pop off the highest index first.
	for (int32 i = LinksToRemove.Num(); i-- > 0; /**/)
	{
		RemoveLinkByIndex(LinksToRemove[i]);
	}

	return true;
}


TArray<UOptimusNodePin*> UOptimusNodeGraph::GetConnectedPins(
	const UOptimusNodePin* InNodePin
	) const
{
	TArray<UOptimusNodePin*> ConnectedPins;
	for (int32 Index: GetAllLinkIndexesToPin(InNodePin))
	{
		const UOptimusNodeLink* Link = Links[Index];

		if (Link->GetNodeInputPin() == InNodePin)
		{
			ConnectedPins.Add(Link->GetNodeOutputPin());
		}
		else if (Link->GetNodeOutputPin() == InNodePin)
		{
			ConnectedPins.Add(Link->GetNodeInputPin());
		}
	}
	return ConnectedPins;
}


TArray<const UOptimusNodeLink*> UOptimusNodeGraph::GetPinLinks(
	const UOptimusNodePin* InNodePin
	) const
{
	TArray<const UOptimusNodeLink*> PinLinks;
	for (const int32 Index: GetAllLinkIndexesToPin(InNodePin))
	{
		const UOptimusNodeLink* Link = Links[Index];

		if (Link->GetNodeInputPin() == InNodePin)
		{
			PinLinks.Add(Link);
		}
		else if (Link->GetNodeOutputPin() == InNodePin)
		{
			PinLinks.Add(Link);
		}
	}
	return PinLinks;
}


void UOptimusNodeGraph::RemoveLinkByIndex(int32 LinkIndex)
{
	UOptimusNodeLink* Link = Links[LinkIndex];

	Links.RemoveAt(LinkIndex);

	Notify(EOptimusGraphNotifyType::LinkRemoved, Link);

	// Unparent the link to a temporary storage and mark it for kill.
	Link->Rename(nullptr, GetTransientPackage());
}


bool UOptimusNodeGraph::DoesLinkFormCycle(const UOptimusNodePin* InNodeOutputPin, const UOptimusNodePin* InNodeInputPin) const
{
	if (!ensure(InNodeOutputPin != nullptr && InNodeInputPin != nullptr) ||
		!ensure(InNodeOutputPin->GetDirection() == EOptimusNodePinDirection::Output) ||
		!ensure(InNodeInputPin->GetDirection() == EOptimusNodePinDirection::Input) ||
		!ensure(InNodeOutputPin->GetNode()->GetOwningGraph() == InNodeInputPin->GetNode()->GetOwningGraph()))
	{
		// Invalid pins -- no cycle.
		return false;
	}

	// Self-connection is a cycle.
	if (InNodeOutputPin->GetNode() == InNodeInputPin->GetNode())
	{
		return true;
	}

	const UOptimusNode *CycleNode = InNodeOutputPin->GetNode();

	// Crawl forward from the input pin's node to see if we end up hitting the output pin's node.
	TSet<const UOptimusNode *> ProcessedNodes;
	TQueue<int32> QueuedLinks;

	auto EnqueueIndexes = [&QueuedLinks](TArray<int32> InArray) -> void
	{
		for (int32 Index : InArray)
		{
			QueuedLinks.Enqueue(Index);
		}
	};

	// Enqueue as a work set all links going from the output pins of the node.
	EnqueueIndexes(GetAllLinkIndexesToNode(InNodeInputPin->GetNode(), EOptimusNodePinDirection::Output));
	ProcessedNodes.Add(InNodeInputPin->GetNode());

	int32 LinkIndex;
	while (QueuedLinks.Dequeue(LinkIndex))
	{
		const UOptimusNodeLink *Link = Links[LinkIndex];

		const UOptimusNode *NextNode = Link->GetNodeInputPin()->GetNode();

		if (NextNode == CycleNode)
		{
			// We hit the node we want to connect from, so this would cause a cycle.
			return true;
		}

		// If we haven't processed the next node yet, enqueue all its output links and mark
		// this next node as done so we don't process it again.
		if (!ProcessedNodes.Contains(NextNode))
		{
			EnqueueIndexes(GetAllLinkIndexesToNode(NextNode, EOptimusNodePinDirection::Output));
			ProcessedNodes.Add(NextNode);
		}
	}

	// We didn't hit our target node.
	return false;
}


void UOptimusNodeGraph::Notify(EOptimusGraphNotifyType InNotifyType, UObject* InSubject)
{
	GraphNotifyDelegate.Broadcast(InNotifyType, this, InSubject);
}


TArray<int32> UOptimusNodeGraph::GetAllLinkIndexesToNode(
	const UOptimusNode* InNode,
	EOptimusNodePinDirection InDirection
	) const
{
	TArray<int32> LinkIndexes;
	for (int32 LinkIndex = 0; LinkIndex < Links.Num(); LinkIndex++)
	{
		const UOptimusNodeLink* Link = Links[LinkIndex];
		if (ensure(Link != nullptr && Link->GetNodeOutputPin() != nullptr))
		{
			if ((Link->GetNodeOutputPin()->GetNode() == InNode && InDirection != EOptimusNodePinDirection::Input) ||
				(Link->GetNodeInputPin()->GetNode() == InNode && InDirection != EOptimusNodePinDirection::Output))
			{
				LinkIndexes.Add(LinkIndex);
			}
		}
	}

	return LinkIndexes;
}


TArray<int32> UOptimusNodeGraph::GetAllLinkIndexesToNode(const UOptimusNode* InNode) const
{
	return GetAllLinkIndexesToNode(InNode, EOptimusNodePinDirection::Unknown);
}


TArray<int32> UOptimusNodeGraph::GetAllLinkIndexesToPin(
	const UOptimusNodePin* InNodePin
	) const
{
	TArray<int32> LinkIndexes;
	for (int32 LinkIndex = 0; LinkIndex < Links.Num(); LinkIndex++)
	{
		const UOptimusNodeLink* Link = Links[LinkIndex];

		if ((InNodePin->GetDirection() == EOptimusNodePinDirection::Input &&
			Link->GetNodeInputPin() == InNodePin) ||
			(InNodePin->GetDirection() == EOptimusNodePinDirection::Output &&
				Link->GetNodeOutputPin() == InNodePin))
		{
			LinkIndexes.Add(LinkIndex);
		}
	}

	return LinkIndexes;
}


UOptimusActionStack* UOptimusNodeGraph::GetActionStack() const
{
	UOptimusDeformer *Deformer = Cast<UOptimusDeformer>(GetOuter());
	if (!Deformer)
	{
		return nullptr;
	}

	return Deformer->GetActionStack();
}

