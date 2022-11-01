// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGElement.h"
#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "UObject/ObjectMacros.h"

#include "PCGDataTableRowToParamData.generated.h"

class UDataTable;

UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPCGDataTableRowToParamDataSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	//~Begin UPCGSettings interface
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("DataTableRowToParamData")); }
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Metadata; }
#endif

	virtual FName AdditionalTaskName() const override;

	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override { return TArray<FPCGPinProperties>(); }

	// The name of the row to copy from
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FName RowName = NAME_None;	

	// the data table to copy from
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TObjectPtr<UDataTable> DataTable;

protected:
	virtual FPCGElementPtr CreateElement() const override;
	// ~End UPCGSettings interface
};

class FPCGDataTableRowToParamData : public FSimplePCGElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }
	virtual bool IsCacheable(const UPCGSettings* InSettings) const { return false; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};

