// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "PCGElement.h"

#include "PCGMetadataElement.generated.h"

UENUM()
enum class EPCGPointProperties : uint8
{
	Density,
	Extents,
	Color,
	Position,
	Rotation,
	Scale,
	Transform
};

UENUM()
enum class EPCGMetadataOperationTarget : uint8
{
	PropertyToAttribute,
	AttributeToProperty,
	AttributeToAttribute
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGMetadataOperationSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("MetadataOperationNode")); }
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "Target==EPCGMetadataOperationTarget::AttributeToProperty||Target==EPCGMetadataOperationTarget::AttributeToAttribute"))
	FName SourceAttribute = NAME_None;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGPointProperties PointProperty = EPCGPointProperties::Density;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition="Target==EPCGMetadataOperationTarget::PropertyToAttribute||Target==EPCGMetadataOperationTarget::AttributeToAttribute"))
	FName DestinationAttribute = NAME_None;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGMetadataOperationTarget Target = EPCGMetadataOperationTarget::PropertyToAttribute;
};

class FPCGMetadataOperationElement : public FSimplePCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};