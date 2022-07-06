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

	// Rotator is represented as X->Roll, Y->Pitch, Z->Yaw

	/*
	 *	Function to get current value in text box 0
	 *
	 *	@return current string value
	 */
	FString GetCurrentValue_0() const
	{
		// Text box 0: Rotator->Roll, Vector->X
		return GetValue(bIsRotator ? TextBox_2 : TextBox_0);
	}

	/*
	 *	Function to get current value in text box 1
	 *
	 *	@return current string value
	 */
	FString GetCurrentValue_1() const
	{
		// Text box 1: Rotator->Pitch, Vector->Y
		return GetValue(bIsRotator ? TextBox_0 : TextBox_1);
	}

	/*
	 *	Function to get current value in text box 2
	 *
	 *	@return current string value
	 */
	FString GetCurrentValue_2() const
	{
		// Text box 2: Rotator->Yaw, Vector->Z
		return GetValue(bIsRotator ? TextBox_1 : TextBox_2);
	}

	/*
	 *	Function to getch current value based on text box index value
	 *
	 *	@param: Text box index
	 *
	 *	@return current string value
	 */
	FString GetValue( ETextBoxIndex Index ) const
	{
		FString DefaultString = GraphPinObj->GetDefaultAsString();
		TArray<FString> ResultString;

		// Parse string to split its contents separated by ','
		DefaultString.TrimStartInline();
		DefaultString.TrimEndInline();
		DefaultString.ParseIntoArray(ResultString, TEXT(","), true);

		if (Index < ResultString.Num())
		{
			return ResultString[Index];
		}
		else
		{
			return FString(TEXT("0"));
		}
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

		const FString ValueStr = FString::Printf(TEXT("%f"), NewValue);

		FString DefaultValue;
		if (bIsRotator)
		{
			// Update Roll value
			DefaultValue = GetValue(TextBox_0) + FString(TEXT(",")) + GetValue(TextBox_1) + FString(TEXT(",")) + ValueStr;
		}
		else
		{
			// Update X value
			DefaultValue = ValueStr + FString(TEXT(",")) + GetValue(TextBox_1) + FString(TEXT(",")) + GetValue(TextBox_2);
		}

		if (GraphPinObj->GetDefaultAsString() != DefaultValue)
		{
			const FScopedTransaction Transaction(NSLOCTEXT("GraphEditor", "ChangeVectorPinValue", "Change Vector Pin Value"));
			GraphPinObj->Modify();

			// Set new default value
			GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, DefaultValue);
		}
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

		const FString ValueStr = FString::Printf(TEXT("%f"), NewValue);

		FString DefaultValue;
		if (bIsRotator)
		{
			// Update Pitch value
			DefaultValue = ValueStr + FString(TEXT(",")) + GetValue(TextBox_1) + FString(TEXT(",")) + GetValue(TextBox_2);
		}
		else
		{
			// Update Y value
			DefaultValue = GetValue(TextBox_0) + FString(TEXT(",")) + ValueStr + FString(TEXT(",")) + GetValue(TextBox_2);
		}

		if (GraphPinObj->GetDefaultAsString() != DefaultValue)
		{
			const FScopedTransaction Transaction(NSLOCTEXT("GraphEditor", "ChangeVectorPinValue", "Change Vector Pin Value"));
			GraphPinObj->Modify();

			// Set new default value
			GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, DefaultValue);
		}
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

		const FString ValueStr = FString::Printf(TEXT("%f"), NewValue);

		FString DefaultValue;
		if (bIsRotator)
		{
			// Update Yaw value
			DefaultValue = GetValue(TextBox_0) + FString(TEXT(",")) + ValueStr + FString(TEXT(",")) + GetValue(TextBox_2);
		}
		else
		{
			// Update Z value
			DefaultValue = GetValue(TextBox_0) + FString(TEXT(",")) + GetValue(TextBox_1) + FString(TEXT(",")) + ValueStr;
		}

		if (GraphPinObj->GetDefaultAsString() != DefaultValue)
		{
			const FScopedTransaction Transaction(NSLOCTEXT("GraphEditor", "ChangeVectorPinValue", "Change Vector Pin Value"));
			GraphPinObj->Modify();

			// Set new default value
			GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, DefaultValue);
		}
	}

private:
	// Flag is true if the widget is used to represent a rotator; false otherwise
	bool bIsRotator;
};
