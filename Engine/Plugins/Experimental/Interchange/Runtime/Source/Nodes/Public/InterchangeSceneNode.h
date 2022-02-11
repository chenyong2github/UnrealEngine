// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"
#include "Nodes/InterchangeBaseNode.h"

#include "InterchangeSceneNode.generated.h"

class UInterchangeBaseNodeContainer;
//Interchange namespace
namespace UE
{
	namespace Interchange
	{

		struct INTERCHANGENODES_API FSceneNodeStaticData : public FBaseNodeStaticData
		{
			static const FString& GetNodeSpecializeTypeBaseKey();
			static const FString& GetMaterialDependencyUidsBaseKey();
			static const FString& GetTransformSpecializeTypeString();
			static const FString& GetJointSpecializeTypeString();
			static const FString& GetLodGroupSpecializeTypeString();
		};

	}//ns Interchange
}//ns UE

UCLASS(BlueprintType, Experimental)
class INTERCHANGENODES_API UInterchangeSceneNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	UInterchangeSceneNode();

	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const override;

	virtual FString GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override;

	virtual FString GetAttributeCategory(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override;

	/**
	 * Icon name are simply create by adding "InterchangeIcon_" in front of the specialized type. If there is no special type the function will return NAME_None which will use the default icon.
	 */
	virtual FName GetIconName() const override;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	bool IsSpecializedTypeContains(const FString& SpecializedType) const;

	/** Specialized type are scene node special type like (Joint or LODGroup).*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	int32 GetSpecializedTypeCount() const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	void GetSpecializedType(const int32 Index, FString& OutSpecializedType) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	void GetSpecializedTypes(TArray<FString>& OutSpecializedTypes) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	bool AddSpecializedType(const FString& SpecializedType);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	bool RemoveSpecializedType(const FString& SpecializedType);

	/** Asset dependencies are the asset on which this node depend.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	int32 GetMaterialDependencyUidsCount() const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	void GetMaterialDependencyUid(const int32 Index, FString& OutMaterialDependencyUid) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	void GetMaterialDependencyUids(TArray<FString>& OutMaterialDependencyUids) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	bool AddMaterialDependencyUid(const FString& MaterialDependencyUid);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	bool RemoveMaterialDependencyUid(const FString& MaterialDependencyUid);

	//Default transform is the transform we have in the node (no bind pose, no time evaluation).

	/** Return the default scene node local transform. */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	bool GetCustomLocalTransform(FTransform& AttributeValue) const;

	/** Store the default scene node local transform. */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	bool SetCustomLocalTransform(const UInterchangeBaseNodeContainer* BaseNodeContainer, const FTransform& AttributeValue);

	/** Return the default scene node global transform. This value is computed with all parent local transform. */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	bool GetCustomGlobalTransform(const UInterchangeBaseNodeContainer* BaseNodeContainer, FTransform& AttributeValue, bool bForceRecache = false) const;

	//Bind pose transform is the transform of the joint when the binding with the mesh was done.
	//This attribute should be set only if we have a joint.

	/** Return the bind pose scene node local transform. */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Joint")
	bool GetCustomBindPoseLocalTransform(FTransform& AttributeValue) const;

	/** Store the bind pose scene node local transform. */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Joint")
	bool SetCustomBindPoseLocalTransform(const UInterchangeBaseNodeContainer* BaseNodeContainer, const FTransform& AttributeValue);

	/** Return the bind pose scene node global transform. This value is computed with all parent bind pose local transform. */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Joint")
	bool GetCustomBindPoseGlobalTransform(const UInterchangeBaseNodeContainer* BaseNodeContainer, FTransform& AttributeValue, bool bForceRecache = false) const;

	//Time zero transform is the transform of the node at time zero.
	//This is useful when there is no bind pose or when we import rigid mesh.

	/** Return the time zero scene node local transform. */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Joint")
	bool GetCustomTimeZeroLocalTransform(FTransform& AttributeValue) const;

	/** Store the time zero scene node local transform. */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Joint")
	bool SetCustomTimeZeroLocalTransform(const UInterchangeBaseNodeContainer* BaseNodeContainer, const FTransform& AttributeValue);

	/** Return the time zero scene node global transform. This value is computed with all parent timezero local transform. */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Joint")
	bool GetCustomTimeZeroGlobalTransform(const UInterchangeBaseNodeContainer* BaseNodeContainer, FTransform& AttributeValue, bool bForceRecache = false) const;

	/** Return the geometric offset. Any mesh attach to this scene node will be offset using this transform. */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Joint")
	bool GetCustomGeometricTransform(FTransform& AttributeValue) const;

	/** Store the geometric offset. Any mesh attach to this scene node will be offset using this transform. */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Joint")
	bool SetCustomGeometricTransform(const FTransform& AttributeValue);

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	bool GetCustomAssetInstanceUid(FString& AttributeValue) const;

	/** Tells which asset, if any, a scene node is instantiating */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Scene")
	bool SetCustomAssetInstanceUid(const FString& AttributeValue);

	/** This static function make sure all the global transform caches are reset for all the UInterchangeSceneNode nodes in the UInterchangeBaseNodeContainer */
	static void ResetAllGlobalTransformCaches(const UInterchangeBaseNodeContainer* BaseNodeContainer);

	/** This static function make sure all the global transform caches are reset for all the UInterchangeSceneNode nodes children in the UInterchangeBaseNodeContainer */
	static void ResetGlobalTransformCachesOfNodeAndAllChildren(const UInterchangeBaseNodeContainer* BaseNodeContainer, const UInterchangeBaseNode* ParentNode);

private:

	bool GetGlobalTransformInternal(const UE::Interchange::FAttributeKey LocalTransformKey, TOptional<FTransform>& CacheTransform, const UInterchangeBaseNodeContainer* BaseNodeContainer, FTransform& AttributeValue, bool bForceRecache) const;

	//Scene Attribute Keys
	const UE::Interchange::FAttributeKey Macro_CustomLocalTransformKey = UE::Interchange::FAttributeKey(TEXT("LocalTransform"));
	const UE::Interchange::FAttributeKey Macro_CustomBindPoseLocalTransformKey = UE::Interchange::FAttributeKey(TEXT("BindPoseLocalTransform"));
	const UE::Interchange::FAttributeKey Macro_CustomTimeZeroLocalTransformKey = UE::Interchange::FAttributeKey(TEXT("TimeZeroLocalTransform"));
	const UE::Interchange::FAttributeKey Macro_CustomGeometricTransformKey = UE::Interchange::FAttributeKey(TEXT("GeometricTransform"));
	const UE::Interchange::FAttributeKey Macro_CustomAssetInstanceUidKey = UE::Interchange::FAttributeKey(TEXT("AssetInstanceUid"));

	UE::Interchange::TArrayAttributeHelper<FString> NodeSpecializeTypes;
	UE::Interchange::TArrayAttributeHelper<FString> MaterialDependencyUids;

	mutable TOptional<FTransform> CacheGlobalTransform;
	mutable TOptional<FTransform> CacheBindPoseGlobalTransform;
	mutable TOptional<FTransform> CacheTimeZeroGlobalTransform;
};
