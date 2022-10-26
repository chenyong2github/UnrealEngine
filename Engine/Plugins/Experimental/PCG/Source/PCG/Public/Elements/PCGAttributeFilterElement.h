// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGElement.h"
#include "PCGSettings.h"

#include "PCGAttributeFilterElement.generated.h"

/**
* Filter the attributes from a given input metadata.
* Will remove all attributes that are not listed in AttributesToKeep.
* If an attribute to keep is not in the original metadata, it won't be added.
* 
* The output will be the original data with the updated metadata.
*/
UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGAttributeFilterSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Metadata; }
#endif

	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	//~End UPCGSettings interface

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TArray<FName> AttributesToKeep;

protected:
	virtual FPCGElementPtr CreateElement() const override;
};


class FPCGAttributeFilterElement : public FSimplePCGElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const { return true; }
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
