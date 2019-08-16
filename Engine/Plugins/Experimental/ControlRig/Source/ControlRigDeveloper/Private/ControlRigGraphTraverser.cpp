// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ControlRigGraphTraverser.h"
#include "EdGraphSchema_K2.h"

FControlRigGraphTraverser::FControlRigGraphTraverser(UControlRigModel* InModel)
	: Model(InModel)
{
}

#if WITH_EDITORONLY_DATA
bool FControlRigGraphTraverser::IsWiredToExecution(const FName& NodeName)
{
	const FControlRigModelNode* Node = Model->FindNode(NodeName);
	if(Node)
	{
		return IsWiredToExecution(Node);
	}
	return false;
}
#endif

bool FControlRigGraphTraverser::IsWiredToExecution(const FControlRigModelNode* Node)
{
	if (Node == nullptr)
	{
		return false;
	}

	const bool* Found = VisitedNodes.Find(Node->Name);
	if (Found)
	{
		return *Found;
	}

	if (Node->IsBeginExecution())
	{
		VisitedNodes.Add(Node->Name, true);
		return true;
	}

	if (Node->IsParameter() && Node->ParameterType == EControlRigModelParameterType::Output)
	{
		VisitedNodes.Add(Node->Name, true);
		return true;
	}

	VisitedNodes.Add(Node->Name, false);

	bool bFoundWiredPin = false;

	// is this an execution node,
	// walk upwards (to the left) to find a proper begin execution node
	if (Node->IsMutable())
	{
		for (const FControlRigModelPin& Pin : Node->Pins)
		{
			if (Pin.Direction != EEdGraphPinDirection::EGPD_Input)
			{
				continue;
			}
			if (Pin.Type.PinCategory != UEdGraphSchema_K2::PC_Struct)
			{
				continue;
			}
			if (Pin.Type.PinSubCategoryObject != FControlRigExecuteContext::StaticStruct())
			{
				continue;
			}

			for (int32 LinkIndex : Pin.Links)
			{
				const FControlRigModelLink* Link = Model->FindLink(LinkIndex);
				if (Link)
				{
					const FControlRigModelNode* LinkedNode = Model->FindNode(Link->Source.Node);
					if (LinkedNode)
					{
						bool bIsLinkedNodeWired = IsWiredToExecution(LinkedNode);
						if (bIsLinkedNodeWired)
						{
							bFoundWiredPin = true;
						}
					}
				}
			}
		}

		VisitedNodes.FindChecked(Node->Name) = bFoundWiredPin;
		return bFoundWiredPin;
	}

	// for all other nodes walk  to the right...
	for (const FControlRigModelPin& Pin : Node->Pins)
	{
		if (Pin.Direction != EEdGraphPinDirection::EGPD_Output)
		{
			continue;
		}

		for (int32 LinkIndex : Pin.Links)
		{
			const FControlRigModelLink* Link = Model->FindLink(LinkIndex);
			if (Link)
			{
				const FControlRigModelNode* LinkedNode = Model->FindNode(Link->Target.Node);
				if (LinkedNode)
				{
					bool bIsLinkedNodeWired = IsWiredToExecution(LinkedNode);
					if (bIsLinkedNodeWired)
					{
						bFoundWiredPin = true;
					}
				}
			}
		}
	}

	VisitedNodes.FindChecked(Node->Name) = bFoundWiredPin;
	return bFoundWiredPin;
}

void FControlRigGraphTraverser::TraverseAndBuildPropertyLinks(UControlRigBlueprint* Blueprint)
{
	for(const FControlRigModelNode& Node : Model->Nodes())
	{
		if (!IsWiredToExecution(&Node))
		{
			continue;
		}

		for (const FControlRigModelPin& Pin : Node.Pins)
		{
			if (Pin.Direction != EEdGraphPinDirection::EGPD_Output)
			{
				continue;
			}
			for (int32 LinkIndex : Pin.Links)
			{
				const FControlRigModelLink* Link = Model->FindLink(LinkIndex);
				if (Link)
				{
					const FControlRigModelNode* LinkedNode = Model->FindNode(Link->Target.Node);
					if (LinkedNode)
					{
						if (!IsWiredToExecution(LinkedNode))
						{
							continue;
						}

						int32 PinIndex = Link->Source.Pin;
						int32 LinkedPinIndex = Link->Target.Pin;
						FString PinPath = Model->GetPinPath(Link->Source);
						FString LinkedPinPath = Model->GetPinPath(Link->Target);
						Blueprint->MakePropertyLink(PinPath, LinkedPinPath, PinIndex, LinkedPinIndex);
					}
				}
			}
		}
	}
}
