// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
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

	bool IsCompatible(const UPCGPin* OtherPin) const;
	bool CanConnect(const UPCGPin* OtherPin) const;
	bool AddEdgeTo(UPCGPin* OtherPin);
	bool BreakEdgeTo(UPCGPin* OtherPin);
	bool BreakAllEdges();
	bool BreakAllIncompatibleEdges();
	bool IsConnected() const;
	bool IsOutputPin() const;
	int32 EdgeCount() const;
};
