// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNode.h"

#if WITH_ENGINE

#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"

#endif

#include "InterchangeMaterialFactoryNode.generated.h"

UCLASS(BlueprintType)
class INTERCHANGENODES_API UInterchangeMaterialFactoryNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	UInterchangeMaterialFactoryNode()
	:UInterchangeBaseNode()
	{
#if WITH_ENGINE
		AssetClass = nullptr;
#endif
		TextureDependencies.Initialize(Attributes, TextureDependenciesKey.Key);
	}

	/**
	 * Initialize node data
	 * @param: UniqueID - The uniqueId for this node
	 * @param DisplayLabel - The name of the node
	 * @param InAssetClass - The class the material factory will create for this node.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	void InitializeMaterialNode(const FString& UniqueID, const FString& DisplayLabel, const FString& InAssetClass)
	{
		bIsMaterialNodeClassInitialized = false;
		InitializeNode(UniqueID, DisplayLabel, EInterchangeNodeContainerType::NodeContainerType_FactoryData);
		FString OperationName = GetTypeName() + TEXT(".SetAssetClassName");
		InterchangePrivateNodeBase::SetCustomAttribute<FString>(*Attributes, ClassNameAttributeKey, OperationName, InAssetClass);
		FillAssetClassFromAttribute();
	}

	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const override
	{
		const FString TypeName = TEXT("MaterialFactoryNode");
		return TypeName;
	}

	/** Get the class this node want to create */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	virtual class UClass* GetAssetClass() const override
	{
		ensure(bIsMaterialNodeClassInitialized);
#if WITH_ENGINE
		return AssetClass.Get() != nullptr ? AssetClass.Get() : UMaterial::StaticClass();
#else
		return nullptr;
#endif
	}

	virtual FString GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override
	{
		FString KeyDisplayName = NodeAttributeKey.ToString();
		if (NodeAttributeKey == TextureDependenciesKey)
		{
			return KeyDisplayName = TEXT("Texture Dependencies count");
		}
		else if (NodeAttributeKey.Key.StartsWith(TextureDependenciesKey.Key))
		{
			KeyDisplayName = TEXT("Texture Dependencies Index ");
			const FString IndexKey = UE::Interchange::FNameAttributeArrayHelper::IndexKey();
			int32 IndexPosition = NodeAttributeKey.Key.Find(IndexKey) + IndexKey.Len();
			if (IndexPosition < NodeAttributeKey.Key.Len())
			{
				KeyDisplayName += NodeAttributeKey.Key.RightChop(IndexPosition);
			}
			return KeyDisplayName;
		}
		return Super::GetKeyDisplayName(NodeAttributeKey);
	}

	virtual FString GetAttributeCategory(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override
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

	virtual FGuid GetHash() const override
	{
		return Attributes->GetStorageHash();
	}

	static FString GetMaterialFactoryNodeUidFromMaterialNodeUid(const FString& TranslatedNodeUid)
	{
		FString NewUid = TEXT("Factory_") + TranslatedNodeUid;
		return NewUid;
	}

	/**
	 * This function allow to retrieve the number of Texture dependencies for this object.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	int32 GetTextureDependeciesCount() const
	{
		return TextureDependencies.GetCount();
	}

	/**
	 * This function allow to retrieve the Texture dependency for this object.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	void GetTextureDependencies(TArray<FString>& OutDependencies) const
	{
		TextureDependencies.GetNames(OutDependencies);
	}

	/**
	 * This function allow to retrieve one Texture dependency for this object.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	void GetTextureDependency(const int32 Index, FString& OutDependency) const
	{
		TextureDependencies.GetName(Index, OutDependency);
	}

	/**
	 * Add one Texture dependency to this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool SetTextureDependencyUid(const FString& DependencyUid)
	{
		return TextureDependencies.AddName(DependencyUid);
	}

	/**
	 * Remove one Texture dependency from this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool RemoveTextureDependencyUid(const FString& DependencyUid)
	{
		return TextureDependencies.RemoveName(DependencyUid);
	}

	/**
	 * Get the translated texture node unique ID.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool GetCustomTranslatedMaterialNodeUid(FString& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(TranslatedMaterialNodeUid, FString);
	}

	/**
	 * Set the translated texture node unique ID. This is the reference to the node that was create by the translator and this node is needed to get the texture payload.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool SetCustomTranslatedMaterialNodeUid(const FString& AttributeValue)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(TranslatedMaterialNodeUid, FString);
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool GetCustomMaterialUsage(uint8& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(MaterialUsage, uint8);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool SetCustomMaterialUsage(const uint8& AttributeValue, bool bAddApplyDelegate = true)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeMaterialFactoryNode, MaterialUsage, uint8, UMaterialInterface);
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool GetCustomBlendMode(uint8& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(BlendMode, uint8);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool SetCustomBlendMode(const uint8& AttributeValue, bool bAddApplyDelegate = true)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeMaterialFactoryNode, BlendMode, uint8, UMaterialInterface);
	}

	virtual void Serialize(FArchive& Ar) override
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

private:

	void FillAssetClassFromAttribute()
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

	bool SetMaterialNodeClassFromClassAttribute()
	{
		if (!bIsMaterialNodeClassInitialized)
		{
			FillAssetClassFromAttribute();
		}
		return bIsMaterialNodeClassInitialized;
	}

	const UE::Interchange::FAttributeKey ClassNameAttributeKey = UE::Interchange::FBaseNodeStaticData::ClassTypeAttributeKey();
	
	//Translated texture node unique id
	const UE::Interchange::FAttributeKey Macro_CustomTranslatedMaterialNodeUidKey = UE::Interchange::FAttributeKey(TEXT("TranslatedMaterialNodeUid"));

	//Material Attribute
	const UE::Interchange::FAttributeKey Macro_CustomMaterialUsageKey = UE::Interchange::FAttributeKey(TEXT("MaterialUsage"));
	const UE::Interchange::FAttributeKey Macro_CustomBlendModeKey = UE::Interchange::FAttributeKey(TEXT("BlendMode"));

	const UE::Interchange::FAttributeKey TextureDependenciesKey = UE::Interchange::FAttributeKey(TEXT("__TextureDependenciesKey__"));


#if WITH_ENGINE

	//Material Ussage is not handle properly, you can have several usage check
	//Currently I can set only one and retrieve the first, TODO make this work (maybe one key per usage...)
	bool ApplyCustomMaterialUsageToAsset(UObject * Asset) const
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

	bool FillCustomMaterialUsageFromAsset(UObject* Asset)
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
#endif

	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(BlendMode, uint8, UMaterial, TEnumAsByte<enum EBlendMode>);

protected:
#if WITH_ENGINE
	TSubclassOf<UTexture> AssetClass = nullptr;
#endif
	bool bIsMaterialNodeClassInitialized = false;

	UE::Interchange::FNameAttributeArrayHelper TextureDependencies;
};
