// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGElement.h"
#include "PCGSettings.h"

#include "PCGMetadataBreakVector.generated.h"

namespace PCGMetadataBreakVectorConstants
{
	const FName ParamsLabel = TEXT("Params");
	const FName SourceLabel = TEXT("Source");
	const FName XLabel = TEXT("X");
	const FName YLabel = TEXT("Y");
	const FName ZLabel = TEXT("Z");
	const FName WLabel = TEXT("W");
}

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGMetadataBreakVectorSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("MetadataBreakVectorNode")); }
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Metadata; }
#endif

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FName SourceAttributeName = NAME_None;

	// These exist to facilitate unit testing of the MetadataBreakVector element in isolation, when it otherwise would not have any pin connections to generate output
	bool bForceConnectX = false;
	bool bForceConnectY = false;
	bool bForceConnectZ = false;
	bool bForceConnectW = false;
};

class FPCGMetadataBreakVectorElement : public FSimplePCGElement
{
public:
	// MetadataBreakVector relies on StaticDuplicateObject, so we cannot run outside of the main thread
	virtual bool CanExecuteOnlyOnMainThread(const UPCGSettings* InSettings) const override { return true; }
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
