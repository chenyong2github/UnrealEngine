// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Nodes/InterchangeBaseNode.h"

#include "InterchangeActorFactoryNode.generated.h"

UCLASS(BlueprintType)
class INTERCHANGENODES_API UInterchangeActorFactoryNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	virtual UClass* GetObjectClass() const override;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | ActorFactory")
	bool GetCustomGlobalTransform(FTransform& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(GlobalTransform, FTransform);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | ActorFactory")
	bool SetCustomGlobalTransform(const FTransform& AttributeValue)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(GlobalTransform, FTransform);
	}

private:
	const UE::Interchange::FAttributeKey Macro_CustomGlobalTransformKey = UE::Interchange::FAttributeKey(TEXT("GlobalTransform"));
};