// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"

#include "PCGInputOutputSettings.generated.h"

UCLASS(NotBlueprintable, Hidden, ClassGroup = (Procedural))
class PCG_API UPCGGraphInputOutputSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	// ~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("InputNode")); }
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::InputOutput; }
#endif

	virtual bool HasInLabel(const FName& Label) const override { return Label == NAME_None || PinLabels.Contains(Label); }
	virtual bool HasOutLabel(const FName& Label) const override { return Label == NAME_None || PinLabels.Contains(Label); }
	virtual TArray<FName> InLabels() const override { return PinLabels.Array(); }
	virtual TArray<FName> OutLabels() const override { return PinLabels.Array(); }

protected:
	virtual FPCGElementPtr CreateElement() const override { return MakeShared<FPCGTrivialElement>(); }
	// ~End UPCGSettings interface

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Input")
	TSet<FName> PinLabels;
};