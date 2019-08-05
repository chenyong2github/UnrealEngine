// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "HTML5TargetSettingsCustomization.h"
#include "HTML5TargetSettings.h"
#include "PropertyHandle.h"
#include "IDetailPropertyRow.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "Misc/EngineBuildSettings.h"

#include "DetailWidgetRow.h"
//#include "Widgets/DeclarativeSyntaxSupport.h"
//#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
//#include "Widgets/SWidget.h"
#include "Widgets/Images/SImage.h"
//#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
//#include "Widgets/Layout/SBox.h"
//#include "Widgets/Layout/SWidgetSwitcher.h"
//#include "Widgets/Notifications/SNotificationList.h"
//#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "HTML5TargetSettings"
DEFINE_LOG_CATEGORY_STATIC(LogIOSTargetSettings, Log, All);

//////////////////////////////////////////////////////////////////////////
// FHTML5TargetSettingsCustomization
namespace FHTML5TargetSettingsCustomizationConstants
{
	const FText DisabledTip = LOCTEXT("GitHubSourceRequiredToolTip", "This requires GitHub source.");
}


//////////////////////////////////////////////////////////////////////////
class HTML5DeprecationBanner : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(HTML5DeprecationBanner)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		ChildSlot
		[
			SNew(SBorder)
			.BorderBackgroundColor(this, &HTML5DeprecationBanner::GetBorderColor)
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.LightGroupBorder"))
			.Padding(8.0f)
			[
				SNew(SHorizontalBox)
				//	.ToolTipText(Tooltip)

				// Status icon
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("SettingsEditor.WarningIcon"))
				]

				// Notice
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(16.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.ColorAndOpacity(FLinearColor::White)
					.ShadowColorAndOpacity(FLinearColor::Black)
					.ShadowOffset(FVector2D::UnitVector)
					.Text(LOCTEXT("HTML5DeprecatedPlatformMessageText",
						"The HTML5 platform will be migrated from:\n\tan officially supported (integrated) platform,\nand be moved to:\n\tas a community supported (plugin) platform."))
				]
			]
		];
	}

	FSlateColor GetBorderColor() const
	{
		return FLinearColor(0.8f, 0, 0);
	}
};

//////////////////////////////////////////////////////////////////////////


TSharedRef<IDetailCustomization> FHTML5TargetSettingsCustomization::MakeInstance()
{
	return MakeShareable(new FHTML5TargetSettingsCustomization);
}

FHTML5TargetSettingsCustomization::FHTML5TargetSettingsCustomization()
{
}

FHTML5TargetSettingsCustomization::~FHTML5TargetSettingsCustomization()
{
}

void FHTML5TargetSettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	BuildDeprecationMessage(DetailLayout); // !!! HTML5 Deprecation Message
	// --------------------------------------------------
	BuildEmscriptenCategory(DetailLayout); // !!! HTML5 Deprecation Message
	AudioPluginWidgetManager.BuildAudioCategory(DetailLayout, EAudioPlatform::HTML5);
}

void FHTML5TargetSettingsCustomization::BuildEmscriptenCategory(IDetailLayoutBuilder& DetailLayout)
{
	IDetailCategoryBuilder& EmscriptenCategory = DetailLayout.EditCategory(TEXT("Emscripten"));

#define SETUP_SOURCEONLY_PROP(PropName, Category) \
	{ \
		TSharedRef<IPropertyHandle> PropertyHandle = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UHTML5TargetSettings, PropName)); \
		Category.AddProperty(PropertyHandle) \
			.IsEnabled(FEngineBuildSettings::IsSourceDistribution()) \
			.ToolTip(FEngineBuildSettings::IsSourceDistribution() ? PropertyHandle->GetToolTipText() : FHTML5TargetSettingsCustomizationConstants::DisabledTip); \
	}

	SETUP_SOURCEONLY_PROP(EnableIndexedDB, EmscriptenCategory);

	SETUP_SOURCEONLY_PROP(EnableMultithreading, EmscriptenCategory);
}

void FHTML5TargetSettingsCustomization::BuildDeprecationMessage(IDetailLayoutBuilder& DetailLayout)
{
	IDetailCategoryBuilder& HTML5PlatformDeprecatedCategory = DetailLayout.EditCategory(TEXT("HTML5 as a UE4 Platform Note:"));
	TSharedRef<HTML5DeprecationBanner> PlatformSetupMessage = SNew(HTML5DeprecationBanner);
	HTML5PlatformDeprecatedCategory.AddCustomRow(LOCTEXT("Warning", "Warning"), false)
		.WholeRowWidget
		[
			PlatformSetupMessage
		];
}

//////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
