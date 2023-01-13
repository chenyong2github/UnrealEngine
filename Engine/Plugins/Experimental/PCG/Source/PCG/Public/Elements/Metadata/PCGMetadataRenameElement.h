// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGSettings.h"

#include "PCGMetadataRenameElement.generated.h"

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGMetadataRenameSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("AttributeRename")); }
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Metadata; }
#endif

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FName AttributeToRename = NAME_None;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FName NewAttributeName = NAME_None;
};

class FPCGMetadataRenameElement : public FSimplePCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
