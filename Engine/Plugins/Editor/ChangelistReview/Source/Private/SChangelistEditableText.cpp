// Copyright Epic Games, Inc. All Rights Reserved.

#include "SChangelistEditableText.h"

#include "Containers/UnrealString.h"
#include "Framework/SlateDelegates.h"
#include "HAL/Platform.h"
#include "Internationalization/Text.h"
#include "Templates/UniquePtr.h"
#include "Widgets/Text/SlateEditableTextLayout.h"

TDefaultNumericTypeInterface<int64> SChangelistEditableText::NumericTypeUtil;

void SChangelistEditableText::Construct(const FArguments& InArgs)
{
	SEditableText::Construct(InArgs);
}

void SChangelistEditableText::OnTextCommitted(const FText& InText, const ETextCommit::Type InTextAction)
{
	TOptional<int64> NewValue = NumericTypeUtil.FromString(InText.ToString(), ValueAttribute.Get());
	
	if (NewValue.IsSet())
	{
		if (!ValueAttribute.IsBound())
		{
			ValueAttribute.Set(NewValue.GetValue());
		}

		OnTextCommittedCallback.ExecuteIfBound(InText, InTextAction);
	}
}

void SChangelistEditableText::OnTextChanged(const FText& InText)
{
	const FString& Data = InText.ToString();

	// Use the longest substring that consists of only valid characters.
	// For example if someone enters:
	// "john.doe2/CL_123456789/version_13", we'll use "123456789" because it's longer than "2" and "13"
	int32 BestSubstrBegin = 0;
	int32 BestSubstrLen = 0;
	int32 I = 0;
	while (I < Data.Len())
	{
		// skip invalid characters
		while(I < Data.Len() && !IsCharacterValid(Data[I]))
		{
			++I;
		}

		// grab chunk of consecutive valid characters
		const int32 SubstrBegin = I;
		while(I < Data.Len() && IsCharacterValid(Data[I]))
		{
			++I;
		}
		
		// if found substr is longer than the best so far, update the best so far
		if (I - SubstrBegin > BestSubstrLen)
		{
			BestSubstrLen = I - SubstrBegin;
			BestSubstrBegin = SubstrBegin;
		}
	}
	
	const FString ValidData = Data.Mid(BestSubstrBegin, BestSubstrLen);
	const FText ValidText = FText::FromString(ValidData);
	EditableTextLayout->SetText(ValidText);
	OnTextChangedCallback.ExecuteIfBound(ValidText);
}

bool SChangelistEditableText::IsCharacterValid(TCHAR InChar)
{
	return TChar<TCHAR>::IsDigit(InChar);
}
