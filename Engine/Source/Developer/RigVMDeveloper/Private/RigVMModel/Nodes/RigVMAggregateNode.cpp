// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMAggregateNode.h"

#include "RigVMModel/RigVMController.h"

URigVMAggregateNode::URigVMAggregateNode()
	: Super()
	, FirstInnerNodeCache(nullptr)
	, LastInnerNodeCache(nullptr)
{
}

bool URigVMAggregateNode::IsInputAggregate() const
{
	for (URigVMNode* Node : GetContainedNodes())
	{
		if (!Node->IsA<URigVMFunctionEntryNode>() && !Node->IsA<URigVMFunctionReturnNode>())
		{
			if (URigVMPin* FirstAggregatePin = Node->GetFirstAggregatePin())
			{
				return FirstAggregatePin->GetDirection() == ERigVMPinDirection::Input;
			}
		}
	}
	return false;
}

URigVMNode* URigVMAggregateNode::GetFirstInnerNode() const
{
#if UE_RIGVM_AGGREGATE_NODES_ENABLED

	if (FirstInnerNodeCache == nullptr)
	{
		if (GetContainedNodes().Num() < 3)
		{
			return nullptr;
		}
		
		if (IsInputAggregate())
		{
			// Find node connected twice to the entry (through aggregate arguments)
			FString Arg1Name = GetFirstAggregatePin()->GetName();
			FString Arg2Name = GetSecondAggregatePin()->GetName();
			URigVMFunctionEntryNode* EntryNode = GetEntryNode();
			TArray<URigVMNode*> ConnectedNodes;
			for (URigVMPin* EntryPin : EntryNode->GetPins())
			{
				TArray<URigVMPin*> TargetPins = EntryPin->GetLinkedTargetPins();
				if (TargetPins.Num() > 0)
				{
					if (TargetPins[0]->GetName() == Arg1Name || TargetPins[0]->GetName() == Arg2Name)
					{
						URigVMNode* TargetNode = TargetPins[0]->GetNode();
						if (ConnectedNodes.Contains(TargetNode))
						{
							FirstInnerNodeCache = TargetNode;
							return FirstInnerNodeCache;
						}

						ConnectedNodes.Add(TargetNode);
					}
				}
			}
		}
		else
		{
			// Find node connected to entry throught the opposite aggregate argument 
			FString ArgOppositeName = GetOppositeAggregatePin()->GetName();
			URigVMFunctionEntryNode* EntryNode = GetEntryNode();
			if (URigVMPin* EntryPin = EntryNode->FindPin(ArgOppositeName))
			{
				TArray<URigVMPin*> TargetPins = EntryPin->GetLinkedTargetPins();
				if (TargetPins.Num() > 0)
				{
					FirstInnerNodeCache = TargetPins[0]->GetNode();
					return FirstInnerNodeCache;
				}
			}
		}
	}
#endif
	return FirstInnerNodeCache;
}

URigVMNode* URigVMAggregateNode::GetLastInnerNode() const
{
#if UE_RIGVM_AGGREGATE_NODES_ENABLED

	if (LastInnerNodeCache == nullptr)
	{
		FString ArgOppositeName = GetOppositeAggregatePin()->GetName();
		if (IsInputAggregate())
		{
			// Find node connected to return throught the opposite aggregate argument
			URigVMFunctionReturnNode* ReturnNode = GetReturnNode();
			if (URigVMPin* ReturnPin = ReturnNode->FindPin(ArgOppositeName))
			{
				TArray<URigVMPin*> SourcePins = ReturnPin->GetLinkedSourcePins();
				if(SourcePins.Num() > 0)
				{
					LastInnerNodeCache = SourcePins[0]->GetNode();
					return LastInnerNodeCache;
				}
			}
		}
		else
		{
			// Find node connected twice to the return (through aggregate arguments)
			FString Arg1Name = GetFirstAggregatePin()->GetName();
			FString Arg2Name = GetSecondAggregatePin()->GetName();
			URigVMFunctionReturnNode* ReturnNode = GetReturnNode();
			TArray<URigVMNode*> ConnectedNodes;
			for (URigVMPin* ReturnPin : ReturnNode->GetPins())
			{
				TArray<URigVMPin*> SourcePins = ReturnPin->GetLinkedSourcePins();
				if (SourcePins.Num() > 0)
				{
					if (SourcePins[0]->GetName() == Arg1Name || SourcePins[0]->GetName() == Arg2Name)
					{
						URigVMNode* SourceNode = SourcePins[0]->GetNode();
						if (ConnectedNodes.Contains(SourceNode))
						{
							LastInnerNodeCache = SourceNode;
							return LastInnerNodeCache;
						}

						ConnectedNodes.Add(SourceNode);
					}
				}
			}
		}
	}
#endif
	return LastInnerNodeCache;
}

URigVMPin* URigVMAggregateNode::GetFirstAggregatePin() const
{
#if UE_RIGVM_AGGREGATE_NODES_ENABLED
	for (URigVMNode* Node : GetContainedNodes())
	{
		if (!Node->IsA<URigVMFunctionEntryNode>() && !Node->IsA<URigVMFunctionReturnNode>())
		{
			return Node->GetFirstAggregatePin();
		}
	}
#endif
	return nullptr;
}

URigVMPin* URigVMAggregateNode::GetSecondAggregatePin() const
{
#if UE_RIGVM_AGGREGATE_NODES_ENABLED
	for (URigVMNode* Node : GetContainedNodes())
	{
		if (!Node->IsA<URigVMFunctionEntryNode>() && !Node->IsA<URigVMFunctionReturnNode>())
		{
			return Node->GetSecondAggregatePin();
		}
	}
#endif
	return nullptr;
}

URigVMPin* URigVMAggregateNode::GetOppositeAggregatePin() const
{
#if UE_RIGVM_AGGREGATE_NODES_ENABLED
	for (URigVMNode* Node : GetContainedNodes())
	{
		if (!Node->IsA<URigVMFunctionEntryNode>() && !Node->IsA<URigVMFunctionReturnNode>())
		{
			return Node->GetOppositeAggregatePin();
		}
	}
#endif
	return nullptr;
}

void URigVMAggregateNode::InvalidateCache()
{
	FirstInnerNodeCache = nullptr;
	LastInnerNodeCache = nullptr;
}

FString URigVMAggregateNode::GetNodeTitle() const
{
	if (URigVMNode* InnerNode = GetFirstInnerNode())
	{
		return InnerNode->GetNodeTitle();
	}
	
	return Super::GetNodeTitle();
}

FName URigVMAggregateNode::GetMethodName() const
{
	if (URigVMUnitNode* InnerNode = Cast<URigVMUnitNode>(GetFirstInnerNode()))
	{
		return InnerNode->GetMethodName();
	}
	
	return NAME_None;
}

FText URigVMAggregateNode::GetToolTipText() const
{
	if (URigVMNode* InnerNode = GetFirstInnerNode())
	{
		return InnerNode->GetToolTipText();
	}
	
	return Super::GetToolTipText();
}

FText URigVMAggregateNode::GetToolTipTextForPin(const URigVMPin* InPin) const
{
	if (URigVMPin* EntryPin = GetContainedGraph()->GetEntryNode()->FindPin(InPin->GetName()))
	{
		const TArray<URigVMPin*> Targets = EntryPin->GetLinkedTargetPins();
		if (Targets.Num() > 0)
		{
			return Targets[0]->GetToolTipText();
		}
	}
	else if(URigVMPin* ReturnPin = GetContainedGraph()->GetReturnNode()->FindPin(InPin->GetName()))
	{
		const TArray<URigVMPin*> Sources = ReturnPin->GetLinkedSourcePins();
		if (Sources.Num() > 0)
		{
			return Sources[0]->GetToolTipText();
		}
	}
	
	return Super::GetToolTipTextForPin(InPin);
}
