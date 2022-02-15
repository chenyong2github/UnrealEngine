// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeUtilities.h"
#include "Types/AttributeStorage.h"
#include "UObject/Class.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeFactoryBaseNode.generated.h"

/**
 * This struct is used to store and retrieve key value attributes. The attributes are store in a generic FAttributeStorage which serialize the value in a TArray64<uint8>
 * See UE::Interchange::EAttributeTypes to know the supported template types
 * This is an abstract class. This is the base class of the interchange node graph format, all class in this format should derive from this class
 */
UCLASS(BlueprintType, Experimental)
class INTERCHANGECORE_API UInterchangeFactoryBaseNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	virtual FString GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override;

	/**
	 * Return the custom sub-path under PackageBasePath, where the assets will be created.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool GetCustomSubPath(FString& AttributeValue) const;

	/**
	 * Set the custom sub-path under PackageBasePath where the assets will be created.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool SetCustomSubPath(const FString& AttributeValue);

private:

	const UE::Interchange::FAttributeKey Macro_CustomSubPathKey = UE::Interchange::FAttributeKey(TEXT("SubPath"));

};