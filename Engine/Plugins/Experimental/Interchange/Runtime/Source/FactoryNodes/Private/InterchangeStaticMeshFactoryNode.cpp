// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeStaticMeshFactoryNode.h"

#if WITH_ENGINE
#include "Engine/StaticMesh.h"
#endif

namespace UE
{
	namespace Interchange
	{
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

FString UInterchangeStaticMeshFactoryNode::GetTypeName() const
{
	const FString TypeName = TEXT("StaticMeshNode");
	return TypeName;
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
