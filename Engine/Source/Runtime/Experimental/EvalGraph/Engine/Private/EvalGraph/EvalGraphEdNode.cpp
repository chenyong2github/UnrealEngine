// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvalGraph/EvalGraphEdNode.h"

#include "EvalGraph/EvalGraphNode.h"
#include "EvalGraph/EvalGraph.h"
#include "Logging/LogMacros.h"

#define LOCTEXT_NAMESPACE "EvalGraphEdNode"

DEFINE_LOG_CATEGORY_STATIC(EGEvalGraphNodeLOG, Error, All);


void UEvalGraphEdNode::AllocateDefaultPins()
{
	UE_LOG(EGEvalGraphNodeLOG, Verbose, TEXT("UEvalGraphEdNode::AllocateDefaultPins()"));
	// called on node creation from UI. 
#if WITH_EDITOR && !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (EgGraph)
	{
		if (EgNodeGuid.IsValid())
		{
			if (TSharedPtr<Eg::FNode> EgNode = EgGraph->FindBaseNode(EgNodeGuid))
			{
				for (const Eg::FPin& Pin : EgNode->GetPins())
				{
					if (Pin.Direction == Eg::FPin::EDirection::INPUT)
					{
						CreatePin(EEdGraphPinDirection::EGPD_Input, Pin.Type, Pin.Name);
					}					
					if (Pin.Direction == Eg::FPin::EDirection::OUTPUT)
					{
						CreatePin(EEdGraphPinDirection::EGPD_Output, Pin.Type, Pin.Name);
					}
				}
			}
		}
	}
#endif // WITH_EDITOR && !UE_BUILD_SHIPPING
}

void UEvalGraphEdNode::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar << EgNodeGuid;
}


#undef LOCTEXT_NAMESPACE

