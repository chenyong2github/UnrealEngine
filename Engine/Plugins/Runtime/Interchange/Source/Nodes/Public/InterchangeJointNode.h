// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNode.h"

#include "InterchangeJointNode.generated.h"

/**
 * The joint node is use to store the joint information available when the translator read the data
 */


UCLASS(BlueprintType)
class INTERCHANGENODEPLUGIN_API UInterchangeJointNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	UInterchangeJointNode()
	:UInterchangeBaseNode()
	{
	}

	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const override
	{
		const FString TypeName = TEXT("JointNode");
		return TypeName;
	}

	/** Get the class this node want to create */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Joint")
	virtual class UClass* GetAssetClass() const override
	{
		//No factory for joint so just return nullptr here, it will be import with the skeleton factory
		return nullptr;
	}

	virtual FGuid GetHash() const override
	{
		return Attributes.GetStorageHash();
	}

public:
	//Joint name attribute, all joints in a skeleton must have unique name

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Joint")
	bool GetCustomName(FName& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(Name, FName);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Joint")
	bool SetCustomName(const FName& AttributeValue)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(Name, FName)
	}

	//Local transform attribute

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Joint")
	bool GetCustomLocalTransform(FTransform& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(LocalTransform, FTransform);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Joint")
	bool SetCustomLocalTransform(const FTransform& AttributeValue)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(LocalTransform, FTransform)
	}

private:
	const UE::Interchange::FAttributeKey Macro_CustomNameKey = UE::Interchange::FAttributeKey(TEXT("JointName"));
	const UE::Interchange::FAttributeKey Macro_CustomLocalTransformKey = UE::Interchange::FAttributeKey(TEXT("LocalTransform"));
};
