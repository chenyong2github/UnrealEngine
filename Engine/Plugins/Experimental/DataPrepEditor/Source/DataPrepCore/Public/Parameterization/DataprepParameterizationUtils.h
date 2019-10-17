// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NameTypes.h"
#include "UObject/ObjectMacros.h"

#include "DataprepParameterizationUtils.generated.h"

class IPropertyHandle;
class UDataprepAsset;
class UProperty;

enum class EParametrizationState : uint8
{
	CanBeParameterized,
	IsParameterized,
	ParentIsParameterized,
	InvalidForParameterization
};

/**
 * A small context that help when constructing the widgets for the parameterization
 */
struct FDataprepParameterizationContext
{
	TArray<FDataprepPropertyLink> PropertyChain;
	EParametrizationState State = EParametrizationState::CanBeParameterized;
};


USTRUCT()
struct FDataprepPropertyLink
{
	GENERATED_BODY()

	FDataprepPropertyLink( UProperty* InCachedProperty, FName InPropertyName, int32 InContenerIndex )
		: CachedProperty( MakeWeakObjectPtr(InCachedProperty) )
		, PropertyName( InPropertyName )
		, ContainerIndex( InContenerIndex )
	{}

	FDataprepPropertyLink() = default;
	FDataprepPropertyLink(const FDataprepPropertyLink& Other) = default;
	FDataprepPropertyLink(FDataprepPropertyLink&& Other) = default;
	FDataprepPropertyLink& operator=(const FDataprepPropertyLink& Other) = default;
	FDataprepPropertyLink& operator=(FDataprepPropertyLink&& Other) = default;

	UPROPERTY()
	TWeakObjectPtr<UProperty> CachedProperty;

	UPROPERTY()
	FName PropertyName;

	UPROPERTY()
	int32 ContainerIndex;
};

uint32 GetTypeHash(const FDataprepPropertyLink& PropertyLink);

class DATAPREPCORE_API FDataprepParameterizationUtils
{
public:
	/**
	 * Take a property handle from the details view and generate the property for the dataprep parameterization
	 * @param PropertyHandle This is the handle from the details panel
	 * @return A non empty array if we were able make a compatible property chain
	 */
	static TArray<FDataprepPropertyLink> MakePropertyChain(TSharedPtr<IPropertyHandle> PropertyHandle);


	/**
	 * Take a already existing parameterization context and create a new version including the handle.
	 */
	static FDataprepParameterizationContext CreateContext(TSharedPtr<IPropertyHandle> PropertyHandle, const FDataprepParameterizationContext& ParameterisationContext);

	/**
	 * Grab the dataprep used for the parameterization of the object.
	 * @param Object The object from which the bindings would be created
	 * @return A valid pointer if the object was valid for the parameterization
	 */
	static UDataprepAsset* GetDataprepAssetForParameterization(UObject* Object);
};
