// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Delegates/DelegateCombinations.h"
#include "Framework/SlateDelegates.h"

class SEditableTextBox;

/** An editable text block which validates input with a delegate. */
class SEditableTextBoxWithVerification : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_RetVal_TwoParams(bool, FOnVerifyTextChanged, const FText&, FText&)

public:
	SLATE_BEGIN_ARGS(SEditableTextBoxWithVerification)
	{}
		/** The text displayed in this text block */
		SLATE_ATTRIBUTE(FText, Text)

		/** Called whenever the text is changed interactively by the user */
		SLATE_EVENT(FOnVerifyTextChanged, OnVerifyTextChanged)

		/** Called when the text has been committed to the text box.  Text will not be committed if it fails verification. */
		SLATE_EVENT(FOnTextCommitted, OnTextCommitted);

	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs);

private:
	void OnTextChanged(const FText& InText);

	void OnTextBoxCommitted(const FText& InText, ETextCommit::Type InCommitType);

private:
	TSharedPtr<SEditableTextBox> TextBox;
	FOnVerifyTextChanged OnVerifyTextChangedDelegate;
	FOnTextCommitted OnTextCommittedDelegate;
};
