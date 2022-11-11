// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IChooserColumn.h"
#include "IChooserParameterFloat.h"
#include "ChooserPropertyAccess.h"
#include "FloatRangeColumn.generated.h"

UCLASS()
class CHOOSER_API UChooserParameterFloat_ContextProperty :  public UObject, public IChooserParameterFloat
{
	GENERATED_BODY()
public:
	UPROPERTY()
	TArray<FName> PropertyBindingChain;
	
	virtual bool GetValue(const UObject* ContextObject, float& OutResult) const override;

#if WITH_EDITOR
	static bool CanBind(const FProperty& Property)
	{
		static FString DoubleTypeName = "double";
		static FString FloatTypeName = "float";
		const FString& TypeName = Property.GetCPPType();
		return TypeName == FloatTypeName || TypeName == DoubleTypeName;
	}
	
	void SetBinding(const TArray<FBindingChainElement>& InBindingChain)
	{
		UE::Chooser::CopyPropertyChain(InBindingChain, PropertyBindingChain);
	}
#endif
};

USTRUCT()
struct FChooserFloatRangeRowData
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, Category=Runtime)
	float Min=0;
	
	UPROPERTY(EditAnywhere, Category=Runtime)
	float Max=0;
};

UCLASS()
class CHOOSER_API UChooserColumnFloatRange : public UObject, public IChooserColumn
{
	GENERATED_BODY()
	public:
	UChooserColumnFloatRange() {}
	UChooserColumnFloatRange(const FObjectInitializer& ObjectInitializer);
		
	UPROPERTY(EditAnywhere, Meta = (EditInlineInterface = "true"), Category = "Input")
	TScriptInterface<IChooserParameterFloat> InputValue;
	
	UPROPERTY(EditAnywhere, Category=Runtime)
	// array of results (cells for this column for each row in the table)
	// should match the length of the Results array 
	TArray<FChooserFloatRangeRowData> RowValues;
	
	virtual void Filter(const UObject* ContextObject, const TArray<uint32>& IndexListIn, TArray<uint32>& IndexListOut) override;
	virtual void SetNumRows(uint32 NumRows) { RowValues.SetNum(NumRows); }
	virtual void DeleteRows(const TArray<uint32> & RowIndices )
	{
		for(uint32 Index : RowIndices)
		{
			RowValues.RemoveAt(Index);
		}
	}
	virtual UClass* GetInputValueInterface() { return UChooserParameterFloat::StaticClass(); };
	virtual UObject* GetInputValue() override { return InputValue.GetObject(); };
	virtual void SetInputValue(UObject* Value) override { InputValue = Value; };
};