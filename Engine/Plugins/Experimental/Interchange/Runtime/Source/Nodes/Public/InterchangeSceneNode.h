// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNode.h"

#include "InterchangeSceneNode.generated.h"

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

			static const FString& GetMaterialDependencyUidsBaseKey()
			{
				static FString MaterialDependencyUids_BaseKey = TEXT("__MaterialDependencyUidsBaseKey__");
				return MaterialDependencyUids_BaseKey;
			}

			static const FString& GetJointSpecializeTypeString()
			{
				static FString JointSpecializeTypeString = TEXT("Joint");
				return JointSpecializeTypeString;
			}

			static const FString& GetLodGroupSpecializeTypeString()
			{
				static FString JointSpecializeTypeString = TEXT("LodGroup");
				return JointSpecializeTypeString;
			}
		};

	}//ns Interchange
}//ns UE

UCLASS(BlueprintType, Experimental)
class INTERCHANGENODES_API UInterchangeSceneNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	UInterchangeSceneNode()
	:UInterchangeBaseNode()
	{
		NodeSpecializeTypes.Initialize(Attributes, UE::Interchange::FSceneNodeStaticData::GetNodeSpecializeTypeBaseKey());
		MaterialDependencyUids.Initialize(Attributes, UE::Interchange::FSceneNodeStaticData::GetMaterialDependencyUidsBaseKey());
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
			KeyDisplayName = TEXT("Specialized type count");
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
		else if (NodeAttributeKey.Key.Equals(UE::Interchange::FSceneNodeStaticData::GetMaterialDependencyUidsBaseKey()))
		{
			KeyDisplayName = TEXT("Material dependencies count");
			return KeyDisplayName;
		}
		else if (NodeAttributeKey.Key.StartsWith(UE::Interchange::FSceneNodeStaticData::GetMaterialDependencyUidsBaseKey()))
		{
			KeyDisplayName = TEXT("Material dependency index ");
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

	/** Return true if this node contains the specialized type parameter.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	bool IsSpecializedTypeContains(const FString& SpecializedType) const
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

	/** Asset dependencies are the asset on which this node depend.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	int32 GetMaterialDependencyUidsCount() const
	{
		return MaterialDependencyUids.GetCount();
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	void GetMaterialDependencyUid(const int32 Index, FString& OutMaterialDependencyUid) const
	{
		MaterialDependencyUids.GetName(Index, OutMaterialDependencyUid);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	void GetMaterialDependencyUids(TArray<FString>& OutMaterialDependencyUids) const
	{
		MaterialDependencyUids.GetNames(OutMaterialDependencyUids);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	bool AddMaterialDependencyUid(const FString& MaterialDependencyUid)
	{
		return MaterialDependencyUids.AddName(MaterialDependencyUid);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	bool RemoveMaterialDependencyUid(const FString& MaterialDependencyUid)
	{
		return MaterialDependencyUids.RemoveName(MaterialDependencyUid);
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

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	bool GetCustomAssetInstanceUid(FString& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(AssetInstanceUid, FString);
	}

	/** Tells which asset, if any, a scene node is instantiating */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	bool SetCustomAssetInstanceUid(const FString& AttributeValue)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(AssetInstanceUid, FString);
	}

private:
	//Scene Attribute Keys
	const UE::Interchange::FAttributeKey Macro_CustomLocalTransformKey = UE::Interchange::FAttributeKey(TEXT("LocalTransform"));
	const UE::Interchange::FAttributeKey Macro_CustomGlobalTransformKey = UE::Interchange::FAttributeKey(TEXT("GlobalTransform"));
	const UE::Interchange::FAttributeKey Macro_CustomAssetInstanceUidKey = UE::Interchange::FAttributeKey(TEXT("AssetInstanceUid"));

	UE::Interchange::FNameAttributeArrayHelper NodeSpecializeTypes;
	UE::Interchange::FNameAttributeArrayHelper MaterialDependencyUids;
};
