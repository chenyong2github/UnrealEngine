// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeSceneNode.h"

//Interchange namespace
namespace UE
{
	namespace Interchange
	{

		const FString& FSceneNodeStaticData::GetNodeSpecializeTypeBaseKey()
		{
			static FString SceneNodeSpecializeType_BaseKey = TEXT("SceneNodeSpecializeType");
			return SceneNodeSpecializeType_BaseKey;
		}

		const FString& FSceneNodeStaticData::GetMaterialDependencyUidsBaseKey()
		{
			static FString MaterialDependencyUids_BaseKey = TEXT("__MaterialDependencyUidsBaseKey__");
			return MaterialDependencyUids_BaseKey;
		}

		const FString& FSceneNodeStaticData::GetJointSpecializeTypeString()
		{
			static FString JointSpecializeTypeString = TEXT("Joint");
			return JointSpecializeTypeString;
		}

		const FString& FSceneNodeStaticData::GetLodGroupSpecializeTypeString()
		{
			static FString JointSpecializeTypeString = TEXT("LodGroup");
			return JointSpecializeTypeString;
		}
	}//ns Interchange
}//ns UE

UInterchangeSceneNode::UInterchangeSceneNode()
{
	NodeSpecializeTypes.Initialize(Attributes, UE::Interchange::FSceneNodeStaticData::GetNodeSpecializeTypeBaseKey());
	MaterialDependencyUids.Initialize(Attributes, UE::Interchange::FSceneNodeStaticData::GetMaterialDependencyUidsBaseKey());
}

/**
	* Return the node type name of the class, we use this when reporting error
	*/
FString UInterchangeSceneNode::GetTypeName() const
{
	const FString TypeName = TEXT("SceneNode");
	return TypeName;
}

FString UInterchangeSceneNode::GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const
{
	FString KeyDisplayName = NodeAttributeKey.Key;
	if (NodeAttributeKey.Key.Equals(UE::Interchange::FSceneNodeStaticData::GetNodeSpecializeTypeBaseKey()))
	{
		KeyDisplayName = TEXT("Specialized type count");
		return KeyDisplayName;
	}
	else if (NodeAttributeKey.Key.StartsWith(UE::Interchange::FSceneNodeStaticData::GetNodeSpecializeTypeBaseKey()))
	{
		KeyDisplayName = TEXT("Specialized type index ");
		const FString IndexKey = UE::Interchange::TArrayAttributeHelper<FString>::IndexKey();
		int32 IndexPosition = NodeAttributeKey.Key.Find(IndexKey) + IndexKey.Len();
		if (IndexPosition < NodeAttributeKey.Key.Len())
		{
			KeyDisplayName += NodeAttributeKey.Key.RightChop(IndexPosition);
		}
		return KeyDisplayName;
	}
	else if (NodeAttributeKey.Key.Equals(UE::Interchange::FSceneNodeStaticData::GetMaterialDependencyUidsBaseKey()))
	{
		KeyDisplayName = TEXT("Material dependencies count");
		return KeyDisplayName;
	}
	else if (NodeAttributeKey.Key.StartsWith(UE::Interchange::FSceneNodeStaticData::GetMaterialDependencyUidsBaseKey()))
	{
		KeyDisplayName = TEXT("Material dependency index ");
		const FString IndexKey = UE::Interchange::TArrayAttributeHelper<FString>::IndexKey();
		int32 IndexPosition = NodeAttributeKey.Key.Find(IndexKey) + IndexKey.Len();
		if (IndexPosition < NodeAttributeKey.Key.Len())
		{
			KeyDisplayName += NodeAttributeKey.Key.RightChop(IndexPosition);
		}
		return KeyDisplayName;
	}
	return Super::GetKeyDisplayName(NodeAttributeKey);
}

FString UInterchangeSceneNode::GetAttributeCategory(const UE::Interchange::FAttributeKey& NodeAttributeKey) const
{
	if (NodeAttributeKey.Key.StartsWith(UE::Interchange::FSceneNodeStaticData::GetNodeSpecializeTypeBaseKey()))
	{
		return FString(TEXT("SpecializeType"));
	}
	else if (NodeAttributeKey.Key.StartsWith(UE::Interchange::FSceneNodeStaticData::GetMaterialDependencyUidsBaseKey()))
	{
		return FString(TEXT("MaterialDependencies"));
	}
	else if (NodeAttributeKey == Macro_CustomLocalTransformKey
		|| NodeAttributeKey == Macro_CustomGlobalTransformKey
		|| NodeAttributeKey == Macro_CustomAssetInstanceUidKey)
	{
		return FString(TEXT("Scene"));
	}
	return Super::GetAttributeCategory(NodeAttributeKey);
}

FName UInterchangeSceneNode::GetIconName() const
{
	FString SpecializedType;
	GetSpecializedType(0, SpecializedType);
	if (SpecializedType.IsEmpty())
	{
		return NAME_None;
	}
	SpecializedType = TEXT("SceneGraphIcon.") + SpecializedType;
	return FName(*SpecializedType);
}

bool UInterchangeSceneNode::IsSpecializedTypeContains(const FString& SpecializedType) const
{
	TArray<FString> SpecializedTypes;
	GetSpecializedTypes(SpecializedTypes);
	for (const FString& SpecializedTypeRef : SpecializedTypes)
	{
		if (SpecializedTypeRef.Equals(SpecializedType))
		{
			return true;
		}
	}
	return false;
}

int32 UInterchangeSceneNode::GetSpecializedTypeCount() const
{
	return NodeSpecializeTypes.GetCount();
}

void UInterchangeSceneNode::GetSpecializedType(const int32 Index, FString& OutSpecializedType) const
{
	NodeSpecializeTypes.GetItem(Index, OutSpecializedType);
}

void UInterchangeSceneNode::GetSpecializedTypes(TArray<FString>& OutSpecializedTypes) const
{
	NodeSpecializeTypes.GetItems(OutSpecializedTypes);
}

bool UInterchangeSceneNode::AddSpecializedType(const FString& SpecializedType)
{
	return NodeSpecializeTypes.AddItem(SpecializedType);
}

bool UInterchangeSceneNode::RemoveSpecializedType(const FString& SpecializedType)
{
	return NodeSpecializeTypes.RemoveItem(SpecializedType);
}

int32 UInterchangeSceneNode::GetMaterialDependencyUidsCount() const
{
	return MaterialDependencyUids.GetCount();
}

void UInterchangeSceneNode::GetMaterialDependencyUid(const int32 Index, FString& OutMaterialDependencyUid) const
{
	MaterialDependencyUids.GetItem(Index, OutMaterialDependencyUid);
}

void UInterchangeSceneNode::GetMaterialDependencyUids(TArray<FString>& OutMaterialDependencyUids) const
{
	MaterialDependencyUids.GetItems(OutMaterialDependencyUids);
}

bool UInterchangeSceneNode::AddMaterialDependencyUid(const FString& MaterialDependencyUid)
{
	return MaterialDependencyUids.AddItem(MaterialDependencyUid);
}

bool UInterchangeSceneNode::RemoveMaterialDependencyUid(const FString& MaterialDependencyUid)
{
	return MaterialDependencyUids.RemoveItem(MaterialDependencyUid);
}

bool UInterchangeSceneNode::GetCustomLocalTransform(FTransform& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(LocalTransform, FTransform);
}

bool UInterchangeSceneNode::SetCustomLocalTransform(const FTransform& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(LocalTransform, FTransform);
}

bool UInterchangeSceneNode::GetCustomGlobalTransform(FTransform& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(GlobalTransform, FTransform);
}

bool UInterchangeSceneNode::SetCustomGlobalTransform(const FTransform& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(GlobalTransform, FTransform);
}

bool UInterchangeSceneNode::GetCustomBindPoseLocalTransform(FTransform& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(BindPoseLocalTransform, FTransform);
}

bool UInterchangeSceneNode::SetCustomBindPoseLocalTransform(const FTransform& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(BindPoseLocalTransform, FTransform);
}

bool UInterchangeSceneNode::GetCustomBindPoseGlobalTransform(FTransform& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(BindPoseGlobalTransform, FTransform);
}

bool UInterchangeSceneNode::SetCustomBindPoseGlobalTransform(const FTransform& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(BindPoseGlobalTransform, FTransform);
}

bool UInterchangeSceneNode::GetCustomTimeZeroLocalTransform(FTransform& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(TimeZeroLocalTransform, FTransform);
}

bool UInterchangeSceneNode::SetCustomTimeZeroLocalTransform(const FTransform& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(TimeZeroLocalTransform, FTransform);
}

bool UInterchangeSceneNode::GetCustomTimeZeroGlobalTransform(FTransform& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(TimeZeroGlobalTransform, FTransform);
}

bool UInterchangeSceneNode::SetCustomTimeZeroGlobalTransform(const FTransform& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(TimeZeroGlobalTransform, FTransform);
}

bool UInterchangeSceneNode::GetCustomGeometricTransform(FTransform& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(GeometricTransform, FTransform);
}

bool UInterchangeSceneNode::SetCustomGeometricTransform(const FTransform& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(GeometricTransform, FTransform);
}

bool UInterchangeSceneNode::GetCustomAssetInstanceUid(FString& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(AssetInstanceUid, FString);
}

bool UInterchangeSceneNode::SetCustomAssetInstanceUid(const FString& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(AssetInstanceUid, FString);
}

