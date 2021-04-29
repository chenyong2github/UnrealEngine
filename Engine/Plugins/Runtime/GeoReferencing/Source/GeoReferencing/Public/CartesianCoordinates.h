// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

// Using double precision maths from the Geometry plugin. 
#include "VectorTypes.h"

#include "CartesianCoordinates.generated.h"

/// PEER_REVIEW - I used an Fxxx instead of UObject-based as I thought it was cheaper memorywise, 
/// but I needed to add a BP_FL to make the conversions since Fstruct can't contain UFUNCTIONS. Is that a good choice ?

USTRUCT(BlueprintType)
struct GEOREFERENCING_API FCartesianCoordinates
{
	GENERATED_USTRUCT_BODY()

public:
	FCartesianCoordinates();
	FCartesianCoordinates(double InX, double InY, double InZ);
	FCartesianCoordinates(const FVector3d& Coordinates);
	FCartesianCoordinates(const FVector4d& Coordinates);

	FText ToFullText(int32 IntegralDigits = 3);
	FText ToCompactText(int32 IntegralDigits = 3);
	void ToSeparateTexts(FText& OutX, FText& OutY, FText& OutZ, int32 IntegralDigits = 3);

	void ToFloatApproximation(float& OutX, float& OutY, float& OutZ);

	FVector3d ToVector3d() const;
	
	double X;
	double Y;
	double Z;
};

UCLASS()
class GEOREFERENCING_API UCartesianCoordinatesFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

	/**
	 * Converts a LargeCoordinates value to localized formatted text, in the form 'X= Y= Z='
	 **/
	UFUNCTION(BlueprintPure, meta = (DisplayName = "ToFullText", AdvancedDisplay = "1", BlueprintAutocast), Category = "GeoReferencing")
	static FORCEINLINE FText ToFullText(UPARAM(ref) FCartesianCoordinates& CartesianCoordinates, int32 IntegralDigits = 3)
	{
		return CartesianCoordinates.ToFullText(IntegralDigits);
	}

	/**
	 * Converts a LargeCoordinates value to formatted text, in the form '(X, Y, Z)'
	 **/
	UFUNCTION(BlueprintPure, meta = (DisplayName = "ToCompactText", AdvancedDisplay = "1", BlueprintAutocast), Category = "GeoReferencing")
	static FORCEINLINE FText ToCompactText(UPARAM(ref) FCartesianCoordinates& CartesianCoordinates, int32 IntegralDigits = 3)
	{
		return CartesianCoordinates.ToCompactText(IntegralDigits);
	}

	/**
	 * Converts a LargeCoordinates value to 3 separate text values
	 **/
	UFUNCTION(BlueprintPure, meta = (DisplayName = "ToSeparateTexts", AdvancedDisplay = "4", BlueprintAutocast), Category = "GeoReferencing")
	static void ToSeparateTexts(UPARAM(ref) FCartesianCoordinates& CartesianCoordinates, FText& OutX, FText& OutY, FText& OutZ, int32 IntegralDigits = 3)
	{
		CartesianCoordinates.ToSeparateTexts(OutX, OutY, OutZ, IntegralDigits);
	}

	/**
	 * Get the Coordinates as a float approximation.
	 * USE WISELY as we can't guarantee there will no be rounding due to IEEE754 float encoding !
	 **/
	UFUNCTION(BlueprintPure, Category = "GeoReferencing")
	static void ToFloatApproximation(UPARAM(ref) FCartesianCoordinates& CartesianCoordinates, float& OutX, float& OutY, float& OutZ)
	{
		CartesianCoordinates.ToFloatApproximation(OutX, OutY, OutZ);
	}

	/**
	 * Set the Coordinates from float approximation.
	 * USE WISELY as we can't guarantee there will no be rounding due to IEEE754 float encoding !
	 **/
	UFUNCTION(BlueprintPure, Category = "GeoReferencing")
	static FCartesianCoordinates MakeCartesianCoordinatesApproximation(const float& InX, const float& InY, const float& InZ)
	{
		return FCartesianCoordinates(static_cast<float>(InX), static_cast<float>(InY), static_cast<float>(InZ));
	}
};
