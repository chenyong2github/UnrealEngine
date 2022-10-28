// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGElement.h"
#include "PCGSettings.h"
#include "Elements/PCGActorSelector.h"

#include "PCGDataFromActor.generated.h"

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGDataFromActorSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("DataFromActorNode")); }
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spatial; }
#endif

	virtual TArray<FPCGPinProperties> InputPinProperties() const override { return TArray<FPCGPinProperties>(); }

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (ShowOnlyInnerProperties))
	FPCGActorSelectorSettings ActorSelector;
};

class FPCGDataFromActorElement : public FSimplePCGElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const;
};