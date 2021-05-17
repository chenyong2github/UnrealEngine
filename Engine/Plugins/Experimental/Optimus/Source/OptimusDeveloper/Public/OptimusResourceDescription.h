// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusDataType.h"

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"

#include "OptimusResourceDescription.generated.h"



UCLASS()
class UOptimusResourceDescription :
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

#if defined(WITH_EDITOR)
	void PostEditChangeProperty( struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
