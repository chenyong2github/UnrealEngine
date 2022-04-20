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
	TSet<TObjectPtr<UObject>> GetDataDependencies(UPCGBlueprintElement* InElement);
}
#endif // WITH_EDITOR

UCLASS(Abstract, BlueprintType, Blueprintable, hidecategories = (Object))
class PCG_API UPCGBlueprintElement : public UObject
{
	GENERATED_BODY()

public:
	// ~Begin UObject interface
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
	// ~End UObject interface

	UFUNCTION(BlueprintNativeEvent, Category = Execution)
	void ExecuteWithContext(UPARAM(ref)FPCGContext& InContext, const FPCGDataCollection& Input, FPCGDataCollection& Output);

	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = Execution)
	void Execute(const FPCGDataCollection& Input, FPCGDataCollection& Output);

	UFUNCTION(BlueprintImplementableEvent, Category = Execution)
	bool PointLoopBody(UPARAM(ref) FPCGContext& InContext, const UPCGPointData* InData, const FPCGPoint& InPoint, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const;

	UFUNCTION(BlueprintImplementableEvent, Category = Execution)
	bool PointPairLoopBody(UPARAM(ref) FPCGContext& InContext, const UPCGPointData* InA, const UPCGPointData* InB, const FPCGPoint& InPointA, const FPCGPoint& InPointB, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const;

	UFUNCTION(BlueprintImplementableEvent, Category = Execution)
	bool IterationLoopBody(UPARAM(ref) FPCGContext& InContext, int64 Iteration, const UPCGSpatialData* InA, const UPCGSpatialData* InB, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const;

	/** Calls the LoopBody function on all points */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = Execution, meta = (HideSelfPin = "true"))
	void LoopOnPoints(UPARAM(ref) FPCGContext& InContext, const UPCGPointData* InData, UPCGPointData*& OutData, UPCGPointData* OptionalOutData = nullptr) const;

	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = Execution, meta = (HideSelfPin = "true"))
	void LoopOnPointPairs(UPARAM(ref) FPCGContext& InContext, const UPCGPointData* InA, const UPCGPointData* InB, UPCGPointData*& OutData, UPCGPointData* OptionalOutData = nullptr) const;

	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = Execution, meta = (HideSelfPin = "true"))
	void LoopNTimes(UPARAM(ref) FPCGContext& InContext, int64 NumIterations, UPCGPointData*& OutData, const UPCGSpatialData* InA = nullptr, const UPCGSpatialData* InB = nullptr, UPCGPointData* OptionalOutData = nullptr) const;

	/** Override for the default node name */
	UFUNCTION(BlueprintNativeEvent, Category = Graph)
	FName NodeTitleOverride() const;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Input & Output")
	TSet<FName> InputPinLabels;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Input & Output")
	TSet<FName> OutputPinLabels;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Input & Output")
	bool bHasDefaultInPin = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Input & Output")
	bool bHasDefaultOutPin = true;

protected:
#if WITH_EDITOR
	void OnDependencyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent);
	TSet<TObjectPtr<UObject>> DataDependencies;
#endif
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGBlueprintSettings : public UPCGSettings
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
	virtual TArray<FName> InLabels() const override;
	virtual TArray<FName> OutLabels() const override;
	virtual bool HasDefaultInLabel() const override;
	virtual bool HasDefaultOutLabel() const override;

protected:
	virtual FPCGElementPtr CreateElement() const override;
	// ~End UPCGSettings interface

public:
	// ~Begin UObject interface
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	// ~End UObject interface
#endif

	UFUNCTION(BlueprintCallable, Category = Settings, meta=(DeterminesOutputType="InElementType", DynamicOutputParam = "ElementInstance"))
	void SetElementType(TSubclassOf<UPCGBlueprintElement> InElementType, UPCGBlueprintElement*& ElementInstance);

protected:
	UPROPERTY()
	TSubclassOf<UPCGBlueprintElement> BlueprintElement_DEPRECATED;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Template)
	TSubclassOf<UPCGBlueprintElement> BlueprintElementType;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Instanced, Category = Settings, meta = (ShowOnlyInnerProperties))
	TObjectPtr<UPCGBlueprintElement> BlueprintElementInstance;

#if WITH_EDITORONLY_DATA
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TArray<FName> TrackedActorTags;
#endif

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bCreatesArtifacts = false;

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

struct FPCGBlueprintExecutionContext : public FPCGContext
{
	UPCGBlueprintElement* BlueprintElementInstance = nullptr;
};

class FPCGExecuteBlueprintElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
	virtual FPCGContext* Initialize(const FPCGDataCollection& InputData, UPCGComponent* SourceComponent, const UPCGNode* Node) override;
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override;
};
