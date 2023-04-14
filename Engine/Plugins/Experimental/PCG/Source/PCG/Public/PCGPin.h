// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGCommon.h"

#include "PCGPin.generated.h"

class UPCGNode;
class UPCGEdge;

USTRUCT(BlueprintType)
struct PCG_API FPCGPinProperties
{
	GENERATED_BODY()

	FPCGPinProperties() = default;
	explicit FPCGPinProperties(const FName& InLabel, EPCGDataType InAllowedTypes = EPCGDataType::Any, bool bInAllowMultipleConnections = true, bool bAllowMultipleData = true, const FText& InTooltip = FText::GetEmpty());

	bool operator==(const FPCGPinProperties& Other) const;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FName Label = NAME_None;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGDataType AllowedTypes = EPCGDataType::Any;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bAllowMultipleData = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bAllowMultipleConnections = true;

	/* Advanced pin will be hidden by default in the UI and will be shown only if the user extend the node (in the UI) to see advanced pins. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bAdvancedPin = false;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = Settings)
	FText Tooltip;
#endif
};

UCLASS(ClassGroup = (Procedural))
class PCG_API UPCGPin : public UObject
{
	GENERATED_BODY()

public:
	UPCGPin(const FObjectInitializer& ObjectInitializer);

	// ~Begin UObject interface
	virtual void PostLoad() override;
	// ~End UObject interface

	UPROPERTY(BlueprintReadOnly, Category = Properties)
	TObjectPtr<UPCGNode> Node = nullptr;

	UPROPERTY()
	FName Label_DEPRECATED = NAME_None;

	UPROPERTY(BlueprintReadOnly, TextExportTransient, Category = Properties)
	TArray<TObjectPtr<UPCGEdge>> Edges;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, meta = (ShowOnlyInnerProperties))
	FPCGPinProperties Properties;

	UFUNCTION(BlueprintCallable, Category = Settings)
	FText GetTooltip() const;
	
	UFUNCTION(BlueprintCallable, Category = Settings)
	void SetTooltip(const FText& InTooltip);

	bool AllowMultipleConnections() const;
	bool IsCompatible(const UPCGPin* OtherPin) const;
	bool CanConnect(const UPCGPin* OtherPin) const;

	/** Adds edge to pin if edge does not exist. Optionally returns list of affected nodes (including the pin owner). */
	bool AddEdgeTo(UPCGPin* OtherPin, TSet<UPCGNode*>* InTouchedNodes = nullptr);

	/** Breaks edge to pin if edge exists. Optionally returns list of affected nodes (including the pin owner). */
	bool BreakEdgeTo(UPCGPin* OtherPin, TSet<UPCGNode*>* InTouchedNodes = nullptr);

	/** Breaks all connected edges. Optionally returns list of affected nodes (including the pin owner). */
	bool BreakAllEdges(TSet<UPCGNode*>* InTouchedNodes = nullptr);

	/** Break all edges the connect pins of invalid type. Optionally returns list of affected nodes (including the pin owner). */
	bool BreakAllIncompatibleEdges(TSet<UPCGNode*>* InTouchedNodes = nullptr);

	bool IsConnected() const;
	bool IsOutputPin() const;
	int32 EdgeCount() const;

	/** Returns the current pin types, which can either be the static types from the pin properties, or a dynamic type based on connected edges. */
	EPCGDataType GetCurrentTypes() const;
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#endif
