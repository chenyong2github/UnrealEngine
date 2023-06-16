// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGPin.h"
#include "PCGSettings.h"
#include "Metadata/PCGAttributePropertySelector.h"

#include "PCGCreateTargetActor.generated.h"

class AActor;

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGCreateTargetActor : public UPCGSettings
{
	GENERATED_BODY()

public:
	//~Begin UObject interface
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PreEditUndo() override;
	virtual void PostEditUndo() override;
#endif
	//~End UObject interface

	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("CreateTargetActor")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spawner; }
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface

#if WITH_EDITOR
	void OnBlueprintChanged(UBlueprint* Blueprint);
	void SetupBlueprintEvent();
	void TeardownBlueprintEvent();
#endif

private:
	void RefreshTemplateActor();

public:
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, meta = (OnlyPlaceable, DisallowCreateNew))
	TSubclassOf<AActor> TemplateActorClass = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = Settings, meta = (ShowInnerProperties))
	TObjectPtr<AActor> TemplateActor;

	UPROPERTY(BlueprintReadWrite, Category = Settings, meta = (PCG_Overridable))
	TSoftObjectPtr<AActor> RootActor;
};

class FPCGCreateTargetActorElement : public FSimplePCGElement
{
protected:
	// Since this element creates an actor, it needs to run on the game thread and cannot be cached
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }
	virtual bool IsCacheable(const UPCGSettings* Settings) const override { return false; }
	
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};