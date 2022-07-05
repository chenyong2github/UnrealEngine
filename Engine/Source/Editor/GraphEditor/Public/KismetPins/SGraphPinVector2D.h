// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "SGraphPin.h"
#include "SVector2DTextBox.h"

template <typename NumericType>
class SGraphPinVector2D : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SGraphPinVector2D) {}
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
		// Create widget
		return SNew(SVector2DTextBox<NumericType>)
			.VisibleText_X(this, &SGraphPinVector2D::GetCurrentValue_X)
			.VisibleText_Y(this, &SGraphPinVector2D::GetCurrentValue_Y)
			.Visibility(this, &SGraphPin::GetDefaultValueVisibility)
			.IsEnabled(this, &SGraphPin::GetDefaultValueIsEditable)
			.OnNumericCommitted_Box_X(this, &SGraphPinVector2D::OnChangedValueTextBox_X)
			.OnNumericCommitted_Box_Y(this, &SGraphPinVector2D::OnChangedValueTextBox_Y);
	}

private:

	// Enum values to identify text boxes.
	enum ETextBoxIndex
	{
		TextBox_X,
		TextBox_Y
	};

	/*
	 *	Function to get current value in text box 0
	 *
	 *	@return current string value
	 */
	FString GetCurrentValue_X() const
	{
		return GetValue(TextBox_X);
	}

	/*
	 *	Function to get current value in text box 1
	 *
	 *	@return current string value
	 */
	FString GetCurrentValue_Y() const
	{
		return GetValue(TextBox_Y);
	}

	/*
	 *	Function to getch current value based on text box index value
	 *
	 *	@param: Text box index
	 *
	 *	@return current string value
	 */
	FString GetValue(ETextBoxIndex Index) const
	{
		FString DefaultString = GraphPinObj->GetDefaultAsString();
		TArray<FString> ResultString;

		FVector2D Value;
		Value.InitFromString(DefaultString);

		if (Index == TextBox_X)
		{
			return FString::Printf(TEXT("%f"), Value.X);
		}
		else
		{
			return FString::Printf(TEXT("%f"), Value.Y);
		}
	}

	FString MakeVector2DString(const FString& X, const FString& Y)
	{
		return FString(TEXT("(X=")) + X + FString(TEXT(",Y=")) + Y + FString(TEXT(")"));
	}

	/*
	 *	Function to store value when text box 0 value in modified
	 *
	 *	@param 0: Updated numeric value
	 */
	void OnChangedValueTextBox_X(NumericType NewValue, ETextCommit::Type CommitInfo)
	{
		if (GraphPinObj->IsPendingKill())
		{
			return;
		}

		const FString ValueStr = FString::Printf(TEXT("%f"), NewValue);
		const FString Vector2DString = MakeVector2DString(ValueStr, GetValue(TextBox_Y));

		if (GraphPinObj->GetDefaultAsString() != Vector2DString)
		{
			const FScopedTransaction Transaction(NSLOCTEXT("GraphEditor", "ChangeVectorPinValue", "Change Vector Pin Value"));
			GraphPinObj->Modify();
			GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, Vector2DString);
		}
	}

	/*
	 *	Function to store value when text box 1 value in modified
	 *
	 *	@param 0: Updated numeric value
	 */
	void OnChangedValueTextBox_Y(NumericType NewValue, ETextCommit::Type CommitInfo)
	{
		if (GraphPinObj->IsPendingKill())
		{
			return;
		}

		const FString ValueStr = FString::Printf(TEXT("%f"), NewValue);
		const FString Vector2DString = MakeVector2DString(GetValue(TextBox_X), ValueStr);

		if (GraphPinObj->GetDefaultAsString() != Vector2DString)
		{
			const FScopedTransaction Transaction(NSLOCTEXT("GraphEditor", "ChangeVectorPinValue", "Change Vector Pin Value"));
			GraphPinObj->Modify();
			GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, Vector2DString);
		}
	}
};
