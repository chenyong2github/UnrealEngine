// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGEdge.h"
#include "PCGNode.h"

void UPCGEdge::BreakEdge()
{
	Modify();
	if(InboundNode)
	{
		InboundNode->RemoveOutboundEdge(this);
	}

	if(OutboundNode)
	{
		OutboundNode->RemoveInboundEdge(this);
	}
}