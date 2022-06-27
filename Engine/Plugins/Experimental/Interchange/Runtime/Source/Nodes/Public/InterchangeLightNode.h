// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNode.h"

#include "InterchangeLightNode.generated.h"

UCLASS(BlueprintType, Experimental)
class INTERCHANGENODES_API UInterchangeBaseLightNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	static FStringView StaticAssetTypeName();

	/**
	 * Return the node type name of the class, we use this when reporting errors
	 */
	virtual FString GetTypeName() const override;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Light")
	bool GetCustomLightColor(FLinearColor& AttributeValue) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Light")
	bool SetCustomLightColor(const FLinearColor& AttributeValue);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Light")
	bool GetCustomIntensity(float& AttributeValue) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Light")
	bool SetCustomIntensity(float AttributeValue);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Light")
	bool GetCustomTemperature(float& AttributeValue) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Light")
	bool SetCustomTemperature(float AttributeValue);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Light")
	bool GetCustomUseTemperature(bool & AttributeValue) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Light")
	bool SetCustomUseTemperature(bool AttributeValue);

private:

	IMPLEMENT_NODE_ATTRIBUTE_KEY(LightColor)
	IMPLEMENT_NODE_ATTRIBUTE_KEY(LightIntensity)
	IMPLEMENT_NODE_ATTRIBUTE_KEY(Temperature)
	IMPLEMENT_NODE_ATTRIBUTE_KEY(UseTemperature)
};

UCLASS(BlueprintType)
class INTERCHANGENODES_API UInterchangeLightNode : public UInterchangeBaseLightNode
{
	GENERATED_BODY()

public:

	virtual FString GetTypeName() const override;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | LocalLight")
	bool GetCustomIntensityUnits(ELightUnits& AttributeValue) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | LocalLight")
	bool SetCustomIntensityUnits(ELightUnits AttributeValue);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | LocalLight")
	bool GetCustomAttenuationRadius(float& AttributeValue) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | LocalLight")
	bool SetCustomAttenuationRadius(float AttributeValue);

private:

	IMPLEMENT_NODE_ATTRIBUTE_KEY(IntensityUnits)
	IMPLEMENT_NODE_ATTRIBUTE_KEY(AttenuationRadius)
};

UCLASS(BlueprintType)
class INTERCHANGENODES_API UInterchangePointLightNode : public UInterchangeLightNode
{
	GENERATED_BODY()

public:

	virtual FString GetTypeName() const override;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | PointLight")
	bool GetCustomUseInverseSquaredFalloff(bool& AttributeValue) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | PointLight")
	bool SetCustomUseInverseSquaredFalloff(bool AttributeValue);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | PointLight")
	bool GetCustomLightFalloffExponent(float& AttributeValue) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | PointLight")
	bool SetCustomLightFalloffExponent(float AttributeValue);

private:
	IMPLEMENT_NODE_ATTRIBUTE_KEY(UseInverseSquaredFalloff)
	IMPLEMENT_NODE_ATTRIBUTE_KEY(LightFalloffExponent)
};

UCLASS(BlueprintType)
class INTERCHANGENODES_API UInterchangeSpotLightNode : public UInterchangePointLightNode
{
	GENERATED_BODY()

public:

	virtual FString GetTypeName() const override;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SpotLight")
	bool GetCustomInnerConeAngle(float& AttributeValue) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SpotLight")
	bool SetCustomInnerConeAngle(float AttributeValue);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SpotLight")
	bool GetCustomOuterConeAngle(float& AttributeValue) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SpotLight")
	bool SetCustomOuterConeAngle(float AttributeValue);

private:

	IMPLEMENT_NODE_ATTRIBUTE_KEY(InnerConeAngle)
	IMPLEMENT_NODE_ATTRIBUTE_KEY(OuterConeAngle)
};

UCLASS(BlueprintType)
class INTERCHANGENODES_API UInterchangeRectLightNode : public UInterchangeLightNode
{
	GENERATED_BODY()

public:

	virtual FString GetTypeName() const override;

	//For the moment n/a, need more discussion specially about barn door's angle
	/*
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | RectLightFactory")
	bool GetCustomSourceWidth(float& AttributeValue) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | RectLightFactory")
	bool SetCustomSourceWidth(float AttributeValue);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | RectLightFactory")
	bool GetCustomSourceHeight(float& AttributeValue) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | RectLightFactory")
	bool SetCustomSourceHeight(float AttributeValue);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | RectLightFactory")
	bool GetCustomBarnDoorAngle(float& AttributeValue) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | RectLightFactory")
	bool SetCustomBarnDoorAngle(float AttributeValue);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | RectLightFactory")
	bool GetCustomBarnDoorLength(float& AttributeValue) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | RectLightFactory")
	bool SetCustomBarnDoorLength(float AttributeValue);

private:
	IMPLEMENT_NODE_ATTRIBUTE_KEY(SourceWidth)
	IMPLEMENT_NODE_ATTRIBUTE_KEY(SourceHeight)
	IMPLEMENT_NODE_ATTRIBUTE_KEY(BarnDoorAngle)
	IMPLEMENT_NODE_ATTRIBUTE_KEY(BarnDoorLength)
	*/
};

UCLASS(BlueprintType)
class INTERCHANGENODES_API UInterchangeDirectionalLightNode : public UInterchangeBaseLightNode
{
	GENERATED_BODY()

public:

	virtual FString GetTypeName() const override;
};
