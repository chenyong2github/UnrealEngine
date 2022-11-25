// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRecentTracesList.h"

#include "EditorTraceUtilitiesStyle.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UnrealInsightsLauncher.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBorder.h"

#define LOCTEXT_NAMESPACE "RecentTracesList"

class SRecentTracesListEntry : public STableRow<TSharedPtr<FString>>
{
public:
	SLATE_BEGIN_ARGS(SRecentTracesListEntry) {}

	SLATE_ARGUMENT(TSharedPtr<FTraceFileInfo>, TraceInfo)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable)
	{
		TraceInfo = InArgs._TraceInfo;

		STableRow<TSharedPtr<FString>>::Construct(STableRow<TSharedPtr<FString>>::FArguments(), InOwnerTable);
	}

	virtual void ConstructChildren(ETableViewMode::Type InOwnerTableMode, const TAttribute<FMargin>& InPadding, const TSharedRef<SWidget>& InContent) override
	{
		FString TraceName = FPaths::GetBaseFilename(TraceInfo->FilePath);
		TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		int32 LastCharacter = FontMeasureService->FindLastWholeCharacterIndexBeforeOffset(TraceName, FAppStyle::GetFontStyle("Menu.Label"), 180);
		if (LastCharacter < TraceName.Len() - 1 && LastCharacter > 3)
		{
			TraceName.LeftInline(LastCharacter - 3);
			TraceName += TEXT("...");
		}

		FString TooltipText = TraceInfo->FilePath;
		FPaths::NormalizeFilename(TooltipText);
		TooltipText = FPaths::ConvertRelativePathToFull(TooltipText);

		const FSlateBrush* TraceLocationIcon;
		FText TraceLocationTooltip;

		if (TraceInfo->bIsFromTraceStore)
		{
			TraceLocationIcon = FEditorTraceUtilitiesStyle::Get().GetBrush("Icons.TraceStore.Menu");
			TraceLocationTooltip = LOCTEXT("TraceStoreLocationTooltip", "This trace is located in the trace store.");
		}
		else
		{
			TraceLocationIcon = FEditorTraceUtilitiesStyle::Get().GetBrush("Icons.File.Menu");
			TraceLocationTooltip = LOCTEXT("TraceStoreLocationTooltip", "This trace was saved to file and is located in the current project's profilling folder.");
		}

		ChildSlot
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Menu.Background"))
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.VAlign(EVerticalAlignment::VAlign_Center)
					.HAlign(EHorizontalAlignment::HAlign_Left)
					.Padding(8.0f, 0.0f, 0.0f, 0.0f)
					.AutoWidth()
					[
						SNew(SImage)
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.Image(TraceLocationIcon)
						.ToolTipText(TraceLocationTooltip)
					]

					+ SHorizontalBox::Slot()
					.VAlign(EVerticalAlignment::VAlign_Center)
					.HAlign(EHorizontalAlignment::HAlign_Left)
					.Padding(0.0f, 2.0f, 0.0f, 0.0f)
					.AutoWidth()
					[
						SNew(STextBlock)
						.TextStyle(FAppStyle::Get(), "Menu.Label")
						.Text(FText::FromString(TraceName))
						.ToolTipText(FText::FromString(TooltipText))
						.ColorAndOpacity(FSlateColor::UseForeground())
					]
					
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.HAlign(EHorizontalAlignment::HAlign_Right)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.HAlign(EHorizontalAlignment::HAlign_Right)
						.FillWidth(1.0f)
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "SimpleButton")
							.OnClicked(this, &SRecentTracesListEntry::OpenContainingFolder)
							.ToolTipText(LOCTEXT("ExploreFolderTooltip", "Open the folder containing the trace file."))
							.Content()
							[
								SNew(SImage)
								.ColorAndOpacity(FSlateColor::UseSubduedForeground())
								.Image(FAppStyle::Get().GetBrush("Icons.FolderOpen"))
							]
						]

						+ SHorizontalBox::Slot()
						.HAlign(EHorizontalAlignment::HAlign_Right)
						.AutoWidth()
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "SimpleButton")
							.OnClicked(this, &SRecentTracesListEntry::OpenTrace)
							.ToolTipText(LOCTEXT("OpenTraceTooltip", "Open the trace file in Unreal Insights."))
							.Content()
							[
								SNew(SImage)
								.ColorAndOpacity(FSlateColor::UseSubduedForeground())
								.Image(FEditorTraceUtilitiesStyle::Get().GetBrush("Icons.UnrealInsights.Menu"))
							]
						]
					]
				]
			];
	}

	FReply OpenContainingFolder()
	{
		FPlatformProcess::ExploreFolder(*TraceInfo->FilePath);

		return FReply::Handled();
	}

	FReply OpenTrace()
	{
		FUnrealInsightsLauncher::Get()->TryOpenTraceFromDestination(TraceInfo->FilePath);
		return FReply::Handled();
	}

	TSharedPtr<FTraceFileInfo> TraceInfo;
};

void SRecentTracesList::Construct(const FArguments& InArgs, const FString& InStorePath)
{
	StorePath = InStorePath;

	PopulateRecentTracesList();

	ListView = SNew(SListView<TSharedPtr<FTraceFileInfo>>)
		.IsFocusable(true)
		.SelectionMode(ESelectionMode::None)
		.ListItemsSource(&Traces)
		.OnGenerateRow(this, &SRecentTracesList::OnGenerateRow);
	
	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.Padding(FMargin(2.0f, 0.0f))
		.FillHeight(1.0f)
		[
			SNew(SScrollBorder, ListView.ToSharedRef())
			[
				ListView.ToSharedRef()
			]
		]
	];
}

TSharedRef<ITableRow> SRecentTracesList::OnGenerateRow(TSharedPtr<FTraceFileInfo> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SRecentTracesListEntry, OwnerTable)
		.TraceInfo(Item);
}

void SRecentTracesList::PopulateRecentTracesList()
{
	Traces.Empty();
	bool bIsFromTraceStore = true;
	auto Visitor = [this, &bIsFromTraceStore](const TCHAR* Filename, const FFileStatData& StatData)
	{
		if (FPaths::GetExtension(Filename) == TEXT("utrace"))
		{
			TSharedPtr<FTraceFileInfo> TraceInfo = MakeShared<FTraceFileInfo>();
			TraceInfo->FilePath = Filename;
			TraceInfo->ModifiedTime = StatData.ModificationTime;
			TraceInfo->bIsFromTraceStore = bIsFromTraceStore;

			Traces.Add(TraceInfo);
		}

		return true;
	};

	IFileManager::Get().IterateDirectoryStat(*StorePath, Visitor);
	
	bIsFromTraceStore = false;
	IFileManager::Get().IterateDirectoryStat(*FPaths::ProfilingDir(), Visitor);
	
	Algo::SortBy(Traces, [](TSharedPtr<FTraceFileInfo> TraceInfo) { return *TraceInfo; });
}

#undef LOCTEXT_NAMESPACE
