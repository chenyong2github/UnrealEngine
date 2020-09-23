// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNode.h"

#if WITH_ENGINE


#endif

#include "InterchangeSceneNode.generated.h"


UCLASS(BlueprintType)
class INTERCHANGENODEPLUGIN_API UInterchangeSceneNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	UInterchangeSceneNode()
	:UInterchangeBaseNode()
	{
	}

	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const override
	{
		const FString TypeName = TEXT("SceneNode");
		return TypeName;
	}

	virtual FGuid GetHash() const override
	{
		return Attributes.GetStorageHash();
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
	const Interchange::FAttributeKey Macro_CustomLocalTransformKey = Interchange::FAttributeKey(TEXT("LocalTransform"));
	const Interchange::FAttributeKey Macro_CustomGlobalTransformKey = Interchange::FAttributeKey(TEXT("GlobalTransform"));
};
