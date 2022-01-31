// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNodeGraph.h"

#include "OptimusDeformer.h"
#include "OptimusNode.h"
#include "OptimusNodeLink.h"
#include "OptimusNodePin.h"
#include "OptimusActionStack.h"
#include "OptimusCoreModule.h"
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

#include "OptimusNodeSubGraph.h"
#include "Nodes/OptimusNode_ComputeKernelFunction.h"
#include "Nodes/OptimusNode_CustomComputeKernel.h"
#include "Nodes/OptimusNode_FunctionReference.h"
#include "Nodes/OptimusNode_GraphTerminal.h"
#include "Nodes/OptimusNode_SubGraphReference.h"


#define LOCTEXT_NAMESPACE "OptimusNodeGraph"


const FName UOptimusNodeGraph::SetupGraphName("SetupGraph");
const FName UOptimusNodeGraph::UpdateGraphName("UpdateGraph");
const TCHAR* UOptimusNodeGraph::LibraryRoot = TEXT("@Library");


UOptimusNodeGraph* UOptimusNodeGraph::GetParentGraph() const
{
	return Cast<UOptimusNodeGraph>(GetOuter());
}


FString UOptimusNodeGraph::GetGraphPath() const
{
	// The path is local to the root of the node graph collection owner hierarchy, excluding the
	// root collection itself (e.g. the deformer asset).
	switch(GraphType)
	{
	case EOptimusNodeGraphType::Setup:
	case EOptimusNodeGraphType::Update:
	case EOptimusNodeGraphType::ExternalTrigger:
		return GetName();

	case EOptimusNodeGraphType::Function:
		// FIXME: Check if we're internal or external function graph
		return FString::Printf(TEXT("%s/%s"), LibraryRoot, *GetName());
		
	case EOptimusNodeGraphType::SubGraph:
		{
			TArray<FString, TInlineAllocator<8>> Ancestry;

			const UOptimusNodeGraph* CurrentGraph = this;
			while(CurrentGraph)
			{
				if (CurrentGraph->GetGraphType() == EOptimusNodeGraphType::SubGraph)
				{
					Ancestry.Add(CurrentGraph->GetName());
				}
				else
				{
					Ancestry.Add(CurrentGraph->GetGraphPath());
					break;
				}
				CurrentGraph = Cast<const UOptimusNodeGraph>(CurrentGraph->GetOuter());
			}
			Algo::Reverse(Ancestry);

			TStringBuilder<256> Path;
			return Path.Join(Ancestry, TEXT('/')).ToString();
		}
		
	case EOptimusNodeGraphType::Transient:
		return TEXT("Transient");
	}

	checkNoEntry();
	return TEXT("<unknown graph type>");
}


bool UOptimusNodeGraph::IsValidUserGraphName(
	const FString& InGraphName,
	FText* OutFailureReason
	)
{
	// Reserved names are reserved.
	if (InGraphName.Equals(SetupGraphName.ToString(), ESearchCase::IgnoreCase) ||
		InGraphName.Equals(UpdateGraphName.ToString(), ESearchCase::IgnoreCase))
	{
		if (OutFailureReason)
		{
			*OutFailureReason = FText::Format(LOCTEXT("ReservedName", "'{}' is a reserved name."), FText::FromString(InGraphName));
		}
		return false;
	}

	// '.' and '/' are path separateors. '$' is used for special graphs (e.g. libraries).
	return FName::IsValidXName(InGraphName, TEXT("./$"), OutFailureReason);
}

int32 UOptimusNodeGraph::GetGraphIndex() const
{
	return GetCollectionOwner()->GetGraphs().IndexOfByKey(this);
}


FOptimusGraphNotifyDelegate& UOptimusNodeGraph::GetNotifyDelegate()
{
	return GraphNotifyDelegate;
}


IOptimusPathResolver* UOptimusNodeGraph::GetPathResolver() const
{
	return Cast<IOptimusPathResolver>(GetCollectionRoot());
}


UOptimusNode* UOptimusNodeGraph::AddNodeInternal(
	const TSubclassOf<UOptimusNode> InNodeClass,
	const FVector2D& InPosition,
	TFunction<void(UOptimusNode*)> InNodeConfigFunc
	)
{
	// FIXME: Need better naming.
	FName NodeName = Optimus::GetUniqueNameForScopeAndClass(this, UOptimusNode::StaticClass(), InNodeClass->GetFName());
	FOptimusNodeGraphAction_AddNode *AddNodeAction = new FOptimusNodeGraphAction_AddNode(
		GetGraphPath(), InNodeClass, NodeName,
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
		GetGraphPath(), InNode, NodeName,
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
			UE_LOG(LogOptimusCore, Warning, TEXT("Nodes to duplicate have to all belong to the same graph."));
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
		const UOptimusNode *OutputNode = Link->GetNodeOutputPin()->GetOwningNode();
		const UOptimusNode *InputNode = Link->GetNodeInputPin()->GetOwningNode();

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
			GetGraphPath(), Node, NewNodeNameMap[Node],
			[Node, NodeOffset](UOptimusNode *InNode) {
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
		UE_LOG(LogOptimusCore, Error, TEXT("%s: Not a custom kernel node."), *InCustomKernel->GetName());
		return nullptr;
	}

	// The node has to have at least one input and one output binding.
	if (CustomKernelNode->InputBindings.IsEmpty() || CustomKernelNode->OutputBindings.IsEmpty())
	{
		UE_LOG(LogOptimusCore, Error, TEXT("%s: Need at least one input binding and one output binding."), *CustomKernelNode->GetName());
		return nullptr;
	}

	// FIXME: We need to have a "compiled" state on the node, so that we know it's been successfully compiled.
	if (CustomKernelNode->GetDiagnosticLevel() == EOptimusDiagnosticLevel::Error)
	{
		UE_LOG(LogOptimusCore, Error, TEXT("%s: Node has an error on it."), *CustomKernelNode->GetName());
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
		UE_LOG(LogOptimusCore, Error, TEXT("%s: Not a kernel function node."), *InKernelFunction->GetName());
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


UOptimusNode* UOptimusNodeGraph::CollapseNodesToFunction(
	const TArray<UOptimusNode*>& InNodes
	)
{
	return nullptr;
}


UOptimusNode* UOptimusNodeGraph::CollapseNodesToSubGraph(
	const TArray<UOptimusNode*>& InNodes
	)
{
	const TSet<UOptimusNode*> NodeSet(InNodes);
	
	// Collect all links that go to nodes that are not a part of the group and all links that
	// are within elements of the group. At the same time, collect the bindings that apply.
	TArray<const UOptimusNodeLink*> InternalLinks;
	TArray<const UOptimusNodeLink*> InputLinks;			// Links going into the node set
	TArray<const UOptimusNodeLink*> OutputLinks;		// Links coming from the node set
	TArray<FOptimusParameterBinding> InputBindings;
	TArray<FOptimusParameterBinding> OutputBindings;

	// FIXME: The bindings should be ordered by node's Y-coordinate and input number.
	for (const UOptimusNodeLink* Link: Links)
	{
		const bool bInputNodeInSet = NodeSet.Contains(Link->GetNodeInputPin()->GetOwningNode());
		const bool bOutputNodeInSet = NodeSet.Contains(Link->GetNodeOutputPin()->GetOwningNode());

		if (bInputNodeInSet && bOutputNodeInSet)
		{
			InternalLinks.Add(Link);
		}
		else if (bInputNodeInSet)
		{
			InputLinks.Add(Link);

			// The entry pin on our sub-graph is named after the input pin of the node that's
			// being collapsed into it.
			const UOptimusNodePin* InputPin = Link->GetNodeInputPin();
			FOptimusParameterBinding Binding;
			Binding.Name = InputPin->GetFName();
			Binding.DataType = InputPin->GetDataType();
			if (InputPin->GetStorageType() == EOptimusNodePinStorageType::Resource)
			{
				Binding.DataDomain = InputPin->GetDataDomainLevelNames();
			}
			else
			{
				Binding.DataDomain.LevelNames.Reset();
			}
			InputBindings.Add(Binding);
		}
		else if (bOutputNodeInSet)
		{
			OutputLinks.Add(Link);
			
			const UOptimusNodePin* OutputPin = Link->GetNodeOutputPin();
			FOptimusParameterBinding Binding;
			Binding.Name = OutputPin->GetFName();
			Binding.DataType = OutputPin->GetDataType();
			if (OutputPin->GetStorageType() == EOptimusNodePinStorageType::Resource)
			{
				Binding.DataDomain = OutputPin->GetDataDomainLevelNames();
			}
			else
			{
				Binding.DataDomain.LevelNames.Reset();
			}
			OutputBindings.Add(Binding);
		}
	}

	FOptimusCompoundAction *Action = new FOptimusCompoundAction(TEXT("Collapse Nodes to Sub-graph"));
	IOptimusPathResolver* PathResolver = GetPathResolver();

	FName SubGraphName("SubGraph");
	SubGraphName = Optimus::GetUniqueNameForScopeAndClass(this, UOptimusNodeSubGraph::StaticClass(), SubGraphName);
	
	FOptimusNodeGraphAction_AddGraph* CreateGraph = new FOptimusNodeGraphAction_AddGraph(
		this, EOptimusNodeGraphType::SubGraph, SubGraphName, INDEX_NONE,
		[InputBindings, OutputBindings](UOptimusNodeGraph* InGraph) -> bool
		{
			UOptimusNodeSubGraph* SubGraph = Cast<UOptimusNodeSubGraph>(InGraph);
			SubGraph->InputBindings = InputBindings;
			SubGraph->OutputBindings = OutputBindings;
			return true;
		});
	Action->AddSubAction(CreateGraph);

	FString SubGraphPath = GetGraphPath() + TEXT("/") + SubGraphName.ToString();

	// Create the entry and return nodes.
	FBox2D NodeBox(ForceInit);
	for (const UOptimusNode* Node: InNodes)
	{
		NodeBox += Node->GetGraphPosition();
	}
	
	Action->AddSubAction<FOptimusNodeGraphAction_AddNode>(
		SubGraphPath, UOptimusNode_GraphTerminal::StaticClass(), "Entry",
		[NodeBox, SubGraphPath, PathResolver](UOptimusNode* InNode)
		{
			UOptimusNode_GraphTerminal* EntryNode = Cast<UOptimusNode_GraphTerminal>(InNode); 
			UOptimusNodeSubGraph* SubGraph = Cast<UOptimusNodeSubGraph>(PathResolver->ResolveGraphPath(SubGraphPath));

			SubGraph->EntryNode = EntryNode;
			
			EntryNode->TerminalType = EOptimusTerminalType::Entry;
			EntryNode->OwningGraph = SubGraph;
			return EntryNode->SetGraphPositionDirect({NodeBox.Min.X - 150.0f, NodeBox.GetCenter().Y});
		});

	Action->AddSubAction<FOptimusNodeGraphAction_AddNode>(
		SubGraphPath, UOptimusNode_GraphTerminal::StaticClass(), "Return",
		[NodeBox, SubGraphPath, PathResolver](UOptimusNode* InNode)
		{
			UOptimusNode_GraphTerminal* ReturnNode = Cast<UOptimusNode_GraphTerminal>(InNode); 
			UOptimusNodeSubGraph* SubGraph = Cast<UOptimusNodeSubGraph>(PathResolver->ResolveGraphPath(SubGraphPath));

			SubGraph->ReturnNode = ReturnNode;
			
			ReturnNode->TerminalType = EOptimusTerminalType::Return;
			ReturnNode->OwningGraph = SubGraph;
			return ReturnNode->SetGraphPositionDirect({NodeBox.Max.X + 300.0f, NodeBox.GetCenter().Y});
		});
	
	// Duplicate the nodes into the graph.
	for (UOptimusNode* Node: InNodes)
	{
		Action->AddSubAction<FOptimusNodeGraphAction_DuplicateNode>(
			SubGraphPath, Node, Node->GetFName(), [](UOptimusNode*) { return true; });
	}

	// Create the reference node and connect it.
	FName GraphNodeRefName("SubGraphNode");
	GraphNodeRefName = Optimus::GetUniqueNameForScopeAndClass(this, UObject::StaticClass(), GraphNodeRefName);
	FOptimusNodeGraphAction_AddNode* AddSubGraphRefNodeAction = new FOptimusNodeGraphAction_AddNode(
		GetGraphPath(), UOptimusNode_SubGraphReference::StaticClass(), GraphNodeRefName,
		[NodeBox, SubGraphPath, PathResolver](UOptimusNode* InNode)
		{
			UOptimusNode_SubGraphReference* SubGraphNode = Cast<UOptimusNode_SubGraphReference>(InNode); 
			UOptimusNodeSubGraph* SubGraph = Cast<UOptimusNodeSubGraph>(PathResolver->ResolveGraphPath(SubGraphPath));
			
			SubGraphNode->SubGraph = SubGraph;
			return SubGraphNode->SetGraphPositionDirect(NodeBox.GetCenter());
		});
	
	Action->AddSubAction(AddSubGraphRefNodeAction);
	
	// Remove all existing links in the original graph. This has to be done before we remove 
	// nodes and add new links, otherwise the node removal and link creation to already-connected
	// inputs will fail.
	for (const UOptimusNodeLink* Link: InternalLinks)
	{
		Action->AddSubAction<FOptimusNodeGraphAction_RemoveLink>(Link);
	}
	for (const UOptimusNodeLink* Link: InputLinks)
	{
		Action->AddSubAction<FOptimusNodeGraphAction_RemoveLink>(Link);
	}
	for (const UOptimusNodeLink* Link: OutputLinks)
	{
		Action->AddSubAction<FOptimusNodeGraphAction_RemoveLink>(Link);
	}
	
	// Create the links in the sub-graph between the duplicated nodes and the entry/return
	// nodes using the internal/input/output links gathered previously.
	for (const UOptimusNodeLink* Link: InternalLinks)
	{
		FString NodeOutputPinPath = FString::Printf(TEXT("%s/%s.%s"),
			*SubGraphPath, *Link->NodeOutputPin->GetOwningNode()->GetName(), *Link->NodeOutputPin->GetUniqueName().ToString());
		FString NodeInputPinPath = FString::Printf(TEXT("%s/%s.%s"),
			*SubGraphPath, *Link->NodeInputPin->GetOwningNode()->GetName(), *Link->NodeInputPin->GetUniqueName().ToString());
		Action->AddSubAction<FOptimusNodeGraphAction_AddLink>(NodeOutputPinPath, NodeInputPinPath);
	}
	for (const UOptimusNodeLink* Link: InputLinks)
	{
		// Once for the Entry -> Sub-graph nodes, and another for Outer graph nodes -> Sub-graph ref node inputs.
		FString NodeOutputPinPath = FString::Printf(TEXT("%s/Entry.%s"),
			*SubGraphPath, *Link->NodeInputPin->GetUniqueName().ToString());
		FString NodeInputPinPath = FString::Printf(TEXT("%s/%s.%s"),
			*SubGraphPath, *Link->NodeInputPin->GetOwningNode()->GetName(), *Link->NodeInputPin->GetUniqueName().ToString());
		Action->AddSubAction<FOptimusNodeGraphAction_AddLink>(NodeOutputPinPath, NodeInputPinPath);
		
		NodeOutputPinPath = Link->NodeOutputPin->GetPinPath();
		NodeInputPinPath = FString::Printf(TEXT("%s/%s.%s"),
			*GetGraphPath(), *GraphNodeRefName.ToString(), *Link->NodeInputPin->GetUniqueName().ToString());
		Action->AddSubAction<FOptimusNodeGraphAction_AddLink>(NodeOutputPinPath, NodeInputPinPath);
	}
	for (const UOptimusNodeLink* Link: OutputLinks)
	{
		// Once for the Sub-graph nodes -> Return, and another for Sub-graph ref node outputs -> Outer graph nodes.
		FString NodeOutputPinPath = FString::Printf(TEXT("%s/%s.%s"),
			*SubGraphPath, *Link->NodeOutputPin->GetOwningNode()->GetName(), *Link->NodeOutputPin->GetUniqueName().ToString());
		FString NodeInputPinPath = FString::Printf(TEXT("%s/Return.%s"),
			*SubGraphPath, *Link->NodeOutputPin->GetUniqueName().ToString());
		Action->AddSubAction<FOptimusNodeGraphAction_AddLink>(NodeOutputPinPath, NodeInputPinPath);
		
		NodeOutputPinPath = FString::Printf(TEXT("%s/%s.%s"),
			*GetGraphPath(), *GraphNodeRefName.ToString(), *Link->NodeOutputPin->GetUniqueName().ToString());
		NodeInputPinPath = Link->NodeInputPin->GetPinPath();
		Action->AddSubAction<FOptimusNodeGraphAction_AddLink>(NodeOutputPinPath, NodeInputPinPath);
	}
	
	// Delete the existing nodes
	for (UOptimusNode* Node: InNodes)
	{
		Action->AddSubAction<FOptimusNodeGraphAction_RemoveNode>(Node);
	}
	
	if (!GetActionStack()->RunAction(Action))
	{
		return nullptr;
	}
	
	return AddSubGraphRefNodeAction->GetNode(PathResolver);
}


TArray<UOptimusNode*> UOptimusNodeGraph::ExpandCollapsedNodes(
	UOptimusNode* InFunctionNode
	)
{
	const bool bIsFunction = IsFunctionReference(InFunctionNode);
	const bool bIsSubGraph = IsSubGraphReference(InFunctionNode);
	if (!bIsFunction && !bIsSubGraph)
	{
		return {};
	}

	return {};
}


bool UOptimusNodeGraph::IsCustomKernel(UOptimusNode* InNode) const
{
	return Cast<UOptimusNode_CustomComputeKernel>(InNode) != nullptr;
}


bool UOptimusNodeGraph::IsKernelFunction(UOptimusNode* InNode) const
{
	return Cast<UOptimusNode_ComputeKernelFunction>(InNode) != nullptr;
}


bool UOptimusNodeGraph::IsFunctionReference(UOptimusNode* InNode) const
{
	return Cast<UOptimusNode_FunctionReference>(InNode) != nullptr;
}


bool UOptimusNodeGraph::IsSubGraphReference(UOptimusNode* InNode) const
{
	return Cast<UOptimusNode_SubGraphReference>(InNode) != nullptr;
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

	if (NodeOutputPin == NodeInputPin || NodeOutputPin->GetOwningNode() == NodeInputPin->GetOwningNode())
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


TArray<FOptimusRoutedNodePin> UOptimusNodeGraph::GetConnectedPinsWithRouting(
	const UOptimusNodePin* InNodePin,
	const FOptimusPinTraversalContext& InContext
	) const
{
	TQueue<FOptimusRoutedNodePin> PinQueue;

	PinQueue.Enqueue({const_cast<UOptimusNodePin*>(InNodePin), InContext});

	TArray<FOptimusRoutedNodePin> RoutedNodePins;
	FOptimusRoutedNodePin WorkingPin;
	while (PinQueue.Dequeue(WorkingPin))
	{
		for (UOptimusNodePin* ConnectedPin: WorkingPin.NodePin->GetConnectedPins())
		{
			const IOptimusNodePinRouter* RouterNode = Cast<IOptimusNodePinRouter>(ConnectedPin->GetOwningNode());

			// If this connection leads to a router node, find the matching pin on the other side and
			// add it to the queue. Otherwise we're done, and we add the connected pin and the
			// context to the result (in case the user wants to traverse further via that node through
			// the given pin).
			if (RouterNode)
			{
				const FOptimusRoutedNodePin RoutedPin = RouterNode->GetPinCounterpart(ConnectedPin, WorkingPin.TraversalContext);
				if (RoutedPin.NodePin != nullptr)
				{
					PinQueue.Enqueue(RoutedPin);
				}
			}
			else
			{
				RoutedNodePins.Add({ConnectedPin, WorkingPin.TraversalContext});
			}
		}
	}

	return RoutedNodePins;
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
		!ensure(InNodeOutputPin->GetOwningNode()->GetOwningGraph() == InNodeInputPin->GetOwningNode()->GetOwningGraph()))
	{
		// Invalid pins -- no cycle.
		return false;
	}

	// Self-connection is a cycle.
	if (InNodeOutputPin->GetOwningNode() == InNodeInputPin->GetOwningNode())
	{
		return true;
	}

	const UOptimusNode *CycleNode = InNodeOutputPin->GetOwningNode();

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
	EnqueueIndexes(GetAllLinkIndexesToNode(InNodeInputPin->GetOwningNode(), EOptimusNodePinDirection::Output));
	ProcessedNodes.Add(InNodeInputPin->GetOwningNode());

	int32 LinkIndex;
	while (QueuedLinks.Dequeue(LinkIndex))
	{
		const UOptimusNodeLink *Link = Links[LinkIndex];

		const UOptimusNode *NextNode = Link->GetNodeInputPin()->GetOwningNode();

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


void UOptimusNodeGraph::GlobalNotify(EOptimusGlobalNotifyType InNotifyType, UObject* InObject) const
{
	const UOptimusDeformer* Deformer = Cast<UOptimusDeformer>(GetCollectionRoot());
	if (Deformer)
	{
		Deformer->Notify(InNotifyType, InObject);
	}
	
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
			if ((Link->GetNodeOutputPin()->GetOwningNode() == InNode && InDirection != EOptimusNodePinDirection::Input) ||
				(Link->GetNodeInputPin()->GetOwningNode() == InNode && InDirection != EOptimusNodePinDirection::Output))
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
	UOptimusDeformer *Deformer = Cast<UOptimusDeformer>(GetCollectionRoot());
	if (!Deformer)
	{
		return nullptr;
	}

	return Deformer->GetActionStack();
}


IOptimusNodeGraphCollectionOwner* UOptimusNodeGraph::GetCollectionOwner() const
{
	return Cast<IOptimusNodeGraphCollectionOwner>(GetOuter());
}


IOptimusNodeGraphCollectionOwner* UOptimusNodeGraph::GetCollectionRoot() const
{
	return GetCollectionOwner()->GetCollectionRoot();
}


FString UOptimusNodeGraph::GetCollectionPath() const
{
	if (const FString ParentPath = GetCollectionOwner()->GetCollectionPath(); !ParentPath.IsEmpty())
	{
		return ParentPath + TEXT("/") + GetName();
	}
	return GetName();
}


UOptimusNodeGraph* UOptimusNodeGraph::CreateGraph(
	EOptimusNodeGraphType InType,
	FName InName,
	TOptional<int32> InInsertBefore
	)
{
	if (InType != EOptimusNodeGraphType::SubGraph)
	{
		UE_LOG(LogOptimusCore, Warning, TEXT("Only subgraphs can be added to other graphs"));
		return nullptr;
	}

	UOptimusNodeSubGraph* Graph = NewObject<UOptimusNodeSubGraph>(this, UOptimusNodeSubGraph::StaticClass(), InName, RF_Transactional);

	Graph->SetGraphType(EOptimusNodeGraphType::SubGraph);

	// The Entry/Return nodes will be added by the action.

	if (InInsertBefore.IsSet())
	{
		if (!AddGraph(Graph, InInsertBefore.GetValue()))
		{
			Graph->Rename(nullptr, GetTransientPackage());
			return nullptr;
		}
	}
	
	return Graph;
}


bool UOptimusNodeGraph::AddGraph(
	UOptimusNodeGraph* InGraph, 
	int32 InInsertBefore
	)
{
	if (InGraph == nullptr || InGraph->GetGraphType() != EOptimusNodeGraphType::SubGraph)
	{
		return false;
	}

	// If INDEX_NONE, insert at the end.
	if (InInsertBefore == INDEX_NONE)
	{
		InInsertBefore = SubGraphs.Num();
	}
	else
	{
		InInsertBefore = FMath::Clamp(InInsertBefore, 0, SubGraphs.Num());
	}

	SubGraphs.Insert(InGraph, InInsertBefore);

	GlobalNotify(EOptimusGlobalNotifyType::GraphAdded, InGraph);

	return true;
}


bool UOptimusNodeGraph::RemoveGraph(
	UOptimusNodeGraph* InGraph,
	bool bInDeleteGraph
	)
{
	// Not ours?
	const int32 GraphIndex = SubGraphs.IndexOfByKey(InGraph);
	if (GraphIndex == INDEX_NONE)
	{
		return false;
	}

	if (InGraph->GetGraphType() == EOptimusNodeGraphType::Update)
	{
		return false;
	}

	SubGraphs.RemoveAt(GraphIndex);

	GlobalNotify(EOptimusGlobalNotifyType::GraphRemoved, InGraph);

	if (bInDeleteGraph)
	{
		// Un-parent this graph to a temporary storage and mark it for kill.
		InGraph->Rename(nullptr, GetTransientPackage());
	}

	return true;
}


bool UOptimusNodeGraph::MoveGraph(
	UOptimusNodeGraph* InGraph,
	int32 InInsertBefore
	)
{
	return false;
}


bool UOptimusNodeGraph::RenameGraph(
	UOptimusNodeGraph* InGraph,
	const FString& InNewName
	)
{
	// Not ours?
	const int32 GraphIndex = SubGraphs.IndexOfByKey(InGraph);
	if (GraphIndex == INDEX_NONE)
	{
		return false;
	}
		
	if (!IsValidUserGraphName(InNewName))
	{
		return false;
	}

	const bool bSuccess = GetActionStack()->RunAction<FOptimusNodeGraphAction_RenameGraph>(InGraph, FName(*InNewName));
	if (bSuccess)
	{
		GlobalNotify(EOptimusGlobalNotifyType::GraphRenamed, InGraph);
	}
	return bSuccess;
}


#undef LOCTEXT_NAMESPACE
