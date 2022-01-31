// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusDataType.h"

#include "CoreMinimal.h"
#include "OptimusDataDomain.h"
#include "UObject/Object.h"

#include "OptimusResourceDescription.generated.h"


UCLASS(BlueprintType)
class OPTIMUSCORE_API UOptimusResourceDescription :
	public UObject
{
	GENERATED_BODY()
public:

	UOptimusResourceDescription() = default;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = ResourceDescription)
	FName ResourceName;

	/** The the data type of each element of the resource */
	UPROPERTY(EditAnywhere, Category=ResourceDescription, meta=(UseInResource))
	FOptimusDataTypeRef DataType;

	/** The data domain. Only a single level is allowed since we can only allocate the resource
	 *  as a flat array, rather than array-of-arrays and deeper. */
	UPROPERTY(EditAnywhere, Category=ResourceDescription)
	FOptimusDataDomain DataDomain;
	
#if WITH_EDITOR
	void PostEditChangeProperty( struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
