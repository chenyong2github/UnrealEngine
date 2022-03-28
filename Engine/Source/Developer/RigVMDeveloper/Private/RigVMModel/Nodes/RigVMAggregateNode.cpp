// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMAggregateNode.h"

#include "RigVMModel/RigVMController.h"

URigVMAggregateNode::URigVMAggregateNode()
	: Super()
	, FirstInnerNodeCache(nullptr)
{
}

bool URigVMAggregateNode::IsInputAggregate() const
{
	return GetFirstAggregatePin()->GetDirection() == ERigVMPinDirection::Input;
}

URigVMNode* URigVMAggregateNode::GetFirstInnerNode() const
{
	if (FirstInnerNodeCache == nullptr)
	{
		// Find any inner node (not entry or return)
		URigVMNode* InnerNode = nullptr;
		{
			for (URigVMNode* Node : GetContainedGraph()->GetNodes())
			{
				if (!Node->IsA<URigVMFunctionEntryNode>() && !Node->IsA<URigVMFunctionReturnNode>())
				{
					InnerNode = Node;
					break;
				}
			}

			if (!InnerNode || !InnerNode->IsAggregate())
			{
				return nullptr;
			}
		}

		// Find the aggregate pins
		URigVMGraph* Graph = GetContainedGraph();
		if (URigVMFunctionEntryNode* EntryNode = Graph->GetEntryNode())
		{
			URigVMPin* Arg1Pin = nullptr;
			const URigVMPin* Argument1 = InnerNode->GetFirstAggregatePin();
			if (Argument1->GetDirection() == ERigVMPinDirection::Input)
			{
				Arg1Pin = EntryNode->FindPin(Argument1->GetName());
			}
			else
			{
				const URigVMPin* OppositeArgument = InnerNode->GetOppositeAggregatePin();
				Arg1Pin = EntryNode->FindPin(OppositeArgument->GetName());				
			}
			if (Arg1Pin)
			{
				TArray<URigVMPin*> TargetPins = Arg1Pin->GetLinkedTargetPins();
				if (TargetPins.Num() > 0)
				{
					FirstInnerNodeCache = TargetPins[0]->GetNode(); 
				}
			}
		}
	}
	return FirstInnerNodeCache;
}

URigVMPin* URigVMAggregateNode::GetFirstAggregatePin() const
{
	const URigVMNode* FirstNode = GetFirstInnerNode();
	return FirstNode->GetFirstAggregatePin();
}

URigVMPin* URigVMAggregateNode::GetSecondAggregatePin() const
{
	const URigVMNode* FirstNode = GetFirstInnerNode();
	return FirstNode->GetSecondAggregatePin();
}

URigVMPin* URigVMAggregateNode::GetOppositeAggregatePin() const
{
	const URigVMNode* FirstNode = GetFirstInnerNode();
	return FirstNode->GetOppositeAggregatePin();
}

void URigVMAggregateNode::InvalidateCache()
{
	FirstInnerNodeCache = nullptr;
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
