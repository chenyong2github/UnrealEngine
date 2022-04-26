// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeStaticMeshFactoryNode.h"

#if WITH_ENGINE
#include "Engine/StaticMesh.h"
#endif

namespace UE
{
	namespace Interchange
	{
		const FAttributeKey& FStaticMeshNodeStaticData::GetLodDependenciesBaseKey()
		{
			static FAttributeKey LodDependencies_BaseKey = FAttributeKey(TEXT("Lod_Dependencies"));
			return LodDependencies_BaseKey;
		}

		const FAttributeKey& FStaticMeshNodeStaticData::GetSocketUidsBaseKey()
		{
			static FAttributeKey SocketUids_BaseKey = FAttributeKey(TEXT("SocketUids"));
			return SocketUids_BaseKey;
		}
	} // namespace Interchange
} // namespace UE


UInterchangeStaticMeshFactoryNode::UInterchangeStaticMeshFactoryNode()
{
#if WITH_ENGINE
	AssetClass = nullptr;
#endif
	LodDependencies.Initialize(Attributes, UE::Interchange::FStaticMeshNodeStaticData::GetLodDependenciesBaseKey().ToString());
	SocketUids.Initialize(Attributes, UE::Interchange::FStaticMeshNodeStaticData::GetSocketUidsBaseKey().ToString());
}

void UInterchangeStaticMeshFactoryNode::InitializeStaticMeshNode(const FString& UniqueID, const FString& DisplayLabel, const FString& InAssetClass)
{
	bIsNodeClassInitialized = false;
	InitializeNode(UniqueID, DisplayLabel, EInterchangeNodeContainerType::FactoryData);

	FString OperationName = GetTypeName() + TEXT(".SetAssetClassName");
	InterchangePrivateNodeBase::SetCustomAttribute<FString>(*Attributes, ClassNameAttributeKey, OperationName, InAssetClass);
	FillAssetClassFromAttribute();
}

void UInterchangeStaticMeshFactoryNode::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
#if WITH_ENGINE
	if (Ar.IsLoading())
	{
		// Make sure the class is properly set when we compile with engine, this will set the bIsNodeClassInitialized to true.
		SetNodeClassFromClassAttribute();
	}
#endif //#if WITH_ENGINE
}

FString UInterchangeStaticMeshFactoryNode::GetTypeName() const
{
	const FString TypeName = TEXT("StaticMeshNode");
	return TypeName;
}

FString UInterchangeStaticMeshFactoryNode::GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const
{
	FString KeyDisplayName = NodeAttributeKey.ToString();
	FString NodeAttributeKeyString = NodeAttributeKey.ToString();
	if (NodeAttributeKey == UE::Interchange::FStaticMeshNodeStaticData::GetLodDependenciesBaseKey())
	{
		KeyDisplayName = TEXT("LOD Dependencies Count");
		return KeyDisplayName;
	}
	else if (NodeAttributeKeyString.StartsWith(UE::Interchange::FStaticMeshNodeStaticData::GetLodDependenciesBaseKey().ToString()))
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

UClass* UInterchangeStaticMeshFactoryNode::GetObjectClass() const
{
	ensure(bIsNodeClassInitialized);
#if WITH_ENGINE
	return AssetClass.Get() != nullptr ? AssetClass.Get() : UStaticMesh::StaticClass();
#else
	return nullptr;
#endif
}

int32 UInterchangeStaticMeshFactoryNode::GetLodDataCount() const
{
	return LodDependencies.GetCount();
}

void UInterchangeStaticMeshFactoryNode::GetLodDataUniqueIds(TArray<FString>& OutLodDataUniqueIds) const
{
	LodDependencies.GetItems(OutLodDataUniqueIds);
}

bool UInterchangeStaticMeshFactoryNode::AddLodDataUniqueId(const FString& LodDataUniqueId)
{
	return LodDependencies.AddItem(LodDataUniqueId);
}

bool UInterchangeStaticMeshFactoryNode::RemoveLodDataUniqueId(const FString& LodDataUniqueId)
{
	return LodDependencies.RemoveItem(LodDataUniqueId);
}

int32 UInterchangeStaticMeshFactoryNode::GetSocketUidCount() const
{
	return SocketUids.GetCount();
}

void UInterchangeStaticMeshFactoryNode::GetSocketUids(TArray<FString>& OutSocketUids) const
{
	SocketUids.GetItems(OutSocketUids);
}

bool UInterchangeStaticMeshFactoryNode::AddSocketUid(const FString& SocketUid)
{
	return SocketUids.AddItem(SocketUid);
}

bool UInterchangeStaticMeshFactoryNode::AddSocketUids(const TArray<FString>& InSocketUids)
{
	for (const FString& SocketUid : InSocketUids)
	{
		if (!SocketUids.AddItem(SocketUid))
		{
			return false;
		}
	}

	return true;
}

bool UInterchangeStaticMeshFactoryNode::RemoveSocketUd(const FString& SocketUid)
{
	return SocketUids.RemoveItem(SocketUid);
}

bool UInterchangeStaticMeshFactoryNode::GetCustomVertexColorReplace(bool& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(VertexColorReplace, bool)
}

bool UInterchangeStaticMeshFactoryNode::SetCustomVertexColorReplace(const bool& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(VertexColorReplace, bool)
}

bool UInterchangeStaticMeshFactoryNode::GetCustomVertexColorIgnore(bool& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(VertexColorIgnore, bool)
}

bool UInterchangeStaticMeshFactoryNode::SetCustomVertexColorIgnore(const bool& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(VertexColorIgnore, bool)
}

bool UInterchangeStaticMeshFactoryNode::GetCustomVertexColorOverride(FColor& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(VertexColorOverride, FColor)
}

bool UInterchangeStaticMeshFactoryNode::SetCustomVertexColorOverride(const FColor& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(VertexColorOverride, FColor)
}

void UInterchangeStaticMeshFactoryNode::FillAssetClassFromAttribute()
{
#if WITH_ENGINE
	FString OperationName = GetTypeName() + TEXT(".GetAssetClassName");
	FString ClassName;
	InterchangePrivateNodeBase::GetCustomAttribute<FString>(*Attributes, ClassNameAttributeKey, OperationName, ClassName);
	if (ClassName.Equals(UStaticMesh::StaticClass()->GetName()))
	{
		AssetClass = UStaticMesh::StaticClass();
		bIsNodeClassInitialized = true;
	}
#endif
}

bool UInterchangeStaticMeshFactoryNode::SetNodeClassFromClassAttribute()
{
	if (!bIsNodeClassInitialized)
	{
		FillAssetClassFromAttribute();
	}
	return bIsNodeClassInitialized;
}

bool UInterchangeStaticMeshFactoryNode::IsEditorOnlyDataDefined()
{
#if WITH_EDITORONLY_DATA
	return true;
#else
	return false;
#endif
}
