// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "SGraphPin.h"
#include "SVectorTextBox.h"
#include "ScopedTransaction.h"

template <typename NumericType>
class SGraphPinVector : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SGraphPinVector) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
	{
		SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);
	}

protected:

	/**
	 *	Function to create class specific widget.
	 *
	 *	@return Reference to the newly created widget object
	 */
	virtual TSharedRef<SWidget>	GetDefaultValueWidget() override
	{
		UScriptStruct* RotatorStruct = TBaseStructure<FRotator>::Get();
		bIsRotator = (GraphPinObj->PinType.PinSubCategoryObject == RotatorStruct) ? true : false;
	
		return	SNew( SVectorTextBox<NumericType>, bIsRotator )
				.VisibleText_0(this, &SGraphPinVector::GetCurrentValue_0)
				.VisibleText_1(this, &SGraphPinVector::GetCurrentValue_1)
				.VisibleText_2(this, &SGraphPinVector::GetCurrentValue_2)
				.Visibility( this, &SGraphPin::GetDefaultValueVisibility)
				.IsEnabled(this, &SGraphPin::GetDefaultValueIsEditable)
				.OnNumericCommitted_Box_0(this, &SGraphPinVector::OnChangedValueTextBox_0)
				.OnNumericCommitted_Box_1(this, &SGraphPinVector::OnChangedValueTextBox_1)
				.OnNumericCommitted_Box_2(this, &SGraphPinVector::OnChangedValueTextBox_2);
	}

private:

	// Enum values to identify text boxes.
	enum ETextBoxIndex
	{
		TextBox_0,
		TextBox_1,
		TextBox_2,
	};

	using FVectorType = UE::Math::TVector<NumericType>;

	// Rotator is represented as X->Roll, Y->Pitch, Z->Yaw

	/*
	 *	Function to get current value in text box 0
	 *
	 *	@return current string value
	 */
	FString GetCurrentValue_0() const
	{
		// Text box 0: Rotator->Roll, Vector->X
		FVectorType Vector = ConvertDefaultValueStringToVector();
		return TDefaultNumericTypeInterface<NumericType>{}.ToString(bIsRotator ? Vector.Z : Vector.X);
	}

	/*
	 *	Function to get current value in text box 1
	 *
	 *	@return current string value
	 */
	FString GetCurrentValue_1() const
	{
		// Text box 1: Rotator->Pitch, Vector->Y
		FVectorType Vector = ConvertDefaultValueStringToVector();
		return TDefaultNumericTypeInterface<NumericType>{}.ToString(bIsRotator ? Vector.X : Vector.Y);
	}

	/*
	 *	Function to get current value in text box 2
	 *
	 *	@return current string value
	 */
	FString GetCurrentValue_2() const
	{
		// Text box 2: Rotator->Yaw, Vector->Z
		FVectorType Vector = ConvertDefaultValueStringToVector();
		return TDefaultNumericTypeInterface<NumericType>{}.ToString(bIsRotator ? Vector.Y : Vector.Z);
	}

	/*
	 *	Function to store value when text box 0 value in modified
	 *
	 *	@param 0: Updated numeric Value
	 */
	void OnChangedValueTextBox_0(NumericType NewValue, ETextCommit::Type CommitInfo)
	{
		if (GraphPinObj->IsPendingKill())
		{
			return;
		}

		FVectorType NewVector = ConvertDefaultValueStringToVector();
		NumericType OldValue;

		if (bIsRotator)
		{
			/* update roll */
			OldValue = NewVector.Z;
			NewVector.Z = NewValue;
		}
		else
		{
			/* X value */
			OldValue = NewVector.X;
			NewVector.X = NewValue;
		}

		SetNewValueHelper(OldValue, NewValue, NewVector);

	}

	/*
	 *	Function to store value when text box 1 value in modified
	 *
	 *	@param 0: Updated numeric Value
	 */
	void OnChangedValueTextBox_1(NumericType NewValue, ETextCommit::Type CommitInfo)
	{
		if (GraphPinObj->IsPendingKill())
		{
			return;
		}

		FVectorType NewVector = ConvertDefaultValueStringToVector();
		NumericType OldValue;

		if (bIsRotator)
		{
			/* update pitch */
			OldValue = NewVector.X;
			NewVector.X = NewValue;
		}
		else
		{
			/* Y value */
			OldValue = NewVector.Y;
			NewVector.Y = NewValue;
		}

		SetNewValueHelper(OldValue, NewValue, NewVector);
	}

	/*
	 *	Function to store value when text box 2 value in modified
	 *
	 *	@param 0: Updated numeric Value
	 */
	void OnChangedValueTextBox_2(NumericType NewValue, ETextCommit::Type CommitInfo)
	{
		if (GraphPinObj->IsPendingKill())
		{
			return;
		}
		
		FVectorType NewVector = ConvertDefaultValueStringToVector();
		NumericType OldValue;

		if (bIsRotator)
		{
			/* update yaw */
			OldValue = NewVector.Y;
			NewVector.Y = NewValue;
		}
		else
		{
			/* Z value */
			OldValue = NewVector.Z;
			NewVector.Z = NewValue;
		}

		SetNewValueHelper(OldValue, NewValue, NewVector);
	}

	void SetNewValueHelper(NumericType OldValue, NumericType NewValue, FVectorType const& NewVector)
	{
		if (OldValue == NewValue)
		{
			return;
		}

		const FScopedTransaction Transaction(NSLOCTEXT("GraphEditor", "ChangeVectorPinValue", "Change Vector Pin Value"));
		GraphPinObj->Modify();

		// Create the new value string
		FString DefaultValue = FString::Format(TEXT("{0},{1},{2}"), { NewVector.X, NewVector.Y, NewVector.Z });

		//Set new default value
		GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, DefaultValue);
	}

	/*
	 * @brief Converts the default string value to a value of VectorType.
	 * 
	 * Example: it converts the string "(2.00,3.00,4.00)" to the corresponding 3D vector.
	 */
	FVectorType ConvertDefaultValueStringToVector() const
	{
		FString DefaultString = GraphPinObj->GetDefaultAsString();
		DefaultString.TrimStartInline();
		DefaultString.TrimEndInline();

		// Parse string to split its contents separated by ','
		TArray<FString> VecComponentStrings;
		DefaultString.ParseIntoArray(VecComponentStrings, TEXT(","), true);

		check(VecComponentStrings.Num() == 3);

		TDefaultNumericTypeInterface<NumericType> NumericTypeInterface{};

		// Construct the vector from the string parts.
		FVectorType Vec = FVectorType::ZeroVector;
		Vec.X = NumericTypeInterface.FromString(VecComponentStrings[0], 0).Get(0);
		Vec.Y = NumericTypeInterface.FromString(VecComponentStrings[1], 0).Get(0);
		Vec.Z = NumericTypeInterface.FromString(VecComponentStrings[2], 0).Get(0);

		return Vec;
	}

private:
	// Flag is true if the widget is used to represent a rotator; false otherwise
	bool bIsRotator;
};
