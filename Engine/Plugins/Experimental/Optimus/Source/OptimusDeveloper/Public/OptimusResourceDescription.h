// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusDataType.h"

#include "CoreMinimal.h"
#include "OptimusContextNames.h"
#include "UObject/Object.h"

#include "OptimusResourceDescription.generated.h"


/** A struct to hold onto a single resource context type, usually for top-level contexts,
  *  such as kernel driver context and user-defined resources.
  */
USTRUCT()
struct FOptimusResourceContext
{
	GENERATED_BODY()

	FOptimusResourceContext() = default;
	FOptimusResourceContext(FName InContextName) : ContextName(InContextName) {}

	// The name of the context that this resource/kernel applies to.
	UPROPERTY(EditAnywhere, Category = Context)
	FName ContextName = Optimus::ContextName::Vertex;
};

/** A struct to hold onto a nested resource context type, usually for kernels and data
  * interfaces.
  */
USTRUCT()
struct FOptimusNestedResourceContext
{
	GENERATED_BODY()

	FOptimusNestedResourceContext() = default;
	FOptimusNestedResourceContext(FName InContextName) : ContextNames({InContextName}) {}
	FOptimusNestedResourceContext(TArray<FName> InContextNames) : ContextNames(InContextNames) {}

	// The name of the context that this resource/kernel applies to.
	UPROPERTY(EditAnywhere, Category = Context)
	TArray<FName> ContextNames{Optimus::ContextName::Vertex};
};


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

	FOptimusResourceContext Context;
	
#if defined(WITH_EDITOR)
	void PostEditChangeProperty( struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
