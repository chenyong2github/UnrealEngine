// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeMaterialFactoryNode.h"

#if WITH_ENGINE

#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"

#endif


UInterchangeMaterialFactoryNode::UInterchangeMaterialFactoryNode()
{
#if WITH_ENGINE
	AssetClass = nullptr;
#endif
	TextureDependencies.Initialize(Attributes, TextureDependenciesKey.Key);
}

void UInterchangeMaterialFactoryNode::InitializeMaterialNode(const FString& UniqueID, const FString& DisplayLabel, const FString& InAssetClass)
{
	bIsMaterialNodeClassInitialized = false;
	InitializeNode(UniqueID, DisplayLabel, EInterchangeNodeContainerType::FactoryData);
	FString OperationName = GetTypeName() + TEXT(".SetAssetClassName");
	InterchangePrivateNodeBase::SetCustomAttribute<FString>(*Attributes, ClassNameAttributeKey, OperationName, InAssetClass);
	FillAssetClassFromAttribute();
}

FString UInterchangeMaterialFactoryNode::GetTypeName() const
{
	const FString TypeName = TEXT("MaterialFactoryNode");
	return TypeName;
}

UClass* UInterchangeMaterialFactoryNode::GetObjectClass() const
{
	ensure(bIsMaterialNodeClassInitialized);
#if WITH_ENGINE
	return AssetClass.Get() != nullptr ? AssetClass.Get() : UMaterial::StaticClass();
#else
	return nullptr;
#endif
}

FString UInterchangeMaterialFactoryNode::GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const
{
	FString KeyDisplayName = NodeAttributeKey.ToString();
	if (NodeAttributeKey == TextureDependenciesKey)
	{
		return KeyDisplayName = TEXT("Texture Dependencies count");
	}
	else if (NodeAttributeKey.Key.StartsWith(TextureDependenciesKey.Key))
	{
		KeyDisplayName = TEXT("Texture Dependencies Index ");
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

FString UInterchangeMaterialFactoryNode::GetAttributeCategory(const UE::Interchange::FAttributeKey& NodeAttributeKey) const
{
	if (NodeAttributeKey == Macro_CustomTranslatedMaterialNodeUidKey
		|| NodeAttributeKey == Macro_CustomMaterialUsageKey
		|| NodeAttributeKey == Macro_CustomBlendModeKey)
	{
		return FString(TEXT("MaterialFactory"));
	}
	else if (NodeAttributeKey.Key.StartsWith(TextureDependenciesKey.Key))
	{
		return FString(TEXT("TextureDependencies"));
	}
	return Super::GetAttributeCategory(NodeAttributeKey);
}

FString UInterchangeMaterialFactoryNode::GetMaterialFactoryNodeUidFromMaterialNodeUid(const FString& TranslatedNodeUid)
{
	FString NewUid = TEXT("Factory_") + TranslatedNodeUid;
	return NewUid;
}

int32 UInterchangeMaterialFactoryNode::GetTextureDependeciesCount() const
{
	return TextureDependencies.GetCount();
}

void UInterchangeMaterialFactoryNode::GetTextureDependencies(TArray<FString>& OutDependencies) const
{
	TextureDependencies.GetItems(OutDependencies);
}

void UInterchangeMaterialFactoryNode::GetTextureDependency(const int32 Index, FString& OutDependency) const
{
	TextureDependencies.GetItem(Index, OutDependency);
}

bool UInterchangeMaterialFactoryNode::SetTextureDependencyUid(const FString& DependencyUid)
{
	return TextureDependencies.AddItem(DependencyUid);
}

bool UInterchangeMaterialFactoryNode::RemoveTextureDependencyUid(const FString& DependencyUid)
{
	return TextureDependencies.RemoveItem(DependencyUid);
}

bool UInterchangeMaterialFactoryNode::GetCustomTranslatedMaterialNodeUid(FString& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(TranslatedMaterialNodeUid, FString);
}

bool UInterchangeMaterialFactoryNode::SetCustomTranslatedMaterialNodeUid(const FString& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(TranslatedMaterialNodeUid, FString);
}

bool UInterchangeMaterialFactoryNode::GetCustomMaterialUsage(uint8& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(MaterialUsage, uint8);
}

bool UInterchangeMaterialFactoryNode::SetCustomMaterialUsage(const uint8& AttributeValue, bool bAddApplyDelegate /*= true*/)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeMaterialFactoryNode, MaterialUsage, uint8, UMaterialInterface);
}

bool UInterchangeMaterialFactoryNode::GetCustomBlendMode(uint8& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(BlendMode, uint8);
}

bool UInterchangeMaterialFactoryNode::SetCustomBlendMode(const uint8& AttributeValue, bool bAddApplyDelegate /*= true*/)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeMaterialFactoryNode, BlendMode, uint8, UMaterialInterface);
}

void UInterchangeMaterialFactoryNode::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
#if WITH_ENGINE
	if (Ar.IsLoading())
	{
		//Make sure the class is properly set when we compile with engine, this will set the
		//bIsMaterialNodeClassInitialized to true.
		SetMaterialNodeClassFromClassAttribute();
	}
#endif //#if WITH_ENGINE
}

void UInterchangeMaterialFactoryNode::FillAssetClassFromAttribute()
{
#if WITH_ENGINE
	FString OperationName = GetTypeName() + TEXT(".GetAssetClassName");
	FString ClassName;
	InterchangePrivateNodeBase::GetCustomAttribute<FString>(*Attributes, ClassNameAttributeKey, OperationName, ClassName);
	if (ClassName.Equals(UMaterial::StaticClass()->GetName()))
	{
		AssetClass = UMaterial::StaticClass();
		bIsMaterialNodeClassInitialized = true;
	}
	else if (ClassName.Equals(UMaterialInstance::StaticClass()->GetName()))
	{
		AssetClass = UMaterialInstance::StaticClass();
		bIsMaterialNodeClassInitialized = true;
	}
#endif
}

bool UInterchangeMaterialFactoryNode::SetMaterialNodeClassFromClassAttribute()
{
	if (!bIsMaterialNodeClassInitialized)
	{
		FillAssetClassFromAttribute();
	}
	return bIsMaterialNodeClassInitialized;
}

#if WITH_ENGINE

bool UInterchangeMaterialFactoryNode::ApplyCustomMaterialUsageToAsset(UObject* Asset) const
{
	if (!Asset)
	{
		return false;
	}
	UMaterialInterface* TypedObject = Cast<UMaterialInterface>(Asset);
	if (!TypedObject)
	{
		return false;
	}
	uint8 ValueData;
	if (GetCustomMaterialUsage(ValueData))
	{
		TypedObject->CheckMaterialUsage_Concurrent(EMaterialUsage(ValueData));
		return true;
	}
	return false;
}

bool UInterchangeMaterialFactoryNode::FillCustomMaterialUsageFromAsset(UObject* Asset)
{
	if (!Asset)
	{
		return false;
	}
	UMaterialInterface* TypedObject = Cast<UMaterialInterface>(Asset);
	if (!TypedObject)
	{
		return false;
	}
	const UMaterial* Material = TypedObject->GetMaterial_Concurrent();
	if (!Material)
	{
		return false;
	}
	for (int32 Usage = 0; Usage < MATUSAGE_MAX; Usage++)
	{
		const EMaterialUsage UsageEnum = (EMaterialUsage)Usage;
		if (Material->GetUsageByFlag(UsageEnum))
		{
			if (SetCustomMaterialUsage((int8)UsageEnum, false))
			{
				return true;
			}
		}
	}
	return false;
}

#endif //WITH_ENGINE