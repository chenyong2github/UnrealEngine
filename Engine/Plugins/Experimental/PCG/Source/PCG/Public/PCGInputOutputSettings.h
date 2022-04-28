// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"

#include "PCGInputOutputSettings.generated.h"

namespace PCGInputOutputConstants
{
	const FName DefaultInLabel = TEXT("In");
	const FName DefaultInputLabel = TEXT("Input");
	const FName DefaultActorLabel = TEXT("Actor");
	const FName DefaultOriginalActorLabel = TEXT("OriginalActor");
	const FName DefaultExcludedActorsLabel = TEXT("ExcludedActors");

	const FName DefaultOutLabel = TEXT("Out");
}

class PCG_API FPCGInputOutputElement : public FSimplePCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }
};

UCLASS(NotBlueprintable, Hidden, ClassGroup = (Procedural))
class PCG_API UPCGGraphInputOutputSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	UPCGGraphInputOutputSettings(const FObjectInitializer& ObjectInitializer);

	// ~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return bIsInput ? FName(TEXT("InputNode")) : FName(TEXT("OutputNode")); }
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::InputOutput; }
#endif

	virtual bool HasInLabel(const FName& Label) const override { return Label == NAME_None || StaticLabels().Contains(Label) || (bShowAdvancedPins && StaticAdvancedLabels().Contains(Label)) || PinLabels.Contains(Label); }
	virtual bool HasOutLabel(const FName& Label) const override { return Label == NAME_None || StaticLabels().Contains(Label) || (bShowAdvancedPins && StaticAdvancedLabels().Contains(Label)) || PinLabels.Contains(Label); }
	virtual bool HasDefaultInLabel() const override { return false; }
	virtual bool HasDefaultOutLabel() const override { return false; }

	virtual TArray<FName> InLabels() const override
	{
		TArray<FName> Labels = StaticLabels();
		if (bShowAdvancedPins)
		{
			Labels += StaticAdvancedLabels();
		}
		
		Labels += PinLabels.Array();
		return Labels;
	}

	virtual TArray<FName> OutLabels() const override
	{
		TArray<FName> Labels = StaticLabels();
		if (bShowAdvancedPins)
		{
			Labels += StaticAdvancedLabels();
		}

		Labels += PinLabels.Array();
		return Labels;
	}

	void SetInput(bool bInIsInput) { bIsInput = bInIsInput; }

protected:
	virtual FPCGElementPtr CreateElement() const override { return MakeShared<FPCGInputOutputElement>(); }
	// ~End UPCGSettings interface

	const TArray<FName>& StaticLabels() const { return bIsInput ? StaticInLabels : StaticOutLabels; }
	const TArray<FName>& StaticAdvancedLabels() const { return bIsInput ? StaticAdvancedInLabels : StaticAdvancedOutLabels; }

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Input")
	TSet<FName> PinLabels;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Input")
	bool bShowAdvancedPins = false;

	TArray<FName> StaticInLabels;
	TArray<FName> StaticAdvancedInLabels;
	TArray<FName> StaticOutLabels;
	TArray<FName> StaticAdvancedOutLabels;

	bool bIsInput = false;
};