// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGElement.h"
#include "PCGSettings.h"

#include "Data/PCGPointData.h"
#include "PCGPoint.h"

#include "Templates/SubclassOf.h"

#include "PCGExecuteBlueprint.generated.h"

class UWorld;

#if WITH_EDITOR
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPCGBlueprintChanged, UPCGBlueprintElement*);

namespace PCGBlueprintHelper
{
	void GatherDependencies(UObject* Object, TSet<TObjectPtr<UObject>>& OutDependencies);
	void GatherDependencies(FProperty* Property, const void* InContainer, TSet<TObjectPtr<UObject>>& OutDependencies);
	TSet<TObjectPtr<UObject>> GetDataDependencies(UPCGBlueprintElement* InElement);
}
#endif // WITH_EDITOR

UCLASS(Abstract, BlueprintType, Blueprintable, hidecategories = (Object))
class UPCGBlueprintElement : public UObject
{
	GENERATED_BODY()

public:
	// ~Begin UObject interface
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
	// ~End UObject interface

	UFUNCTION(BlueprintNativeEvent, Category = Execution)
	void ExecuteWithContext(UPARAM(ref)FPCGContext& InContext, const FPCGDataCollection& Input, FPCGDataCollection& Output) const;

	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = Execution)
	void Execute(const FPCGDataCollection& Input, FPCGDataCollection& Output) const;

	UFUNCTION(BlueprintImplementableEvent, Category = Execution)
	bool PointLoopBody(const UPCGPointData* InData, const FPCGPoint& InPoint, FPCGPoint& OutPoint) const;

	UFUNCTION(BlueprintImplementableEvent, Category = Execution)
	bool PointPairLoopBody(const UPCGPointData* InA, const UPCGPointData* InB, const FPCGPoint& InPointA, const FPCGPoint& InPointB, FPCGPoint& OutPoint) const;

	/** Calls the LoopBody function on all points */
	UFUNCTION(BlueprintCallable, Category = Execution)
	void LoopOnPoints(UPARAM(ref) FPCGContext& InContext, const UPCGPointData* InData, UPCGPointData*& OutData) const;

	UFUNCTION(BlueprintCallable, Category = Execution)
	void LoopOnPointPairs(UPARAM(ref) FPCGContext& InContext, const UPCGPointData* InA, const UPCGPointData* InB, UPCGPointData*& OutData) const;

	/** Called after object creation to setup the object callbacks */
	void Initialize();

#if WITH_EDITOR
	// ~Begin UObject interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	// ~End UObject interface
#endif

	/** Needed to be able to call certain blueprint functions */
	virtual UWorld* GetWorld() const override;

#if WITH_EDITOR
	FOnPCGBlueprintChanged OnBlueprintChangedDelegate;
#endif

protected:
#if WITH_EDITOR
	void OnDependencyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent);
	TSet<TObjectPtr<UObject>> DataDependencies;
#endif
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPCGBlueprintSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	friend class FPCGExecuteBlueprintElement;

	// ~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("BlueprintNode")); }
	virtual void GetTrackedActorTags(FPCGTagToSettingsMap& OutTagToSettings) const override;
#endif

	virtual FName AdditionalTaskName() const override;

protected:
	virtual FPCGElementPtr CreateElement() const override;
	// ~End UPCGSettings interface

public:
	// ~Begin UObject interface
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	// ~End UObject interface
#endif

	/** To facilitate transition, we will keep this version around even though it's a lot less useful */
	UFUNCTION(BlueprintCallable, BlueprintInternalUseOnly)
	void SetElementTypeInternal(TSubclassOf<UPCGBlueprintElement> InElementType);

	UFUNCTION(BlueprintCallable, Category = Settings, meta=(DeterminesOutputType="InElementType", DynamicOutputParam = "ElementInstance"))
	void SetElementType(TSubclassOf<UPCGBlueprintElement> InElementType, UPCGBlueprintElement*& ElementInstance);

protected:
	UPROPERTY()
	TSubclassOf<UPCGBlueprintElement> BlueprintElement_DEPRECATED;

	UPROPERTY(BlueprintSetter = SetElementTypeInternal, EditAnywhere, Category = Template)
	TSubclassOf<UPCGBlueprintElement> BlueprintElementType;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Instanced, Category = Settings, meta = (ShowOnlyInnerProperties))
	TObjectPtr<UPCGBlueprintElement> BlueprintElementInstance;

#if WITH_EDITORONLY_DATA
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TArray<FName> TrackedActorTags;
#endif

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bCreatesArtifacts = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bPrecachePoints = false;

protected:
#if WITH_EDITOR
	void OnBlueprintChanged(UBlueprint* InBlueprint);
	void OnBlueprintElementChanged(UPCGBlueprintElement* InElement);
#endif

	void RefreshBlueprintElement();
	void SetupBlueprintEvent();
	void TeardownBlueprintEvent();
	void SetupBlueprintElementEvent();
	void TeardownBlueprintElementEvent();
};

class FPCGExecuteBlueprintElement : public FSimpleTypedPCGElement<UPCGBlueprintSettings>
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override;
};