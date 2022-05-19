// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeSkeletalMeshFactoryNode.h"

#if WITH_ENGINE
#include "Engine/SkeletalMesh.h"
#endif

UInterchangeSkeletalMeshFactoryNode::UInterchangeSkeletalMeshFactoryNode()
{
#if WITH_ENGINE
	AssetClass = nullptr;
#endif
}

void UInterchangeSkeletalMeshFactoryNode::InitializeSkeletalMeshNode(const FString& UniqueID, const FString& DisplayLabel, const FString& InAssetClass)
{
	bIsNodeClassInitialized = false;
	InitializeNode(UniqueID, DisplayLabel, EInterchangeNodeContainerType::FactoryData);

	FString OperationName = GetTypeName() + TEXT(".SetAssetClassName");
	InterchangePrivateNodeBase::SetCustomAttribute<FString>(*Attributes, ClassNameAttributeKey, OperationName, InAssetClass);
	FillAssetClassFromAttribute();
}

FString UInterchangeSkeletalMeshFactoryNode::GetTypeName() const
{
	const FString TypeName = TEXT("SkeletalMeshNode");
	return TypeName;
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

bool UInterchangeSkeletalMeshFactoryNode::GetCustomImportContentType(EInterchangeSkeletalMeshContentType& AttributeValue) const
{
	FString OperationName = GetTypeName() + TEXT(".GetImportContentType");
	uint8 EnumRawValue = 0;
	const bool bResult = InterchangePrivateNodeBase::GetCustomAttribute<uint8>(*Attributes, Macro_CustomImportContentTypeKey, OperationName, EnumRawValue);
	if (bResult)
	{
		AttributeValue = static_cast<EInterchangeSkeletalMeshContentType>(EnumRawValue);
	}
	return bResult;
}

bool UInterchangeSkeletalMeshFactoryNode::SetCustomImportContentType(const EInterchangeSkeletalMeshContentType& AttributeValue)
{
	FString OperationName = GetTypeName() + TEXT(".SetImportContentType");
	uint8 EnumRawValue = static_cast<uint8>(AttributeValue);
	return InterchangePrivateNodeBase::SetCustomAttribute<uint8>(*Attributes, Macro_CustomImportContentTypeKey, OperationName, EnumRawValue);
}

void UInterchangeSkeletalMeshFactoryNode::AppendAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	Super::AppendAssetRegistryTags(OutTags);
#if WITH_EDITORONLY_DATA
	EInterchangeSkeletalMeshContentType ContentType;
	if (GetCustomImportContentType(ContentType))
	{
		auto ImportContentTypeToString = [](const EInterchangeSkeletalMeshContentType value)-> FString
		{
			switch (value)
			{
			case EInterchangeSkeletalMeshContentType::All:
				return NSSkeletalMeshSourceFileLabels::GeoAndSkinningMetaDataValue();
			case EInterchangeSkeletalMeshContentType::Geometry:
				return NSSkeletalMeshSourceFileLabels::GeometryMetaDataValue();
			case EInterchangeSkeletalMeshContentType::SkinningWeights:
				return NSSkeletalMeshSourceFileLabels::SkinningMetaDataValue();
			}
			return NSSkeletalMeshSourceFileLabels::GeoAndSkinningMetaDataValue();
		};

		FString EnumString = ImportContentTypeToString(ContentType);
		OutTags.Add(FAssetRegistryTag(NSSkeletalMeshSourceFileLabels::GetSkeletalMeshLastImportContentTypeMetadataKey(), EnumString, FAssetRegistryTag::TT_Hidden));
	}
#endif
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
