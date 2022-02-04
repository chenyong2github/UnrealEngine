// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeSkeletalMeshFactoryNode.h"

#if WITH_ENGINE
#include "Engine/SkeletalMesh.h"
#endif

//Interchange namespace
namespace UE
{
	namespace Interchange
	{
		const FString& FSkeletalMeshNodeStaticData::GetLodDependenciesBaseKey()
		{
			static FString LodDependencies_BaseKey = TEXT("Lod_Dependencies");
			return LodDependencies_BaseKey;
		}
	}//ns Interchange
}//ns UE

UInterchangeSkeletalMeshFactoryNode::UInterchangeSkeletalMeshFactoryNode()
{
#if WITH_ENGINE
	AssetClass = nullptr;
#endif
	LodDependencies.Initialize(Attributes, UE::Interchange::FSkeletalMeshNodeStaticData::GetLodDependenciesBaseKey());
}

void UInterchangeSkeletalMeshFactoryNode::InitializeSkeletalMeshNode(const FString& UniqueID, const FString& DisplayLabel, const FString& InAssetClass)
{
	bIsNodeClassInitialized = false;
	InitializeNode(UniqueID, DisplayLabel, EInterchangeNodeContainerType::FactoryData);

	FString OperationName = GetTypeName() + TEXT(".SetAssetClassName");
	InterchangePrivateNodeBase::SetCustomAttribute<FString>(*Attributes, ClassNameAttributeKey, OperationName, InAssetClass);
	FillAssetClassFromAttribute();
}

void UInterchangeSkeletalMeshFactoryNode::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
#if WITH_ENGINE
	if (Ar.IsLoading())
	{
		//Make sure the class is properly set when we compile with engine, this will set the
		//bIsNodeClassInitialized to true.
		SetNodeClassFromClassAttribute();
	}
#endif //#if WITH_ENGINE
}

FString UInterchangeSkeletalMeshFactoryNode::GetTypeName() const
{
	const FString TypeName = TEXT("SkeletalMeshNode");
	return TypeName;
}

FString UInterchangeSkeletalMeshFactoryNode::GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const
{
	FString KeyDisplayName = NodeAttributeKey.Key;
	if (NodeAttributeKey.Key.Equals(UE::Interchange::FSkeletalMeshNodeStaticData::GetLodDependenciesBaseKey()))
	{
		KeyDisplayName = TEXT("LOD Dependencies Count");
		return KeyDisplayName;
	}
	else if (NodeAttributeKey.Key.StartsWith(UE::Interchange::FSkeletalMeshNodeStaticData::GetLodDependenciesBaseKey()))
	{
		KeyDisplayName = TEXT("LOD Dependencies Index ");
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

UClass* UInterchangeSkeletalMeshFactoryNode::GetObjectClass() const
{
	ensure(bIsNodeClassInitialized);
#if WITH_ENGINE
	return AssetClass.Get() != nullptr ? AssetClass.Get() : USkeletalMesh::StaticClass();
#else
	return nullptr;
#endif
}

int32 UInterchangeSkeletalMeshFactoryNode::GetLodDataCount() const
{
	return LodDependencies.GetCount();
}

void UInterchangeSkeletalMeshFactoryNode::GetLodDataUniqueIds(TArray<FString>& OutLodDataUniqueIds) const
{
	LodDependencies.GetItems(OutLodDataUniqueIds);
}

bool UInterchangeSkeletalMeshFactoryNode::AddLodDataUniqueId(const FString& LodDataUniqueId)
{
	return LodDependencies.AddItem(LodDataUniqueId);
}

bool UInterchangeSkeletalMeshFactoryNode::RemoveLodDataUniqueId(const FString& LodDataUniqueId)
{
	return LodDependencies.RemoveItem(LodDataUniqueId);
}

bool UInterchangeSkeletalMeshFactoryNode::GetCustomSkeletonSoftObjectPath(FSoftObjectPath& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(SkeletonSoftObjectPath, FSoftObjectPath)
}

bool UInterchangeSkeletalMeshFactoryNode::SetCustomSkeletonSoftObjectPath(const FSoftObjectPath& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(SkeletonSoftObjectPath, FSoftObjectPath)
}

bool UInterchangeSkeletalMeshFactoryNode::GetCustomImportMorphTarget(bool& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(ImportMorphTarget, bool)
}

bool UInterchangeSkeletalMeshFactoryNode::SetCustomImportMorphTarget(const bool& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(ImportMorphTarget, bool)
}

bool UInterchangeSkeletalMeshFactoryNode::GetCustomCreatePhysicsAsset(bool& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(CreatePhysicsAsset, bool)
}

bool UInterchangeSkeletalMeshFactoryNode::SetCustomCreatePhysicsAsset(const bool& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(CreatePhysicsAsset, bool)
}

bool UInterchangeSkeletalMeshFactoryNode::GetCustomPhysicAssetSoftObjectPath(FSoftObjectPath& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(PhysicAssetSoftObjectPath, FSoftObjectPath)
}

bool UInterchangeSkeletalMeshFactoryNode::SetCustomPhysicAssetSoftObjectPath(const FSoftObjectPath& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(PhysicAssetSoftObjectPath, FSoftObjectPath)
}

bool UInterchangeSkeletalMeshFactoryNode::GetCustomVertexColorReplace(bool& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(VertexColorReplace, bool)
}

bool UInterchangeSkeletalMeshFactoryNode::SetCustomVertexColorReplace(const bool& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(VertexColorReplace, bool)
}

bool UInterchangeSkeletalMeshFactoryNode::GetCustomVertexColorIgnore(bool& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(VertexColorIgnore, bool)
}

bool UInterchangeSkeletalMeshFactoryNode::SetCustomVertexColorIgnore(const bool& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(VertexColorIgnore, bool)
}

bool UInterchangeSkeletalMeshFactoryNode::GetCustomVertexColorOverride(FColor& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(VertexColorOverride, FColor)
}

bool UInterchangeSkeletalMeshFactoryNode::SetCustomVertexColorOverride(const FColor& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(VertexColorOverride, FColor)
}

void UInterchangeSkeletalMeshFactoryNode::FillAssetClassFromAttribute()
{
#if WITH_ENGINE
	FString OperationName = GetTypeName() + TEXT(".GetAssetClassName");
	FString ClassName;
	InterchangePrivateNodeBase::GetCustomAttribute<FString>(*Attributes, ClassNameAttributeKey, OperationName, ClassName);
	if (ClassName.Equals(USkeletalMesh::StaticClass()->GetName()))
	{
		AssetClass = USkeletalMesh::StaticClass();
		bIsNodeClassInitialized = true;
	}
#endif
}

bool UInterchangeSkeletalMeshFactoryNode::SetNodeClassFromClassAttribute()
{
	if (!bIsNodeClassInitialized)
	{
		FillAssetClassFromAttribute();
	}
	return bIsNodeClassInitialized;
}

bool UInterchangeSkeletalMeshFactoryNode::IsEditorOnlyDataDefined()
{
#if WITH_EDITORONLY_DATA
	return true;
#else
	return false;
#endif
}
