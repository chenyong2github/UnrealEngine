// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNode.h"
#include "UObject/UObjectGlobals.h"

#if WITH_ENGINE
#include "Materials/MaterialInterface.h"
#endif

#include "InterchangeMaterialFactoryNode.generated.h"

UCLASS(BlueprintType, Experimental)
class INTERCHANGENODES_API UInterchangeMaterialFactoryNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	UInterchangeMaterialFactoryNode();

	/**
	 * Initialize node data
	 * @param: UniqueID - The uniqueId for this node
	 * @param DisplayLabel - The name of the node
	 * @param InAssetClass - The class the material factory will create for this node.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	void InitializeMaterialNode(const FString& UniqueID, const FString& DisplayLabel, const FString& InAssetClass);

	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const override;

	/** Get the class this node want to create */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	virtual class UClass* GetObjectClass() const override;

	virtual FString GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override;

	virtual FString GetAttributeCategory(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override;

	static FString GetMaterialFactoryNodeUidFromMaterialNodeUid(const FString& TranslatedNodeUid);

	/**
	 * This function allow to retrieve the number of Texture dependencies for this object.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	int32 GetTextureDependeciesCount() const;

	/**
	 * This function allow to retrieve the Texture dependency for this object.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	void GetTextureDependencies(TArray<FString>& OutDependencies) const;

	/**
	 * This function allow to retrieve one Texture dependency for this object.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	void GetTextureDependency(const int32 Index, FString& OutDependency) const;

	/**
	 * Add one Texture dependency to this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool SetTextureDependencyUid(const FString& DependencyUid);

	/**
	 * Remove one Texture dependency from this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool RemoveTextureDependencyUid(const FString& DependencyUid);

	/**
	 * Get the translated texture node unique ID.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool GetCustomTranslatedMaterialNodeUid(FString& AttributeValue) const;

	/**
	 * Set the translated texture node unique ID. This is the reference to the node that was create by the translator and this node is needed to get the texture payload.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool SetCustomTranslatedMaterialNodeUid(const FString& AttributeValue);

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool GetCustomMaterialUsage(uint8& AttributeValue) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool SetCustomMaterialUsage(const uint8& AttributeValue, bool bAddApplyDelegate = true);

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool GetCustomBlendMode(uint8& AttributeValue) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool SetCustomBlendMode(const uint8& AttributeValue, bool bAddApplyDelegate = true);

	virtual void Serialize(FArchive& Ar) override;

private:

	void FillAssetClassFromAttribute();

	bool SetMaterialNodeClassFromClassAttribute();

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
	bool ApplyCustomMaterialUsageToAsset(UObject* Asset) const;

	bool FillCustomMaterialUsageFromAsset(UObject* Asset);
#endif

	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(BlendMode, uint8, UMaterial);

protected:
#if WITH_ENGINE
	TSubclassOf<UMaterialInterface> AssetClass = nullptr;
#endif
	bool bIsMaterialNodeClassInitialized = false;

	UE::Interchange::TArrayAttributeHelper<FString> TextureDependencies;
};
