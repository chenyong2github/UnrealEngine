// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/NumericTypeInterface.h"

class FText;

/**
 * Blueprint review changelist slate widget 
 */
class SChangelistEditableText : public SEditableText
{
public:
	void Construct(const FArguments& InArgs);
private:
	//~ Begin ISlateEditableTextWidget Interface
	virtual void OnTextChanged(const FText& InText) override;
	virtual void OnTextCommitted(const FText& InText, const ETextCommit::Type InTextAction) override;
	//~ End ISlateEditableTextWidget Interface
	
	TAttribute<int64> ValueAttribute;
	static TDefaultNumericTypeInterface<int64> NumericTypeUtil;
	static bool IsCharacterValid(TCHAR InChar);
};
