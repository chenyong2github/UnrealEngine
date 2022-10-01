// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeParentedNode.h"


void ICustomizableObjectNodeParentedNode::SetParentNode(UCustomizableObject* Object, FGuid NodeId)
{
	SaveParentNode(Object, NodeId);
}


UCustomizableObjectNode* ICustomizableObjectNodeParentedNode::GetParentNode() const
{
	return GetCustomizableObjectExternalNode<UCustomizableObjectNode>(GetParentObject(), GetParentNodeId());
}

