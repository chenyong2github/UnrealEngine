// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGContext.h"
#include "PCGSettings.h"
#include "Elements/PCGActorSelector.h"

#include "PCGPin.h"
#include "PCGDataFromActor.generated.h"

UENUM()
enum class EPCGGetDataFromActorMode : uint8
{
	ParseActorComponents,
	GetSinglePoint,
	GetDataFromProperty,
	GetDataFromPCGComponent,
	GetDataFromPCGComponentOrParseComponents
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGDataFromActorSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("DataFromActor")); }
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spatial; }
	virtual void GetTrackedActorTags(FPCGTagToSettingsMap& OutTagToSettings, TArray<TObjectPtr<const UPCGGraph>>& OutVisitedGraphs) const override;
#endif

	virtual TArray<FPCGPinProperties> InputPinProperties() const override { return TArray<FPCGPinProperties>(); }
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (ShowOnlyInnerProperties))
	FPCGActorSelectorSettings ActorSelector;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGGetDataFromActorMode Mode = EPCGGetDataFromActorMode::ParseActorComponents;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "Mode == EPCGGetDataFromActorMode::GetDataFromPCGComponent || Mode == EPCGGetDataFromActorMode::GetDataFromPCGComponentOrParseComponents"))
	TArray<FName> ExpectedPins;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "Mode == EPCGGetDataFromActorMode::GetDataFromProperty"))
	FName PropertyName = NAME_None;
};

class FPCGDataFromActorContext : public FPCGContext
{
public:
	TArray<AActor*> FoundActors;
	bool bPerformedQuery = false;
};

class FPCGDataFromActorElement : public IPCGElement
{
public:
	virtual FPCGContext* Initialize(const FPCGDataCollection& InputData, TWeakObjectPtr<UPCGComponent> SourceComponent, const UPCGNode* Node) override;
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const;
	void GatherWaitTasks(AActor* FoundActor, TArray<FPCGTaskId>& OutWaitTasks) const;
	void ProcessActor(FPCGContext* Context, const UPCGDataFromActorSettings* Settings, AActor* FoundActor) const;
};
