// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNode.h"

#include "InterchangeLightNode.generated.h"

UCLASS(BlueprintType, Experimental)
class INTERCHANGENODES_API UInterchangeLightNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	static FStringView StaticAssetTypeName()
	{
		return TEXT("Light");
	}

	/**
	 * Return the node type name of the class, we use this when reporting errors
	 */
	virtual FString GetTypeName() const override
	{
		const FString TypeName = TEXT("LightNode");
		return TypeName;
	}

};

UCLASS(BlueprintType)
class INTERCHANGENODES_API UInterchangePointLightNode : public UInterchangeLightNode
{
	GENERATED_BODY()

public:

};

UCLASS(BlueprintType)
class INTERCHANGENODES_API UInterchangeSpotLightNode : public UInterchangePointLightNode
{
	GENERATED_BODY()

public:

};

UCLASS(BlueprintType)
class INTERCHANGENODES_API UInterchangeRectLightNode : public UInterchangeLightNode
{
	GENERATED_BODY()

public:

};

UCLASS(BlueprintType)
class INTERCHANGENODES_API UInterchangeDirectionalLightNode : public UInterchangeLightNode
{
	GENERATED_BODY()

public:

};
