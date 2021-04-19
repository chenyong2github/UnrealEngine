// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNode.h"

#include "InterchangeSceneNode.generated.h"

//Interchange namespace
//Interchange namespace
namespace UE
{
	namespace Interchange
	{

		struct FSceneNodeStaticData : public FBaseNodeStaticData
		{
			static const FString& GetNodeSpecializeTypeBaseKey()
			{
				static FString SceneNodeSpecializeType_BaseKey = TEXT("SceneNodeSpecializeType");
				return SceneNodeSpecializeType_BaseKey;
			}
		};

	}//ns Interchange
}//ns UE

UCLASS(BlueprintType)
class INTERCHANGENODES_API UInterchangeSceneNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	UInterchangeSceneNode()
	:UInterchangeBaseNode()
	{
		NodeSpecializeTypes.Initialize(Attributes, UE::Interchange::FSceneNodeStaticData::GetNodeSpecializeTypeBaseKey());
	}

	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const override
	{
		const FString TypeName = TEXT("SceneNode");
		return TypeName;
	}

	virtual FString GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override
	{
		FString KeyDisplayName = NodeAttributeKey.Key;
		if (NodeAttributeKey.Key.Equals(UE::Interchange::FSceneNodeStaticData::GetNodeSpecializeTypeBaseKey()))
		{
			KeyDisplayName = TEXT("Specialized Type Count");
			return KeyDisplayName;
		}
		else if (NodeAttributeKey.Key.StartsWith(UE::Interchange::FSceneNodeStaticData::GetNodeSpecializeTypeBaseKey()))
		{
			KeyDisplayName = TEXT("Specialized type index ");
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

	virtual FGuid GetHash() const override
	{
		return Attributes->GetStorageHash();
	}

	/**
	 * Icon name are simply create by adding "InterchangeIcon_" in front of the specialized type. If there is no special type the function will return NAME_None which will use the default icon.
	 */
	virtual FName GetIconName() const override
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

	/** Specialized type are scene node special type like (Joint or LODGroup).*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	int32 GetSpecializedTypeCount() const
	{
		return NodeSpecializeTypes.GetCount();
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	void GetSpecializedType(const int32 Index, FString& OutSpecializedType) const
	{
		NodeSpecializeTypes.GetName(Index, OutSpecializedType);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	void GetSpecializedTypes(TArray<FString>& OutSpecializedTypes) const
	{
		NodeSpecializeTypes.GetNames(OutSpecializedTypes);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	bool AddSpecializedType(const FString& SpecializedType)
	{
		return NodeSpecializeTypes.AddName(SpecializedType);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	bool RemoveSpecializedType(const FString& SpecializedType)
	{
		return NodeSpecializeTypes.RemoveName(SpecializedType);
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	bool GetCustomLocalTransform(FTransform& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(LocalTransform, FTransform);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	bool SetCustomLocalTransform(const FTransform& AttributeValue)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(LocalTransform, FTransform);
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	bool GetCustomGlobalTransform(FTransform& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(GlobalTransform, FTransform);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	bool SetCustomGlobalTransform(const FTransform& AttributeValue)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(GlobalTransform, FTransform);
	}

private:
	//Scene Attribute Keys
	const UE::Interchange::FAttributeKey Macro_CustomLocalTransformKey = UE::Interchange::FAttributeKey(TEXT("LocalTransform"));
	const UE::Interchange::FAttributeKey Macro_CustomGlobalTransformKey = UE::Interchange::FAttributeKey(TEXT("GlobalTransform"));

	UE::Interchange::FNameAttributeArrayHelper NodeSpecializeTypes;
};
