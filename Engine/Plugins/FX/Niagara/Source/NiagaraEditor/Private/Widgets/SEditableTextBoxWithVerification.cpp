// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Widgets/SEditableTextBoxWithVerification.h"
#include "Widgets/Input/SEditableTextBox.h"

void SEditableTextBoxWithVerification::Construct(const FArguments& InArgs)
{
	OnVerifyTextChangedDelegate = InArgs._OnVerifyTextChanged;
	OnTextCommittedDelegate = InArgs._OnTextCommitted;
	ChildSlot
	[
		SAssignNew(TextBox, SEditableTextBox)
			.Text(InArgs._Text)
			.OnTextChanged(this, &SEditableTextBoxWithVerification::OnTextChanged)
			.OnTextCommitted(this, &SEditableTextBoxWithVerification::OnTextBoxCommitted)
	];
}

void SEditableTextBoxWithVerification::OnTextChanged(const FText& InText)
{
	FText OutErrorMessage;
	if (OnVerifyTextChangedDelegate.IsBound() && OnVerifyTextChangedDelegate.Execute(InText, OutErrorMessage) == false)
	{
		TextBox->SetError(OutErrorMessage);
	}
	else
	{
		TextBox->SetError(FText::GetEmpty());
	}
}

void SEditableTextBoxWithVerification::OnTextBoxCommitted(const FText& InText, ETextCommit::Type InCommitType)
{
	FText OutErrorMessage;
	if (OnVerifyTextChangedDelegate.IsBound() && OnVerifyTextChangedDelegate.Execute(InText, OutErrorMessage) == false)
	{
		if (InCommitType == ETextCommit::OnEnter)
		{
			TextBox->SetError(OutErrorMessage);
		}
		else
		{
			TextBox->SetError(FText::GetEmpty());
		}
	}
	else
	{
		OnTextCommittedDelegate.ExecuteIfBound(InText, InCommitType);
	}
}