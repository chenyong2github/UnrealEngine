// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkSourceSettingsDetailCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyHandle.h"

#include "Widgets/Input/SCheckBox.h"

#define LOCTEXT_NAMESPACE "LiveLinkSourceSettingsDetailCustomization"


void FLiveLinkSourceSettingsDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& InDetailBuilder)
{
	DetailBuilder = &InDetailBuilder;
	
	TSharedPtr<IPropertyHandle> BufferSettingsPropertyHandle = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULiveLinkSourceSettings, BufferSettings));
	InDetailBuilder.HideProperty(BufferSettingsPropertyHandle);

	TSharedRef<IPropertyHandle> ModePropertyHandle = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULiveLinkSourceSettings, Mode));

	void* ModeValuePtr = nullptr;
	FPropertyAccess::Result ModeResult = ModePropertyHandle->GetValueData(ModeValuePtr);
	if (ModeResult == FPropertyAccess::MultipleValues || ModeResult == FPropertyAccess::Fail || ModeValuePtr == nullptr)
	{
		return;
	}
	const ELiveLinkSourceMode SourceMode = *reinterpret_cast<ELiveLinkSourceMode*>(ModeValuePtr);

	InDetailBuilder.AddPropertyToCategory(ModePropertyHandle);
	ModePropertyHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLiveLinkSourceSettingsDetailCustomization::ForceRefresh));

	IDetailCategoryBuilder& CategoryBuilder = InDetailBuilder.EditCategory("Buffer - Settings");

	CategoryBuilder.AddProperty(BufferSettingsPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLiveLinkSourceBufferManagementSettings, MaxNumberOfFrameToBuffered)));

	if (SourceMode == ELiveLinkSourceMode::Timecode)
	{
		CategoryBuilder.AddProperty(BufferSettingsPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLiveLinkSourceBufferManagementSettings, ValidTimecodeFrame)))
			.DisplayName(LOCTEXT("ValidTimecodeFrameDisplayName", "Valid Buffer"));
		CategoryBuilder.AddProperty(BufferSettingsPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLiveLinkSourceBufferManagementSettings, TimecodeFrameOffset)))
			.DisplayName(LOCTEXT("TimecodeFrameOffsetDisplayName", "Offset"));
		CategoryBuilder.AddProperty(BufferSettingsPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLiveLinkSourceBufferManagementSettings, TimecodeFrameRate)));

		CategoryBuilder.AddProperty(BufferSettingsPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLiveLinkSourceBufferManagementSettings, bKeepAtLeastOneFrame))
			, EPropertyLocation::Advanced);

		IDetailCategoryBuilder& SubFrameCategoryBuilder = InDetailBuilder.EditCategory("Sub Frame");
		SubFrameCategoryBuilder.AddProperty(BufferSettingsPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLiveLinkSourceBufferManagementSettings, bGenerateSubFrame)));
		SubFrameCategoryBuilder.AddProperty(BufferSettingsPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLiveLinkSourceBufferManagementSettings, SourceTimecodeFrameRate)));
	}
	else if (SourceMode == ELiveLinkSourceMode::EngineTime)
	{
		CategoryBuilder.AddProperty(BufferSettingsPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLiveLinkSourceBufferManagementSettings, ValidEngineTime)))
			.DisplayName(LOCTEXT("ValidEngineTimeDisplayName", "Valid Buffer"));
		CategoryBuilder.AddProperty(BufferSettingsPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLiveLinkSourceBufferManagementSettings, EngineTimeOffset)))
			.DisplayName(LOCTEXT("EngineTimeOffsetDisplayName", "Offset"));
		CategoryBuilder.AddProperty(BufferSettingsPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLiveLinkSourceBufferManagementSettings, bKeepAtLeastOneFrame))
			, EPropertyLocation::Advanced);
	}
	else if (SourceMode == ELiveLinkSourceMode::Latest)
	{
		CategoryBuilder.AddProperty(BufferSettingsPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLiveLinkSourceBufferManagementSettings, LatestOffset)))
			.DisplayName(LOCTEXT("LatestOffsetDisplayName", "Offset"));
	}
}

void FLiveLinkSourceSettingsDetailCustomization::ForceRefresh()
{
	if (DetailBuilder)
	{
		DetailBuilder->ForceRefreshDetails();
	}
}

#undef LOCTEXT_NAMESPACE
