// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SSlateOptions.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/IConsoleManager.h"
#include "SlateGlobals.h"
#include "SlateReflectorModule.h"
#include "Styling/CoreStyle.h"
#include "Styling/WidgetReflectorStyle.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SInvalidationPanel.h"
#include "Widgets/SWidgetReflector.h"

#define LOCTEXT_NAMESPACE "SSlateOptions"

void SSlateOptions::Construct( const FArguments& InArgs )
{
	struct Local
	{
		static void FillToolbar(FToolBarBuilder& ToolbarBuilder, const FSlateIcon& Icon, const FText& Label, const TCHAR* ConsoleVariable)
		{
			IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(ConsoleVariable);
			if (CVar)
			{
				FTextBuilder TooltipText;
				TooltipText.AppendLine(FString(CVar->GetHelp()));
				TooltipText.AppendLine(FString(ConsoleVariable));

				ToolbarBuilder.AddToolBarButton(
					FUIAction(
						FExecuteAction::CreateLambda([CVar]() { CVar->Set(!CVar->GetBool(), EConsoleVariableFlags::ECVF_SetByCode); }),
						FCanExecuteAction(),
						FGetActionCheckState::CreateLambda([CVar]() { return CVar->GetBool() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					),
					NAME_None,
					Label,
					TooltipText.ToText(),
					Icon,
					EUserInterfaceActionType::ToggleButton
				);
			}
		}

		static void FillToolbar(FToolBarBuilder& ToolbarBuilder)
		{
			FSlateIcon Icon(FWidgetReflectorStyle::GetStyleSetName(), "Icon.Empty");

			ToolbarBuilder.BeginSection("Flags");
			{
#if WITH_SLATE_DEBUGGING
				FillToolbar(ToolbarBuilder, Icon, LOCTEXT("EnableWidgetCaching", "Widget Caching"), TEXT("Slate.EnableInvalidationPanels"));
				FillToolbar(ToolbarBuilder, Icon, LOCTEXT("InvalidationDebugging", "Invalidation Debugging"), TEXT("SlateDebugger.Invalidate.Enable"));
				FillToolbar(ToolbarBuilder, Icon, LOCTEXT("InvalidationRootDebugging", "Invalidation Root Debugging"), TEXT("SlateDebugger.InvalidationRoot.Enable"));
				FillToolbar(ToolbarBuilder, Icon, LOCTEXT("UpdateDebugging", "Update Debugging"), TEXT("SlateDebugger.Update.Enable"));
				FillToolbar(ToolbarBuilder, Icon, LOCTEXT("PaintDebugging", "Paint Debugging"), TEXT("SlateDebugger.Paint.Enable"));
				FillToolbar(ToolbarBuilder, Icon, LOCTEXT("ShowClipping", "Show Clipping"), TEXT("Slate.ShowClipping"));
				FillToolbar(ToolbarBuilder, Icon, LOCTEXT("DebugCulling", "Debug Culling"), TEXT("Slate.DebugCulling"));
				FillToolbar(ToolbarBuilder, Icon, LOCTEXT("EnsureAllVisibleWidgetsPaint", "Ensure All Visible Widgets Paint"), TEXT("Slate.EnsureAllVisibleWidgetsPaint"));
#endif // WITH_SLATE_DEBUGGING
			}
			ToolbarBuilder.EndSection();
		}
	};

	FToolBarBuilder ToolbarBuilder(TSharedPtr<const FUICommandList>(), FMultiBoxCustomization::None);
	Local::FillToolbar(ToolbarBuilder);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(2.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 4.f, 0.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AppScale", "Application Scale: "))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.MinDesiredWidth(100.f)
				.MaxDesiredWidth(250.f)
				[
					SNew(SSpinBox<float>)
					.Value(this, &SSlateOptions::HandleAppScaleSliderValue)
					.MinValue(0.50f)
					.MaxValue(3.0f)
					.Delta(0.01f)
					.OnValueChanged(this, &SSlateOptions::HandleAppScaleSliderChanged)
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(20.f, 0.f, 4.f, 0.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Flags", "Flags: "))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1)
			.Padding(FMargin(5.0f, 0.0f))
			[
				ToolbarBuilder.MakeWidget()
			]
		]
	];
}

void SSlateOptions::HandleAppScaleSliderChanged(float NewValue)
{
	FSlateApplication::Get().SetApplicationScale(NewValue);
}

float SSlateOptions::HandleAppScaleSliderValue() const
{
	return FSlateApplication::Get().GetApplicationScale();
}

#undef LOCTEXT_NAMESPACE
