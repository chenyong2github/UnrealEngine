// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "DisplayClusterConfigurationTypes_Base.generated.h"

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationRectangle
{
	GENERATED_BODY()

public:
	FDisplayClusterConfigurationRectangle()
		: X(0), Y(0), W(0), H(0)
	{ }

	FDisplayClusterConfigurationRectangle(int32 _X, int32 _Y, int32 _W, int32 _H)
		: X(_X), Y(_Y), W(_W), H(_H)
	{ }

	FDisplayClusterConfigurationRectangle(const FDisplayClusterConfigurationRectangle&) = default;

	FIntRect ToRect() const;

public:
	UPROPERTY(EditAnywhere, Category = nDisplay)
	int32 X;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	int32 Y;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	int32 W;

	UPROPERTY(EditAnywhere, Category = nDisplay)
	int32 H;
};

/**
 * All configuration UObjects should inherit from this class.
 */
UCLASS()
class DISPLAYCLUSTERCONFIGURATION_API UDisplayClusterConfigurationData_Base
	: public UObject
{
	GENERATED_BODY()

public:
	// UObject
	virtual void Serialize(FArchive& Ar) override;
	// ~UObject

protected:
	/** Called before saving to collect objects which should be exported as a sub object block. */
	virtual void GetObjectsToExport(TArray<UObject*>& OutObjects) {}

private:
	UPROPERTY(Export)
	TArray<UObject*> ExportedObjects;
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationPolymorphicEntity
{
	GENERATED_BODY()

public:
	// Polymorphic entity type
	UPROPERTY(EditAnywhere, Category = nDisplay)
	FString Type;

	// Generic parameters map
	UPROPERTY(EditAnywhere, Category = nDisplay)
	TMap<FString, FString> Parameters;

#if WITH_EDITORONLY_DATA
	/**
	 * When a custom policy is selected from the details panel.
	 * This is needed in the event a custom policy is selected
	 * but the custom type is a default policy. This allows users
	 * to further customize default policies if necessary.
	 *
	 * EditAnywhere is required so we can manipulate the property
	 * through a handle. Details will hide it from showing.
	 */
	UPROPERTY(EditAnywhere, Category = nDisplay)
	bool bIsCustom = false;
#endif
};

USTRUCT()
struct DISPLAYCLUSTERCONFIGURATION_API FDisplayClusterConfigurationProjection
	: public FDisplayClusterConfigurationPolymorphicEntity
{
	GENERATED_BODY()

public:
	FDisplayClusterConfigurationProjection();
};

