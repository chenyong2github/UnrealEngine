// Copyright Epic Games, Inc. All Rights Reserved.
#include "Nodes/InterchangeFactoryBaseNode.h"

#include "CoreMinimal.h"
#include "Types/AttributeStorage.h"
#include "UObject/Class.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

FString UInterchangeFactoryBaseNode::GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const
{
	FString KeyDisplayName = NodeAttributeKey.ToString();
	if (NodeAttributeKey == Macro_CustomSubPathKey)
	{
		KeyDisplayName = TEXT("Import Sub-Path");
	}
	else
	{
		KeyDisplayName = Super::GetKeyDisplayName(NodeAttributeKey);
	}

	return KeyDisplayName;
}

bool UInterchangeFactoryBaseNode::GetCustomSubPath(FString& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(SubPath, FString)
}

bool UInterchangeFactoryBaseNode::SetCustomSubPath(const FString& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(SubPath, FString)
}
