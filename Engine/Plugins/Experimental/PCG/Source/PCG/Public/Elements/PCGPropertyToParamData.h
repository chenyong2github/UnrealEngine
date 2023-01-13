// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGPin.h"
#include "PCGSettings.h"
#include "Elements/PCGActorSelector.h"

#include "PCGPropertyToParamData.generated.h"

class UActorComponent;

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGPropertyToParamDataSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	//~Begin UObject interface
	virtual void PostLoad() override;
	//~End UObject interface

	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("PropertyToParamData")); }
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Param; }
#endif

	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override { return TArray<FPCGPinProperties>(); }

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (ShowOnlyInnerProperties))
	FPCGActorSelectorSettings ActorSelector;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bSelectComponent = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "bSelectComponent", EditConditionHides))
	TSubclassOf<UActorComponent> ComponentClass;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FName PropertyName = NAME_None;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FName OutputAttributeName = NAME_None;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bAlwaysRequeryActors = true;

private:
	UPROPERTY()
	EPCGActorSelection ActorSelection_DEPRECATED;

	UPROPERTY()
	FName ActorSelectionTag_DEPRECATED;

	UPROPERTY()
	FName ActorSelectionName_DEPRECATED;

	UPROPERTY()
	TSubclassOf<AActor> ActorSelectionClass_DEPRECATED;

	UPROPERTY()
	EPCGActorFilter ActorFilter_DEPRECATED = EPCGActorFilter::Self;

	UPROPERTY()
	bool bIncludeChildren_DEPRECATED = false;
};

class FPCGPropertyToParamDataElement : public FSimplePCGElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return !CastChecked<UPCGPropertyToParamDataSettings>(InSettings)->bAlwaysRequeryActors; }
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const;
};
