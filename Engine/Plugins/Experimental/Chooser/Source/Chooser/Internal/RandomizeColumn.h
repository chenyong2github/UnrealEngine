// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IChooserColumn.h"
#include "IChooserParameterRandomize.h"
#include "ChooserPropertyAccess.h"
#include "InstancedStruct.h"
#include "RandomizeColumn.generated.h"

USTRUCT(DisplayName = "Randomize Property Binding")
struct CHOOSER_API FRandomizeContextProperty :  public FChooserParameterRandomizeBase
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, Meta = (BindingType = "FChooserRandomizationContext", BindingAllowFunctions = "false", BindingColor = "StructPinTypeColor"), Category = "Binding")
	FChooserPropertyBinding Binding;
	
	virtual bool GetValue(const UObject* ContextObject, const FChooserRandomizationContext*& OutResult) const override;

#if WITH_EDITOR
	static bool CanBind(const FProperty& Property)
	{
		static FString RandomizeTypeName = "bool";
		return Property.GetCPPType() == RandomizeTypeName;
	}

	void SetBinding(const TArray<FBindingChainElement>& InBindingChain)
	{
		UE::Chooser::CopyPropertyChain(InBindingChain, Binding.PropertyBindingChain);
	}

	virtual void GetDisplayName(FText& OutName) const override
	{
		if (!Binding.PropertyBindingChain.IsEmpty())
		{
			OutName = FText::FromName(Binding.PropertyBindingChain.Last());
		}
	}
#endif
};

USTRUCT()
struct CHOOSER_API FRandomizeColumn : public FChooserColumnBase
{
	GENERATED_BODY()
	public:
	FRandomizeColumn();
	
	UPROPERTY(EditAnywhere, Meta = (ExcludeBaseStruct, BaseStruct = "/Script/Chooser.RandomizeContextProperty"), Category = "Data")
	FInstancedStruct InputValue;
	
	
	UPROPERTY(EditAnywhere, Category= "Data", meta=(Tooltip="Multiplies the weight of the previous chosen result (set to 0 to never pick the same result twice in a row)"));
	float RepeatProbabilityMultiplier = 1.0f;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category= "Data", DisplayName="DefaultRowValue");
	float DefaultRowValue = 1.0f;
#endif
	
	UPROPERTY(EditAnywhere, Category= "Data", DisplayName="RowValues");
	TArray<float> RowValues; 
	
	virtual void Filter(const UObject* ContextObject, const TArray<uint32>& IndexListIn, TArray<uint32>& IndexListOut) const override;
	virtual void SetOutputs(UObject* ContextObject, int RowIndex) const override;
	
	virtual bool HasFilters() const override { return true; }
	virtual bool HasOutputs() const override { return true; }

	virtual void PostLoad() override
	{
		if (InputValue.IsValid())
		{
			InputValue.GetMutable<FChooserParameterBase>().PostLoad();
		}
	}

	CHOOSER_COLUMN_BOILERPLATE(FChooserParameterRandomizeBase);

#if WITH_EDITOR
	virtual bool IsRandomizeColumn() const override { return true; }
#endif
};