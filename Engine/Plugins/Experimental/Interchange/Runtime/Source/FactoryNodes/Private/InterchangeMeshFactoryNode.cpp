// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeMeshFactoryNode.h"

namespace UE::Interchange
{
	const FAttributeKey& FMeshFactoryNodeStaticData::GetLodDependenciesBaseKey()
	{
		static FAttributeKey LodDependencies_BaseKey = FAttributeKey(TEXT("Lod_Dependencies"));
		return LodDependencies_BaseKey;
	}

	const FAttributeKey& FMeshFactoryNodeStaticData::GetSlotMaterialDependencyBaseKey()
	{
		static FAttributeKey SlotMaterialDependency_BaseKey(TEXT("__SlotMaterialDependency__"));
		return SlotMaterialDependency_BaseKey;
	}
} // namespace UE::Interchange


UInterchangeMeshFactoryNode::UInterchangeMeshFactoryNode()
{
	LodDependencies.Initialize(Attributes, UE::Interchange::FMeshFactoryNodeStaticData::GetLodDependenciesBaseKey().ToString());
	SlotMaterialDependencies.Initialize(Attributes.ToSharedRef(), UE::Interchange::FMeshFactoryNodeStaticData::GetSlotMaterialDependencyBaseKey().ToString());
}

FString UInterchangeMeshFactoryNode::GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const
{
	FString KeyDisplayName = NodeAttributeKey.ToString();
	FString NodeAttributeKeyString = NodeAttributeKey.ToString();
	if (NodeAttributeKey == UE::Interchange::FMeshFactoryNodeStaticData::GetLodDependenciesBaseKey())
	{
		KeyDisplayName = TEXT("LOD Dependencies Count");
		return KeyDisplayName;
	}
	else if (NodeAttributeKeyString.StartsWith(UE::Interchange::FMeshFactoryNodeStaticData::GetLodDependenciesBaseKey().ToString()))
	{
		KeyDisplayName = TEXT("LOD Dependencies Index ");
		const FString IndexKey = UE::Interchange::TArrayAttributeHelper<FString>::IndexKey();
		int32 IndexPosition = NodeAttributeKeyString.Find(IndexKey) + IndexKey.Len();
		if (IndexPosition < NodeAttributeKeyString.Len())
		{
			KeyDisplayName += NodeAttributeKeyString.RightChop(IndexPosition);
		}
		return KeyDisplayName;
	}
	return Super::GetKeyDisplayName(NodeAttributeKey);
}

int32 UInterchangeMeshFactoryNode::GetLodDataCount() const
{
	return LodDependencies.GetCount();
}

void UInterchangeMeshFactoryNode::GetLodDataUniqueIds(TArray<FString>& OutLodDataUniqueIds) const
{
	LodDependencies.GetItems(OutLodDataUniqueIds);
}

bool UInterchangeMeshFactoryNode::AddLodDataUniqueId(const FString& LodDataUniqueId)
{
	return LodDependencies.AddItem(LodDataUniqueId);
}

bool UInterchangeMeshFactoryNode::RemoveLodDataUniqueId(const FString& LodDataUniqueId)
{
	return LodDependencies.RemoveItem(LodDataUniqueId);
}

bool UInterchangeMeshFactoryNode::GetCustomVertexColorReplace(bool& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(VertexColorReplace, bool)
}

bool UInterchangeMeshFactoryNode::SetCustomVertexColorReplace(const bool& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(VertexColorReplace, bool)
}

bool UInterchangeMeshFactoryNode::GetCustomVertexColorIgnore(bool& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(VertexColorIgnore, bool)
}

bool UInterchangeMeshFactoryNode::SetCustomVertexColorIgnore(const bool& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(VertexColorIgnore, bool)
}

bool UInterchangeMeshFactoryNode::GetCustomVertexColorOverride(FColor& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(VertexColorOverride, FColor)
}

bool UInterchangeMeshFactoryNode::SetCustomVertexColorOverride(const FColor& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(VertexColorOverride, FColor)
}

void UInterchangeMeshFactoryNode::GetSlotMaterialDependencies(TMap<FString, FString>& OutMaterialDependencies) const
{
	OutMaterialDependencies = SlotMaterialDependencies.ToMap();
}

bool UInterchangeMeshFactoryNode::GetSlotMaterialDependencyUid(const FString& SlotName, FString& OutMaterialDependency) const
{
	return SlotMaterialDependencies.GetValue(SlotName, OutMaterialDependency);
}

bool UInterchangeMeshFactoryNode::SetSlotMaterialDependencyUid(const FString& SlotName, const FString& MaterialDependencyUid)
{
	return SlotMaterialDependencies.SetKeyValue(SlotName, MaterialDependencyUid);
}

bool UInterchangeMeshFactoryNode::RemoveSlotMaterialDependencyUid(const FString& SlotName)
{
	return SlotMaterialDependencies.RemoveKey(SlotName);
}
