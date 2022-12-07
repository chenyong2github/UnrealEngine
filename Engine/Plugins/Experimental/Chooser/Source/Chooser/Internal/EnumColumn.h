// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IChooserColumn.h"
#include "IChooserParameterEnum.h"
#include "EnumColumn.generated.h"

struct FBindingChainElement;

UCLASS(DisplayName = "Enum Property Binding")
class CHOOSER_API UChooserParameterEnum_ContextProperty : public UObject, public IChooserParameterEnum
{
	GENERATED_BODY()
public:
	UPROPERTY()
	TArray<FName> PropertyBindingChain;

	virtual bool GetValue(const UObject* ContextObject, uint8& OutResult) const override;

#if WITH_EDITOR
	static bool CanBind(const FProperty& Property)
	{
		if (Property.IsA<FEnumProperty>())
		{
			return true;
		}

		if (const FByteProperty* ByteProperty = CastField<FByteProperty>(&Property))
		{
			return ByteProperty->Enum != nullptr;
		}

		return false;
	}

	void SetBinding(const TArray<FBindingChainElement>& InBindingChain);

	virtual const UEnum* GetEnum() const override { return Enum; }

	virtual FSimpleMulticastDelegate& OnEnumChanged() { return EnumChanged; }
#endif

private:
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<const UEnum> Enum = nullptr;

	FSimpleMulticastDelegate EnumChanged;
#endif
};


UENUM()
enum class EChooserEnumComparison : uint8
{
	Equal UMETA(DisplayName = "Value =="),
	NotEqual UMETA(DisplayName = "Value !="),
	GreaterThan UMETA(DisplayName = "Value >"),
	GreaterThanEqual UMETA(DisplayName = "Value >="),
	LessThan UMETA(DisplayName = "Value <"),
	LessThanEqual UMETA(DisplayName = "Value <="),
};

USTRUCT()
struct FChooserEnumRowData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Runtime)
	EChooserEnumComparison Comparison = EChooserEnumComparison::Equal;

	UPROPERTY(EditAnywhere, Category = Runtime)
	uint8 Value = 0;

	bool Evaluate(const uint8 LeftHandSide) const;
};

UCLASS()
class CHOOSER_API UChooserColumnEnum : public UObject, public IChooserColumn
{
	GENERATED_BODY()
public:
	UChooserColumnEnum() = default;
	UChooserColumnEnum(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, Meta = (EditInlineInterface = "true"), Category = "Input")
	TScriptInterface<IChooserParameterEnum> InputValue;

	UPROPERTY(EditAnywhere, Category = Runtime)
	// array of results (cells for this column for each row in the table)
	// should match the length of the Results array
	TArray<FChooserEnumRowData> RowValues;

	virtual void Filter(const UObject* ContextObject, const TArray<uint32>& IndexListIn, TArray<uint32>& IndexListOut) override;
	virtual void SetNumRows(uint32 NumRows) { RowValues.SetNum(NumRows); }
	virtual void DeleteRows(const TArray<uint32>& RowIndices)
	{
		for (uint32 Index : RowIndices)
		{
			RowValues.RemoveAt(Index);
		}
	}
	virtual UClass* GetInputValueInterface() { return UChooserParameterEnum::StaticClass(); };
	virtual UObject* GetInputValue() override { return InputValue.GetObject(); };
	virtual void SetInputValue(UObject* Value) override
	{
		InputValue = Value;
#if WITH_EDITOR
		InputChanged();
#endif
	};

#if WITH_EDITOR
	FSimpleMulticastDelegate OnEnumChanged;
	void InputChanged()
	{
		InputValue->OnEnumChanged().AddLambda([this](){ OnEnumChanged.Broadcast(); });
		OnEnumChanged.Broadcast();
	}

	virtual void PostLoad() override
	{
		Super::PostLoad();
		InputChanged();
	}
#endif
};