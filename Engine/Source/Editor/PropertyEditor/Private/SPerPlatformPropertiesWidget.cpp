// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPerPlatformPropertiesWidget.h"
#include "Layout/Margin.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/CoreStyle.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "PlatformInfo.h"
#include "Widgets/Layout/SWrapBox.h"
#include "EditorStyleSet.h"
#include "Widgets/Layout/SBox.h"
#include "DetailLayoutBuilder.h"
#include "Widgets/Images/SImage.h"


void SPerPlatformPropertiesWidget::Construct(const typename SPerPlatformPropertiesWidget::FArguments& InArgs)
{
	this->OnGenerateWidget = InArgs._OnGenerateWidget;
	this->OnAddPlatform = InArgs._OnAddPlatform;
	this->OnRemovePlatform = InArgs._OnRemovePlatform;
	this->PlatformOverrideNames = InArgs._PlatformOverrideNames;

	ConstructChildren();

	// this widget has to check platform count change from outside to ensure the widget represents latest update
	RegisterActiveTimer(FMath::RandRange(2.f, 5.f), FWidgetActiveTimerDelegate::CreateSP(this, &SPerPlatformPropertiesWidget::CheckPlatformCount));
}

void SPerPlatformPropertiesWidget::ConstructChildren()
{
	TSharedPtr<SWrapBox> WrapBox;

	TArray<FName> PlatformOverrides = PlatformOverrideNames.Get();
	LastPlatformOverrideNames = PlatformOverrides.Num();

	ChildSlot
	.VAlign(VAlign_Center)
	.HAlign(HAlign_Fill)
	[
		SAssignNew(WrapBox, SWrapBox)
		.UseAllottedSize(true)
	];

	if (OnGenerateWidget.IsBound())
	{
		// Build Platform menu
		bAddedMenuItem = false;
		FMenuBuilder AddPlatformMenuBuilder(true, nullptr, nullptr, true);

		// Platform (group) names
		const TArray<FName>& PlatformGroupNameArray = PlatformInfo::GetAllPlatformGroupNames();
		const TArray<FName>& VanillaPlatformNameArray = PlatformInfo::GetAllVanillaPlatformNames();

		// Sanitized platform names
		TArray<FName> BasePlatformNameArray;
		// Mapping from platform group name to individual platforms
		TMultiMap<FName, FName> GroupToPlatform;
						
		// Create mapping from platform to platform groups and remove postfixes and invalid platform names
		const TArray<FString> Filters = { TEXT("NoEditor"), TEXT("Client"), TEXT("Server"), TEXT("AllDesktop") };
		for (const FName& PlatformName : VanillaPlatformNameArray)
		{
			const PlatformInfo::FPlatformInfo* PlatformInfo = PlatformInfo::FindPlatformInfo(PlatformName);
			FString PlatformNameString = PlatformName.ToString();
			for ( const FString& Filter : Filters)
			{
				const int32 Position = PlatformNameString.Find(Filter);
				if (Position != INDEX_NONE)
				{
					PlatformNameString.RemoveAt(Position, Filter.Len());
					break;
				}
			}

			// Add filtered name if it isn't already set, and also add to group mapping
			const FName FilteredName = FName(*PlatformNameString);				
			if (PlatformNameString.Len() && !PlatformOverrides.Contains(FilteredName))
			{	
				BasePlatformNameArray.AddUnique(FilteredName);
				GroupToPlatform.AddUnique(PlatformInfo->PlatformGroupName, FilteredName);
			}
		}

		// Create section for platform groups 
		const FName PlatformGroupSection(TEXT("PlatformGroupSection"));
		AddPlatformMenuBuilder.BeginSection(PlatformGroupSection, FText::FromString(TEXT("Platform Groups")));
		for (const FName& GroupName : PlatformGroupNameArray)
		{				
			if (!PlatformOverrides.Contains(GroupName))
			{					
				const FTextFormat Format = NSLOCTEXT("SPerPlatformPropertiesWidget", "AddOverrideGroupFor", "Add Override for Platforms part of the {0} Platform Group");
				AddPlatformToMenu(GroupName, Format, AddPlatformMenuBuilder);
				bAddedMenuItem = true;
			}
		}				
		AddPlatformMenuBuilder.EndSection();
			
		for (const FName& GroupName : PlatformGroupNameArray)
		{
			// Create a section for each platform group and their respective platforms
			AddPlatformMenuBuilder.BeginSection(GroupName, FText::FromName(GroupName));

			TArray<FName> PlatformNames;
			GroupToPlatform.MultiFind(GroupName, PlatformNames);

			const FTextFormat Format = NSLOCTEXT("SPerPlatformPropertiesWidget", "AddOverrideFor", "Add Override specifically for {0}");
			for (const FName& PlatformName : PlatformNames)
			{
				AddPlatformToMenu(PlatformName, Format, AddPlatformMenuBuilder);
			}

			bAddedMenuItem |= PlatformNames.Num() > 0;

			AddPlatformMenuBuilder.EndSection();
		}

		// Default control
		WrapBox->AddSlot()
		[
			MakePerPlatformWidget(NAME_None, NSLOCTEXT("SPerPlatformPropertiesWidget", "DefaultPlatform", "Default"), PlatformOverrides, AddPlatformMenuBuilder)
		];

		for (FName PlatformName : PlatformOverrides)
		{
			WrapBox->AddSlot()
			[
				MakePerPlatformWidget(PlatformName, FText::AsCultureInvariant(PlatformName.ToString()), PlatformOverrides, AddPlatformMenuBuilder)
			];
		}
	}
	else
	{
		WrapBox->AddSlot()
		[
			SNew(STextBlock)
			.Text(NSLOCTEXT("SPerPlatformPropertiesWidget", "OnGenerateWidgetWarning", "No OnGenerateWidget() Provided"))
			.ColorAndOpacity(FLinearColor::Red)
		];
	}
}

void SPerPlatformPropertiesWidget::AddPlatformToMenu(const FName& PlatformName, const FTextFormat Format, FMenuBuilder &AddPlatformMenuBuilder)
{		
	const FText MenuText = FText::Format(FText::FromString(TEXT("{0}")), FText::AsCultureInvariant(PlatformName.ToString()));
	const FText MenuTooltipText = FText::Format(Format, FText::AsCultureInvariant(PlatformName.ToString()));
	AddPlatformMenuBuilder.AddMenuEntry(
		MenuText,
		MenuTooltipText,
		FSlateIcon(FEditorStyle::GetStyleSetName(), "PerPlatformWidget.AddPlatform"),
		FUIAction(FExecuteAction::CreateSP(this, &SPerPlatformPropertiesWidget::AddPlatform, PlatformName))
	);
}

void SPerPlatformPropertiesWidget::AddPlatform(FName PlatformName)
{
	if (OnAddPlatform.IsBound() && OnAddPlatform.Execute(PlatformName))
	{
		ConstructChildren();
		Invalidate(EInvalidateWidget::LayoutAndVolatility);
	}
}

FReply SPerPlatformPropertiesWidget::RemovePlatform(FName PlatformName)
{
	if (OnRemovePlatform.IsBound() && OnRemovePlatform.Execute(PlatformName))
	{
		ConstructChildren();
		Invalidate(EInvalidateWidget::LayoutAndVolatility);
	}
	return FReply::Handled();
}

EActiveTimerReturnType SPerPlatformPropertiesWidget::CheckPlatformCount(double InCurrentTime, float InDeltaSeconds)
{
	// @fixme: the platform count is fixed and locally cached
	// so if you change this outside of editor, this widget won't update
	// this timer is the one checking to see if platform count has changed
	// if so, it will reconstruct
	TArray<FName> PlatformOverrides = PlatformOverrideNames.Get();
	if (LastPlatformOverrideNames != PlatformOverrides.Num())
	{
		ConstructChildren();
	}

	return EActiveTimerReturnType::Continue;
}


TSharedRef<SWidget> SPerPlatformPropertiesWidget::MakePerPlatformWidget(FName InName, FText InDisplayText, const TArray<FName>& InPlatformOverrides, FMenuBuilder& InAddPlatformMenuBuilder)
{
	TSharedPtr<SHorizontalBox> HorizontalBox;

	TSharedRef<SWidget> Widget = 
		SNew(SBox)
		.ToolTipText(InName == NAME_None ? 
			NSLOCTEXT("SPerPlatformPropertiesWidget", "DefaultPlatformDesc", "This property can have per-platform or platform group overrides.\nThis is the default value used when no override has been set for a platform or platform group.") : 
			FText::Format(NSLOCTEXT("SPerPlatformPropertiesWidget", "PerPlatformDesc", "Override for {0}"), InDisplayText))
		.Padding(FMargin(0.0f, 2.0f, 4.0f, 2.0f))
		.MinDesiredWidth(50.0f)
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(HorizontalBox, SHorizontalBox)
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(0.0f, 0.0f, 2.0f, 2.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text(InDisplayText)
				]
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				OnGenerateWidget.Execute(InName)
			]
		];

	if(InName != NAME_None)
	{
		HorizontalBox->AddSlot()
			.AutoWidth()
			.Padding(2.0f, 0.0f, 0.0f, 2.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ContentPadding(2.0f)
				.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.OnClicked(this, &SPerPlatformPropertiesWidget::RemovePlatform, InName)
				.ToolTipText(FText::Format(NSLOCTEXT("SPerPlatformPropertiesWidget", "RemoveOverrideFor", "Remove Override for {0}"), InDisplayText))
				.ForegroundColor(FSlateColor::UseForeground())
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Content()
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("Icons.Cross"))
				]
			];
	}
	else
	{
		HorizontalBox->AddSlot()
			.AutoWidth()
			.Padding(2.0f, 0.0f, 0.0f, 2.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SComboButton)
				.Visibility_Lambda([this](){ return bAddedMenuItem ? EVisibility::Visible : EVisibility::Hidden; })
				.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.ContentPadding(2.0f)
				.ForegroundColor(FSlateColor::UseForeground())
				.HasDownArrow(false)
				.ButtonContent()
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("PropertyWindow.Button_AddToArray"))
				]
				.MenuContent()
				[
					InAddPlatformMenuBuilder.MakeWidget()
				]
				.ToolTipText(NSLOCTEXT("SPerPlatformPropertiesWidget", "AddOverrideToolTip", "Add an override for a specific platform or platform group"))	
			];
	}

	return Widget;
}