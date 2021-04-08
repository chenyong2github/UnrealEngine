// Copyright Epic Games, Inc. All Rights Reserved.

#include "CommonUIRichTextData.h"

#include "CommonUISettings.h"

UCommonUIRichTextData* UCommonUIRichTextData::Get()
{
	return ICommonUIModule::GetSettings().GetRichTextData();
}

const FRichTextIconData* UCommonUIRichTextData::FindIcon(const FName& InKey)
{
	FString ContextString = TEXT("UCommonUIRichTextData::FindIcon");
	return InlineIconSet->FindRow<FRichTextIconData>(InKey, ContextString);
}
