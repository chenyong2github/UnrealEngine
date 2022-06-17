// Copyright Epic Games, Inc. All Rights Reserved.
#include "Nodes/InterchangeFactoryBaseNode.h"

#include "CoreMinimal.h"
#include "Types/AttributeStorage.h"
#include "UObject/Class.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"


namespace UE
{
	namespace Interchange
	{
		const FString& FFactoryBaseNodeStaticData::FactoryDependenciesBaseKey()
		{
			static FString BaseNodeFactoryDependencies_BaseKey = TEXT("__BaseNodeFactoryDependencies__");
			return BaseNodeFactoryDependencies_BaseKey;
		}

	} //ns Interchange
} //ns UE


UInterchangeFactoryBaseNode::UInterchangeFactoryBaseNode()
{
	FactoryDependencies.Initialize(Attributes, UE::Interchange::FFactoryBaseNodeStaticData::FactoryDependenciesBaseKey());
}


FString UInterchangeFactoryBaseNode::GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const
{
	FString KeyDisplayName = NodeAttributeKey.ToString();
	const FString OriginalKeyName = KeyDisplayName;
	if (NodeAttributeKey == Macro_CustomSubPathKey)
	{
		KeyDisplayName = TEXT("Import Sub-Path");
	}
	else if (OriginalKeyName.Equals(UE::Interchange::FFactoryBaseNodeStaticData::FactoryDependenciesBaseKey()))
	{
		KeyDisplayName = TEXT("Factory Dependencies Count");
	}
	else if (OriginalKeyName.StartsWith(UE::Interchange::FFactoryBaseNodeStaticData::FactoryDependenciesBaseKey()))
	{
		KeyDisplayName = TEXT("Factory Dependencies Index ");
		const FString IndexKey = UE::Interchange::TArrayAttributeHelper<FString>::IndexKey();
		int32 IndexPosition = OriginalKeyName.Find(IndexKey) + IndexKey.Len();
		if (IndexPosition < OriginalKeyName.Len())
		{
			KeyDisplayName += OriginalKeyName.RightChop(IndexPosition);
		}
	}
	else
	{
		KeyDisplayName = Super::GetKeyDisplayName(NodeAttributeKey);
	}

	return KeyDisplayName;
}


FString UInterchangeFactoryBaseNode::GetAttributeCategory(const UE::Interchange::FAttributeKey& NodeAttributeKey) const
{
	if (NodeAttributeKey.ToString().StartsWith(UE::Interchange::FFactoryBaseNodeStaticData::FactoryDependenciesBaseKey()))
	{
		return TEXT("FactoryDependencies");
	}
	else
	{
		return Super::GetAttributeCategory(NodeAttributeKey);
	}
}


UClass* UInterchangeFactoryBaseNode::GetObjectClass() const
{
	return nullptr;
}

bool UInterchangeFactoryBaseNode::GetCustomSubPath(FString& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(SubPath, FString)
}

bool UInterchangeFactoryBaseNode::SetCustomSubPath(const FString& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(SubPath, FString)
}

int32 UInterchangeFactoryBaseNode::GetFactoryDependenciesCount() const
{
	return FactoryDependencies.GetCount();
}

void UInterchangeFactoryBaseNode::GetFactoryDependency(const int32 Index, FString& OutDependency) const
{
	FactoryDependencies.GetItem(Index, OutDependency);
}

void UInterchangeFactoryBaseNode::GetFactoryDependencies(TArray<FString>& OutDependencies) const
{
	FactoryDependencies.GetItems(OutDependencies);
}

bool UInterchangeFactoryBaseNode::AddFactoryDependencyUid(const FString& DependencyUid)
{
	return FactoryDependencies.AddItem(DependencyUid);
}

bool UInterchangeFactoryBaseNode::RemoveFactoryDependencyUid(const FString& DependencyUid)
{
	return FactoryDependencies.RemoveItem(DependencyUid);
}

FString UInterchangeFactoryBaseNode::BuildFactoryNodeUid(const FString& TranslatedNodeUid)
{
	return TEXT("Factory_") + TranslatedNodeUid;
}
