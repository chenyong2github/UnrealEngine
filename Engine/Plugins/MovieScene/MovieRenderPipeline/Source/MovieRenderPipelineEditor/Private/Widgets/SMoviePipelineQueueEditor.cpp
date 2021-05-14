// Copyright Epic Games, Inc. All Rights Reserved.

// Movie Pipeline
#include "Widgets/SMoviePipelineQueueEditor.h"
#include "Widgets/MoviePipelineWidgetConstants.h"
#include "MovieRenderPipelineDataTypes.h"
#include "MoviePipelineQueue.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelineQueueSubsystem.h"
#include "MoviePipelineMasterConfig.h"
#include "MovieRenderPipelineStyle.h"
#include "MovieRenderPipelineSettings.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "MoviePipelineCommands.h"
#include "MoviePipelineEditorBlueprintLibrary.h"

// Slate Includes
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Views/SExpanderArrow.h"
#include "Widgets/Input/SHyperlink.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EditorStyleSet.h"
#include "EditorFontGlyphs.h"
#include "SDropTarget.h"

// Editor
#include "Editor.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "ScopedTransaction.h"
#include "PropertyCustomizationHelpers.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Commands/GenericCommands.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "AssetData.h"

// Misc
#include "LevelSequence.h"
#include "Engine/EngineTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "SMoviePipelineConfigPanel.h"
#include "Widgets/SWindow.h"
#include "HAL/FileManager.h"
#include "Widgets/Layout/SBox.h"


#define LOCTEXT_NAMESPACE "SMoviePipelineQueueEditor"

struct FMoviePipelineQueueJobTreeItem;
struct FMoviePipelineMapTreeItem;
struct FMoviePipelineShotItem;
class SQueueJobListRow;

struct IMoviePipelineQueueTreeItem : TSharedFromThis<IMoviePipelineQueueTreeItem>
{
	virtual ~IMoviePipelineQueueTreeItem() {}

	virtual TSharedPtr<FMoviePipelineQueueJobTreeItem> AsJob() { return nullptr; }
	virtual UMoviePipelineExecutorJob* GetOwningJob() { return nullptr; }
	virtual void Delete(UMoviePipelineQueue* InOwningQueue) {}
	virtual void ResetStatus() {}
	virtual UMoviePipelineExecutorJob* Duplicate(UMoviePipelineQueue* InOwningQueue) { return nullptr; }

	virtual TSharedRef<ITableRow> ConstructWidget(TWeakPtr<SMoviePipelineQueueEditor> InQueueWidget, const TSharedRef<STableViewBase>& OwnerTable) = 0;
};

class SQueueJobListRow : public SMultiColumnTableRow<TSharedPtr<IMoviePipelineQueueTreeItem>>
{
public:
	SLATE_BEGIN_ARGS(SQueueJobListRow) {}
		SLATE_ARGUMENT(TSharedPtr<FMoviePipelineQueueJobTreeItem>, Item)
		SLATE_EVENT(FOnMoviePipelineEditConfig, OnEditConfigRequested)
	SLATE_END_ARGS()

	static const FName NAME_JobName;
	static const FName NAME_Settings;
	static const FName NAME_Output;
	static const FName NAME_Status;

	TSharedPtr<FMoviePipelineQueueJobTreeItem> Item;

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable);
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;

private:
	FOnMoviePipelineEditConfig OnEditConfigRequested;
};

struct FMoviePipelineQueueJobTreeItem : IMoviePipelineQueueTreeItem
{
	/** The job that this tree item represents */
	TWeakObjectPtr<UMoviePipelineExecutorJob> WeakJob;

	/** Sorted list of this category's children */
	TArray<TSharedPtr<IMoviePipelineQueueTreeItem>> Children;

	FOnMoviePipelineEditConfig OnEditConfigCallback;
	FOnMoviePipelineEditConfig OnChosePresetCallback;

	explicit FMoviePipelineQueueJobTreeItem(UMoviePipelineExecutorJob* InJob, FOnMoviePipelineEditConfig InOnEditConfigCallback, FOnMoviePipelineEditConfig InOnChosePresetCallback)
		: WeakJob(InJob)
		, OnEditConfigCallback(InOnEditConfigCallback)
		, OnChosePresetCallback(InOnChosePresetCallback)
	{}

	virtual TSharedRef<ITableRow> ConstructWidget(TWeakPtr<SMoviePipelineQueueEditor> InQueueWidget, const TSharedRef<STableViewBase>& OwnerTable) override
	{
		return SNew(SQueueJobListRow, OwnerTable)
			.Item(SharedThis(this));
	}

	virtual TSharedPtr<FMoviePipelineQueueJobTreeItem> AsJob() override
	{
		return SharedThis(this);
	}

	virtual UMoviePipelineExecutorJob* GetOwningJob() override
	{ 
		return WeakJob.Get();
	}

	virtual void Delete(UMoviePipelineQueue* InOwningQueue) override
	{
		InOwningQueue->DeleteJob(WeakJob.Get());
	}

	virtual UMoviePipelineExecutorJob* Duplicate(UMoviePipelineQueue* InOwningQueue) override
	{
		return InOwningQueue->DuplicateJob(WeakJob.Get());
	}

	virtual void ResetStatus() override
	{
		if (WeakJob.Get())
		{
			WeakJob->SetConsumed(false);
		}
	}

public:
	FText GetJobName() const
	{
		UMoviePipelineExecutorJob* Job = WeakJob.Get();
		if (Job)
		{
			return FText::FromString(Job->JobName);
		}

		return FText();
	}

	FText GetMasterConfigLabel() const
	{
		UMoviePipelineExecutorJob* Job = WeakJob.Get();
		if (Job)
		{
			UMoviePipelineConfigBase* Config = Job->GetPresetOrigin();
			if (!Config)
			{
				Config = Job->GetConfiguration();
			}

			if (Config)
			{
				return FText::FromString(Config->DisplayName);
			}
		}

		return FText();
	}

	void OnPickPresetFromAsset(const FAssetData& AssetData)
	{
		// Close the dropdown menu that showed them the assets to pick from.
		FSlateApplication::Get().DismissAllMenus();

		UMoviePipelineExecutorJob* Job = WeakJob.Get();
		if (Job)
		{
			Job->SetPresetOrigin(CastChecked<UMoviePipelineMasterConfig>(AssetData.GetAsset()));
		}

		OnChosePresetCallback.ExecuteIfBound(WeakJob, nullptr);
	}

	void OnPickNewPreset()
	{
		// Close the dropdown menu that showed them the assets to pick from.
		FSlateApplication::Get().DismissAllMenus();

		UMoviePipelineExecutorJob* Job = WeakJob.Get();
		if (Job)
		{
			// Copy from the CDO's version of the job to pick up the right name.
			Job->SetConfiguration(GetMutableDefault<UMoviePipelineExecutorJob>()->GetConfiguration());
			UMoviePipelineEditorBlueprintLibrary::EnsureJobHasDefaultSettings(Job);
		}

		OnChosePresetCallback.ExecuteIfBound(WeakJob, nullptr);
	}

	EVisibility GetMasterConfigModifiedVisibility() const
	{
		UMoviePipelineExecutorJob* Job = WeakJob.Get();
		if (Job)
		{
			return (Job->GetPresetOrigin() == nullptr) ? EVisibility::Visible : EVisibility::Collapsed;
		}
		
		return EVisibility::Collapsed;
	}

	void OnEditMasterConfigForJob()
	{
		OnEditConfigCallback.ExecuteIfBound(WeakJob, nullptr);
	}

	FText GetOutputLabel() const
	{
		UMoviePipelineExecutorJob* Job = WeakJob.Get();
		if (Job && Job->GetConfiguration())
		{
			UMoviePipelineOutputSetting* OutputSetting = Job->GetConfiguration()->FindSetting<UMoviePipelineOutputSetting>();
			check(OutputSetting);

			return FText::FromString(OutputSetting->OutputDirectory.Path);
		}

		return LOCTEXT("MissingConfigOutput_Label", "[No Config Set]");
	}

	void BrowseToOutputFolder()
	{
		UMoviePipelineExecutorJob* Job = WeakJob.Get();
		if (Job && Job->GetConfiguration())
		{
			UMoviePipelineOutputSetting* OutputSetting = Job->GetConfiguration()->FindSetting<UMoviePipelineOutputSetting>();
			check(OutputSetting);

			// @ToDo: We should resolve the exact path (as much as we can) through the config.
			// For now, we'll just split off any format strings and go to the base folder.
			FString OutputFolderPath = FPaths::ConvertRelativePathToFull(OutputSetting->OutputDirectory.Path);

			FString TrimmedPath;
			if (OutputFolderPath.Split(TEXT("{"), &TrimmedPath, nullptr))
			{
				FPaths::NormalizeDirectoryName(TrimmedPath);
				OutputFolderPath = TrimmedPath;
			}

			// Attempt to make the directory. The user can see the output folder before they render so the folder
			// may not have been created yet and the ExploreFolder call will fail.
			IFileManager::Get().MakeDirectory(*OutputFolderPath, true);

			FPlatformProcess::ExploreFolder(*OutputFolderPath);
		}
	}

	int32 GetStatusIndex() const
	{
		UMoviePipelineExecutorJob* Job = WeakJob.Get();
		if (Job)
		{
			// If the progress is zero we want to show the status message instead.
			return Job->GetStatusProgress() > 0;
		}

		return 0;
	}

	bool IsEnabled() const
	{
		UMoviePipelineExecutorJob* Job = WeakJob.Get();
		if (Job)
		{
			return !Job->IsConsumed();
		}
		return false;
	}

	TOptional<float> GetProgressPercent() const
	{
		UMoviePipelineExecutorJob* Job = WeakJob.Get();
		return Job ? Job->GetStatusProgress() : TOptional<float>();
	}

	FText GetStatusMessage() const
	{
		UMoviePipelineExecutorJob* Job = WeakJob.Get();
		return Job ? FText::FromString(Job->GetStatusMessage()) : FText();
	}

	TSharedRef<SWidget> OnGenerateConfigPresetPickerMenu()
	{
		return OnGenerateConfigPresetPickerMenuFromClass(UMoviePipelineMasterConfig::StaticClass(),
			FOnAssetSelected::CreateRaw(this, &FMoviePipelineQueueJobTreeItem::OnPickPresetFromAsset),
			FExecuteAction::CreateRaw(this, &FMoviePipelineQueueJobTreeItem::OnPickNewPreset)
			);
	}

	static TSharedRef<SWidget> OnGenerateConfigPresetPickerMenuFromClass(TSubclassOf<UMoviePipelineConfigBase> InClass, FOnAssetSelected InOnAssetSelected, FExecuteAction InNewConfig)
	{
		FMenuBuilder MenuBuilder(true, nullptr);
		IContentBrowserSingleton& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();

		FAssetPickerConfig AssetPickerConfig;
		{
			AssetPickerConfig.SelectionMode = ESelectionMode::Single;
			AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
			AssetPickerConfig.bFocusSearchBoxWhenOpened = true;
			AssetPickerConfig.bAllowNullSelection = false;
			AssetPickerConfig.bShowBottomToolbar = true;
			AssetPickerConfig.bAutohideSearchBar = false;
			AssetPickerConfig.bAllowDragging = false;
			AssetPickerConfig.bCanShowClasses = false;
			AssetPickerConfig.bShowPathInColumnView = true;
			AssetPickerConfig.bShowTypeInColumnView = false;
			AssetPickerConfig.bSortByPathInColumnView = false;
			AssetPickerConfig.ThumbnailScale = 0.1f;
			AssetPickerConfig.SaveSettingsName = TEXT("MoviePipelineConfigAsset");

			AssetPickerConfig.AssetShowWarningText = LOCTEXT("NoConfigs_Warning", "No Master Configurations Found");
			AssetPickerConfig.Filter.ClassNames.Add(InClass->GetFName());
			AssetPickerConfig.OnAssetSelected = InOnAssetSelected;
		}

		MenuBuilder.BeginSection(NAME_None, LOCTEXT("NewConfig_MenuSection", "New Configuration"));
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("NewConfig_Label", "Clear Config"),
				LOCTEXT("NewConfig_Tooltip", "Resets the changes to the config and goes back to the defaults."),
				FSlateIcon(),
				FUIAction(InNewConfig),
				NAME_None,
				EUserInterfaceActionType::Button
			);
		}

		MenuBuilder.BeginSection(NAME_None, LOCTEXT("ImportConfig_MenuSection", "Import Configuration"));
		{
			TSharedRef<SWidget> PresetPicker = SNew(SBox)
				.WidthOverride(300.f)
				.HeightOverride(300.f)
				[
					ContentBrowser.CreateAssetPicker(AssetPickerConfig)
				];

			MenuBuilder.AddWidget(PresetPicker, FText(), true, false);
		}
		MenuBuilder.EndSection();

		return MenuBuilder.MakeWidget();
	}
};

void SQueueJobListRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
{
	Item = InArgs._Item;
	OnEditConfigRequested = InArgs._OnEditConfigRequested;

	FSuperRowType::FArguments SuperArgs = FSuperRowType::FArguments();
	FSuperRowType::Construct(SuperArgs, OwnerTable);
}

TSharedRef<SWidget> SQueueJobListRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	if (ColumnName == NAME_JobName)
	{
		return SNew(SBox)
			.Padding(2.0f)
			.IsEnabled(Item.Get(), &FMoviePipelineQueueJobTreeItem::IsEnabled)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 6, 0)
				[
					SNew(SExpanderArrow, SharedThis(this))
				]

				+SHorizontalBox::Slot()
				.FillWidth(1.f)
				[
					SNew(STextBlock)
					.Text(Item.Get(), &FMoviePipelineQueueJobTreeItem::GetJobName)
				]
			];
	}
	else if (ColumnName == NAME_Settings)
	{
		return SNew(SHorizontalBox)
		.IsEnabled(Item.Get(), &FMoviePipelineQueueJobTreeItem::IsEnabled)

		// Preset Label
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(2, 0)
		[
			SNew(SHyperlink)
			.Text(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(Item.Get(), &FMoviePipelineQueueJobTreeItem::GetMasterConfigLabel)))
			.OnNavigate(Item.Get(), &FMoviePipelineQueueJobTreeItem::OnEditMasterConfigForJob)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ModifiedConfigIndicator", "*"))
			.Visibility(Item.Get(), &FMoviePipelineQueueJobTreeItem::GetMasterConfigModifiedVisibility)
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		[
			SNullWidget::NullWidget
		]

		// Dropdown Arrow
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Right)
		.Padding(4,0,4,0)
		[
			SNew(SComboButton)
			.ContentPadding(1)
			.OnGetMenuContent(Item.Get(), &FMoviePipelineQueueJobTreeItem::OnGenerateConfigPresetPickerMenu)
			.HasDownArrow(false)
			.ButtonContent()
			[
				SNew(SBox)
				.Padding(FMargin(2, 0))
				[
					SNew(STextBlock)
					.TextStyle(FEditorStyle::Get(), "NormalText.Important")
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
					.Text(FEditorFontGlyphs::Caret_Down)
				]
			]
		];
	}
	else if (ColumnName == NAME_Output)
	{
		return SNew(SBox)
			.IsEnabled(Item.Get(), &FMoviePipelineQueueJobTreeItem::IsEnabled)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			[
				SNew(SHyperlink)
					.Text(Item.Get(), &FMoviePipelineQueueJobTreeItem::GetOutputLabel)
					.OnNavigate(Item.Get(), &FMoviePipelineQueueJobTreeItem::BrowseToOutputFolder)
			];

	}
	else if(ColumnName == NAME_Status)
	{
		return SNew(SWidgetSwitcher)
			.WidgetIndex(Item.Get(), &FMoviePipelineQueueJobTreeItem::GetStatusIndex)
			.IsEnabled(Item.Get(), &FMoviePipelineQueueJobTreeItem::IsEnabled)

			// Status Message Label
			+ SWidgetSwitcher::Slot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(Item.Get(), &FMoviePipelineQueueJobTreeItem::GetStatusMessage)
			]

			// Progress Bar
		+ SWidgetSwitcher::Slot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Fill)
			[
				SNew(SProgressBar)
				.Percent(Item.Get(), &FMoviePipelineQueueJobTreeItem::GetProgressPercent)
			];

	}

	return SNullWidget::NullWidget;
}

const FName SQueueJobListRow::NAME_JobName = FName(TEXT("Job Name"));
const FName SQueueJobListRow::NAME_Settings = FName(TEXT("Settings"));
const FName SQueueJobListRow::NAME_Output = FName(TEXT("Output"));
const FName SQueueJobListRow::NAME_Status = FName(TEXT("Status"));

class SQueueShotListRow : public SMultiColumnTableRow<TSharedPtr<IMoviePipelineQueueTreeItem>>
{
public:
	SLATE_BEGIN_ARGS(SQueueShotListRow) {}
	SLATE_ARGUMENT(TSharedPtr<FMoviePipelineShotItem>, Item)
		SLATE_EVENT(FOnMoviePipelineEditConfig, OnEditConfigRequested)
		SLATE_END_ARGS()

	TSharedPtr<FMoviePipelineShotItem> Item;

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable);
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;

private:
	FOnMoviePipelineEditConfig OnEditConfigRequested;
};

struct FMoviePipelineShotItem : IMoviePipelineQueueTreeItem
{
	/** The job that this tree item represents */
	TWeakObjectPtr<UMoviePipelineExecutorJob> WeakJob;

	/** The identifier in the job for which shot this is. */
	TWeakObjectPtr<UMoviePipelineExecutorShot> WeakShot;

	TWeakPtr<SMoviePipelineQueueEditor> WeakQueueEditor;

	FOnMoviePipelineEditConfig OnEditConfigCallback;
	FOnMoviePipelineEditConfig OnChosePresetCallback;

	explicit FMoviePipelineShotItem(UMoviePipelineExecutorJob* InJob, UMoviePipelineExecutorShot* InShot, FOnMoviePipelineEditConfig InOnEditConfigCallback, FOnMoviePipelineEditConfig InOnChosePresetCallback)
		: WeakJob(InJob)
		, WeakShot(InShot)
		, OnEditConfigCallback(InOnEditConfigCallback)
		, OnChosePresetCallback(InOnChosePresetCallback)
	{}

	virtual UMoviePipelineExecutorJob* GetOwningJob() override
	{
		return WeakJob.Get();
	}

	virtual TSharedRef<ITableRow> ConstructWidget(TWeakPtr<SMoviePipelineQueueEditor> InQueueWidget, const TSharedRef<STableViewBase>& OwnerTable) override
	{
		WeakQueueEditor = InQueueWidget;

		return SNew(SQueueShotListRow, OwnerTable)
			.Item(SharedThis(this));
	}

	ECheckBoxState GetCheckState() const
	{
		if (UMoviePipelineExecutorShot* Shot = WeakShot.Get())
		{
			return Shot->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		}
		
		return ECheckBoxState::Unchecked;
	}

	void SetCheckState(ECheckBoxState InNewState)
	{
		if (WeakQueueEditor.IsValid() && WeakQueueEditor.Pin()->GetSelectedItems().Contains(SharedThis(this)))
		{
			for (TSharedPtr<IMoviePipelineQueueTreeItem> Item : WeakQueueEditor.Pin()->GetSelectedItems())
			{
				TSharedPtr<FMoviePipelineShotItem> ShotItem = StaticCastSharedPtr<FMoviePipelineShotItem>(Item);
				if (ShotItem.IsValid())
				{
					if (UMoviePipelineExecutorShot* Shot = ShotItem->WeakShot.Get())
					{
						Shot->bEnabled = InNewState == ECheckBoxState::Checked;
					}
				}
			}
		}
		else if (UMoviePipelineExecutorShot* Shot = WeakShot.Get())
		{
			Shot->bEnabled = InNewState == ECheckBoxState::Checked;
		}
	}

	FText GetShotLabel() const
	{
		UMoviePipelineExecutorShot* Shot = WeakShot.Get();
		if (Shot)
		{
			FString FormattedTitle = FString::Printf(TEXT("%s %s"), *Shot->OuterName, *Shot->InnerName);
			return FText::FromString(FormattedTitle);
		}
		return FText();
	}

	FText GetPresetLabel() const
	{
		return FText();
	}

	int32 GetStatusIndex() const
	{
		UMoviePipelineExecutorShot* Shot = WeakShot.Get();
		if (Shot)
		{
			// If the progress is zero we want to show the status message instead.
			return Shot->GetStatusProgress() > 0;
		}

		return 0;
	}

	bool IsEnabled() const
	{
		UMoviePipelineExecutorJob* Job = WeakJob.Get();
		if (Job)
		{
			return !Job->IsConsumed();
		}
		return false;
	}

	TOptional<float> GetProgressPercent() const
	{
		UMoviePipelineExecutorShot* Shot = WeakShot.Get();
		return Shot ? Shot->GetStatusProgress() : TOptional<float>();
	}

	FText GetStatusMessage() const
	{
		UMoviePipelineExecutorShot* Shot = WeakShot.Get();
		return Shot ? FText::FromString(Shot->GetStatusMessage()) : FText();
	}

	FText GetShotConfigLabel() const
	{
		UMoviePipelineExecutorShot* Shot = WeakShot.Get();
		if (Shot)
		{
			UMoviePipelineShotConfig* Config = Shot->GetShotOverridePresetOrigin();
			if (!Config)
			{
				Config = Shot->GetShotOverrideConfiguration();
			}

			if (Config)
			{
				return FText::FromString(Config->DisplayName);
			}
		}

		return FText::FromString(TEXT("Edit"));
	}

	void OnPickShotPresetFromAsset(const FAssetData& AssetData)
	{
		// Close the dropdown menu that showed them the assets to pick from.
		FSlateApplication::Get().DismissAllMenus();

		UMoviePipelineExecutorShot* Shot = WeakShot.Get();
		if (Shot)
		{
			Shot->SetShotOverridePresetOrigin(CastChecked<UMoviePipelineShotConfig>(AssetData.GetAsset()));
		}

		OnChosePresetCallback.ExecuteIfBound(WeakJob, WeakShot);
	}

	void OnPickNewShotPreset()
	{
		// Close the dropdown menu that showed them the assets to pick from.
		FSlateApplication::Get().DismissAllMenus();

		UMoviePipelineExecutorShot* Shot = WeakShot.Get();
		if (Shot)
		{
			Shot->SetShotOverrideConfiguration(nullptr);
		}

		OnChosePresetCallback.ExecuteIfBound(WeakJob, WeakShot);
	}

	EVisibility GetShotConfigModifiedVisibility() const
	{
		UMoviePipelineExecutorShot* Shot = WeakShot.Get();
		if (Shot && Shot->GetShotOverrideConfiguration() && (Shot->GetShotOverridePresetOrigin() == nullptr))
		{
			return EVisibility::Visible;
		}

		return EVisibility::Collapsed;
	}

	void OnEditConfigForShot()
	{
		OnEditConfigCallback.ExecuteIfBound(WeakJob, WeakShot);
	}

	TSharedRef<SWidget> OnGenerateShotConfigPresetPickerMenu()
	{
		return FMoviePipelineQueueJobTreeItem::OnGenerateConfigPresetPickerMenuFromClass(UMoviePipelineShotConfig::StaticClass(),
			FOnAssetSelected::CreateRaw(this, &FMoviePipelineShotItem::OnPickShotPresetFromAsset),
			FExecuteAction::CreateRaw(this, &FMoviePipelineShotItem::OnPickNewShotPreset)
			);
	}
};

void SQueueShotListRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
{
	Item = InArgs._Item;
	OnEditConfigRequested = InArgs._OnEditConfigRequested;

	FSuperRowType::FArguments SuperArgs = FSuperRowType::FArguments();
	FSuperRowType::Construct(SuperArgs, OwnerTable);
}

TSharedRef<SWidget> SQueueShotListRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	if (ColumnName == SQueueJobListRow::NAME_JobName)
	{
		return SNew(SBox)
		.Padding(2.0f)
		[
			SNew(SHorizontalBox)

			// Toggle Checkbox for deciding to render or not.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4, 4)
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.Style(FMovieRenderPipelineStyle::Get(), "MovieRenderPipeline.Setting.Switch")
				.IsFocusable(false)
				.IsChecked(Item.Get(), &FMoviePipelineShotItem::GetCheckState)
				.OnCheckStateChanged(Item.Get(), &FMoviePipelineShotItem::SetCheckState)
			]

			// Shot Name Label
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Item.Get(), &FMoviePipelineShotItem::GetShotLabel)
			]
		];
	}
	else if (ColumnName == SQueueJobListRow::NAME_Settings)
	{
		return SNew(SHorizontalBox)
		.IsEnabled(Item.Get(), &FMoviePipelineShotItem::IsEnabled)

		// Preset Label
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(2, 0)
		[
			SNew(SHyperlink)
			.Text(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(Item.Get(), &FMoviePipelineShotItem::GetShotConfigLabel)))
			.OnNavigate(Item.Get(), &FMoviePipelineShotItem::OnEditConfigForShot)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ModifiedShotConfigIndicator", "*"))
			.Visibility(Item.Get(), &FMoviePipelineShotItem::GetShotConfigModifiedVisibility)
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		[
			SNullWidget::NullWidget
		]

		// Dropdown Arrow
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Right)
		.Padding(4, 0, 4, 0)
		[
			SNew(SComboButton)
			.ContentPadding(1)
			.OnGetMenuContent(Item.Get(), &FMoviePipelineShotItem::OnGenerateShotConfigPresetPickerMenu)
			.HasDownArrow(false)
			.ButtonContent()
			[
				SNew(SBox)
				.Padding(FMargin(2, 0))
				[
					SNew(STextBlock)
					.TextStyle(FEditorStyle::Get(), "NormalText.Important")
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
					.Text(FEditorFontGlyphs::Caret_Down)
				]
			]
		];
	}
	else if (ColumnName == SQueueJobListRow::NAME_Output)
	{
		return SNullWidget::NullWidget;
	}
	else if (ColumnName == SQueueJobListRow::NAME_Status)
	{
		return SNew(SWidgetSwitcher)
			.WidgetIndex(Item.Get(), &FMoviePipelineShotItem::GetStatusIndex)
			
			// Ready Label
			+ SWidgetSwitcher::Slot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Fill)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PendingJobStatusReady_Label", "Ready"))
			]

			// Progress Bar
			+ SWidgetSwitcher::Slot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Fill)
			[
				SNew(SProgressBar)
				.Percent(Item.Get(), &FMoviePipelineShotItem::GetProgressPercent)
			]

			// Completed
			+ SWidgetSwitcher::Slot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Fill)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PendingJobStatusCompleted_Label", "Completed!"))
			];
	}

	return SNullWidget::NullWidget;
}


PRAGMA_DISABLE_OPTIMIZATION
void SMoviePipelineQueueEditor::Construct(const FArguments& InArgs)
{
	CachedQueueSerialNumber = uint32(-1);
	OnEditConfigRequested = InArgs._OnEditConfigRequested;
	OnPresetChosen = InArgs._OnPresetChosen;
	OnJobSelectionChanged = InArgs._OnJobSelectionChanged;

	TreeView = SNew(STreeView<TSharedPtr<IMoviePipelineQueueTreeItem>>)
		.TreeItemsSource(&RootNodes)
		.OnSelectionChanged(this, &SMoviePipelineQueueEditor::OnJobSelectionChanged_Impl)
		.OnGenerateRow(this, &SMoviePipelineQueueEditor::OnGenerateRow)
		.OnGetChildren(this, &SMoviePipelineQueueEditor::OnGetChildren)
		.OnContextMenuOpening(this, &SMoviePipelineQueueEditor::GetContextMenuContent)
		.HeaderRow
		(
			SNew(SHeaderRow)

			+ SHeaderRow::Column(SQueueJobListRow::NAME_JobName)
			.FillWidth(0.25f)
			.DefaultLabel(LOCTEXT("QueueHeaderJobName_Text", "Job"))

			+ SHeaderRow::Column(SQueueJobListRow::NAME_Settings)
			.FillWidth(0.25f)
			.DefaultLabel(LOCTEXT("QueueHeaderSettings_Text", "Settings"))

			+ SHeaderRow::Column(SQueueJobListRow::NAME_Output)
			.FillWidth(0.5f)
			.DefaultLabel(LOCTEXT("QueueHeaderOutput_Text", "Output"))

			+ SHeaderRow::Column(SQueueJobListRow::NAME_Status)
			.FixedWidth(80)
			.DefaultLabel(LOCTEXT("QueueHeaderStatus_Text", "Status"))
		);

	CommandList = MakeShared<FUICommandList>();
	CommandList->MapAction(
		FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &SMoviePipelineQueueEditor::OnDeleteSelected),
		FCanExecuteAction::CreateSP(this, &SMoviePipelineQueueEditor::CanDeleteSelected)
	);

	CommandList->MapAction(
		FGenericCommands::Get().Duplicate,
		FExecuteAction::CreateSP(this, &SMoviePipelineQueueEditor::OnDuplicateSelected),
		FCanExecuteAction::CreateSP(this, &SMoviePipelineQueueEditor::CanDuplicateSelected)
	);

	CommandList->MapAction(
		FMoviePipelineCommands::Get().ResetStatus,
		FExecuteAction::CreateSP(this, &SMoviePipelineQueueEditor::OnResetStatus)
	);

	ChildSlot
	[
		SNew(SDropTarget)
		.OnDrop(this, &SMoviePipelineQueueEditor::OnDragDropTarget)
		.OnAllowDrop(this, &SMoviePipelineQueueEditor::CanDragDropTarget)
		.OnIsRecognized(this, &SMoviePipelineQueueEditor::CanDragDropTarget)
		[
			TreeView.ToSharedRef()
		]
	];
}


TSharedPtr<SWidget> SMoviePipelineQueueEditor::GetContextMenuContent()
{
	FMenuBuilder MenuBuilder(true, CommandList);
	MenuBuilder.BeginSection("Edit");
	MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete);
	MenuBuilder.AddMenuEntry(FGenericCommands::Get().Duplicate);
	MenuBuilder.AddMenuEntry(FMoviePipelineCommands::Get().ResetStatus);
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SMoviePipelineQueueEditor::MakeAddSequenceJobButton()
{
	return SNew(SComboButton)
		.ContentPadding(MoviePipeline::ButtonPadding)
		.ButtonStyle(FMovieRenderPipelineStyle::Get(), "FlatButton.Success")
		.OnGetMenuContent(this, &SMoviePipelineQueueEditor::OnGenerateNewJobFromAssetMenu)
		.ForegroundColor(FSlateColor::UseForeground())
		.HasDownArrow(false)
		.ButtonContent()
		[
			SNew(SHorizontalBox)

			// Plus Icon
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.TextStyle(FEditorStyle::Get(), "NormalText.Important")
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
				.Text(FEditorFontGlyphs::Plus)
			]

			// "Render" Text
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(4, 0, 0, 0)
			[
				SNew(STextBlock)
				.TextStyle(FEditorStyle::Get(), "NormalText.Important")
				.Text(LOCTEXT("AddNewJob_Text", "Render"))
			]

			// Non-Default Down Caret arrow.
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(4, 0, 0, 0)
			[
				SNew(STextBlock)
				.TextStyle(FEditorStyle::Get(), "NormalText.Important")
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
				.Text(FEditorFontGlyphs::Caret_Down)
			]
		];
}

TSharedRef<SWidget> SMoviePipelineQueueEditor::RemoveSelectedJobButton()
{
	return SNew(SButton)
		.ContentPadding(MoviePipeline::ButtonPadding)
		.IsEnabled(this, &SMoviePipelineQueueEditor::CanDeleteSelected)
		.OnClicked(this, &SMoviePipelineQueueEditor::DeleteSelected)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.TextStyle(FEditorStyle::Get(), "NormalText.Important")
			.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
			.Text(FEditorFontGlyphs::Minus)
		];
}

TSharedRef<SWidget> SMoviePipelineQueueEditor::OnGenerateNewJobFromAssetMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	IContentBrowserSingleton& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();

	FAssetPickerConfig AssetPickerConfig;
	{
		AssetPickerConfig.SelectionMode = ESelectionMode::Single;
		AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
		AssetPickerConfig.bFocusSearchBoxWhenOpened = true;
		AssetPickerConfig.bAllowNullSelection = false;
		AssetPickerConfig.bShowBottomToolbar = true;
		AssetPickerConfig.bAutohideSearchBar = false;
		AssetPickerConfig.bAllowDragging = false;
		AssetPickerConfig.bCanShowClasses = false;
		AssetPickerConfig.bShowPathInColumnView = true;
		AssetPickerConfig.bShowTypeInColumnView = false;
		AssetPickerConfig.bSortByPathInColumnView = false;
		AssetPickerConfig.ThumbnailScale = 0.4f;
		AssetPickerConfig.SaveSettingsName = TEXT("MoviePipelineQueueJobAsset");

		AssetPickerConfig.AssetShowWarningText = LOCTEXT("NoSequences_Warning", "No Level Sequences Found");
		AssetPickerConfig.Filter.ClassNames.Add(ULevelSequence::StaticClass()->GetFName());
		AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(this, &SMoviePipelineQueueEditor::OnCreateJobFromAsset);
	}

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("NewJob_MenuSection", "New Render Job"));
	{
		TSharedRef<SWidget> PresetPicker = SNew(SBox)
			.WidthOverride(300.f)
			.HeightOverride(300.f)
			[
				ContentBrowser.CreateAssetPicker(AssetPickerConfig)
			];

		MenuBuilder.AddWidget(PresetPicker, FText(), true, false);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

PRAGMA_ENABLE_OPTIMIZATION

void SMoviePipelineQueueEditor::OnCreateJobFromAsset(const FAssetData& InAsset)
{
	// Close the dropdown menu that showed them the assets to pick from.
	FSlateApplication::Get().DismissAllMenus();

	// Only try to initialize level sequences, in the event they had more than a level sequence selected when drag/dropping.
	ULevelSequence* LevelSequence = Cast<ULevelSequence>(InAsset.GetAsset());
	if (LevelSequence)
	{
		FScopedTransaction Transaction(FText::Format(LOCTEXT("CreateJob_Transaction", "Add {0}|plural(one=Job, other=Jobs)"), 1));

		UMoviePipelineQueue* ActiveQueue = GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>()->GetQueue();
		check(ActiveQueue);
		
		UMoviePipelineExecutorJob* NewJob = UMoviePipelineEditorBlueprintLibrary::CreateJobFromSequence(ActiveQueue, LevelSequence);
		if (!NewJob)
		{
			return;
		}

		PendingJobsToSelect.Add(NewJob);
		
		{
			// The job configuration is already set up with an empty configuration, but we'll try and use their last used preset 
			// (or a engine supplied default) for better user experience. 
			const UMovieRenderPipelineProjectSettings* ProjectSettings = GetDefault<UMovieRenderPipelineProjectSettings>();
			if (ProjectSettings->LastPresetOrigin.IsValid())
			{
				NewJob->SetPresetOrigin(ProjectSettings->LastPresetOrigin.Get());
			}
		}

		// Ensure the job has the settings specified by the project settings added. If they're already added
		// we don't modify the object so that we don't make it confused about whether or not you've modified the preset.
		UMoviePipelineEditorBlueprintLibrary::EnsureJobHasDefaultSettings(NewJob);
	}
}

void SMoviePipelineQueueEditor::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	UMoviePipelineQueue* ActiveQueue = GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>()->GetQueue();
	check(ActiveQueue);

	if (ActiveQueue)
	{
		if (CachedQueueSerialNumber != ActiveQueue->GetQueueSerialNumber())
		{
			ReconstructTree();
		}
	}
	// The sources are no longer valid, so we expect our cached serial number to be -1. If not, we haven't reset the tree yet.
	else if (CachedQueueSerialNumber != uint32(-1))
	{
		ReconstructTree();
	}

	if (PendingJobsToSelect.Num() > 0)
	{
		SetSelectedJobs_Impl(PendingJobsToSelect);
		PendingJobsToSelect.Empty();
	}
}

void SMoviePipelineQueueEditor::ReconstructTree()
{
	UMoviePipelineQueue* ActiveQueue = GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>()->GetQueue();
	check(ActiveQueue);
	if (!ActiveQueue)
	{
		CachedQueueSerialNumber = uint32(-1);
		RootNodes.Reset();
		return;
	}

	CachedQueueSerialNumber = ActiveQueue->GetQueueSerialNumber();

	// TSortedMap<FString, TSharedPtr<FMoviePipelineQueueJobTreeItem>> RootJobs;
	// for (TSharedPtr<IMoviePipelineQueueTreeItem> RootItem : RootNodes)
	// {
	// 	TSharedPtr<FMoviePipelineQueueJobTreeItem> RootCategory = RootItem->AsJob();
	// 	if (RootCategory.IsValid())
	// 	{
	// 		RootCategory->Children.Reset();
	// 		RootJobs.Add(RootCategory->Category.ToString(), RootCategory);
	// 	}
	// }

	RootNodes.Reset();

	// We attempt to re-use tree items in order to maintain selection states on them
	// TMap<FObjectKey, TSharedPtr<FTakeRecorderSourceTreeItem>> OldSourceToTreeItem;
	// Swap(SourceToTreeItem, OldSourceToTreeItem);

	for (UMoviePipelineExecutorJob* Job : ActiveQueue->GetJobs())
	{
		if (!Job)
		{
			continue;
		}

		TSharedPtr<FMoviePipelineQueueJobTreeItem> JobTreeItem = MakeShared<FMoviePipelineQueueJobTreeItem>(Job, OnEditConfigRequested, OnPresetChosen);

		// Add Shots
		for (UMoviePipelineExecutorShot* ShotInfo : Job->ShotInfo)
		{
			TSharedPtr<FMoviePipelineShotItem> Shot = MakeShared<FMoviePipelineShotItem>(Job, ShotInfo, OnEditConfigRequested, OnPresetChosen);
			JobTreeItem->Children.Add(Shot);
		}

		RootNodes.Add(JobTreeItem);
	}

	TreeView->RequestTreeRefresh();
}


FReply SMoviePipelineQueueEditor::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (CommandList->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

TSharedRef<ITableRow> SMoviePipelineQueueEditor::OnGenerateRow(TSharedPtr<IMoviePipelineQueueTreeItem> Item, const TSharedRef<STableViewBase>& Tree)
{
	// Let the item construct itself.
	return Item->ConstructWidget(SharedThis(this), Tree);
}

void SMoviePipelineQueueEditor::OnGetChildren(TSharedPtr<IMoviePipelineQueueTreeItem> Item, TArray<TSharedPtr<IMoviePipelineQueueTreeItem>>& OutChildItems)
{
	TSharedPtr<FMoviePipelineQueueJobTreeItem> Job = Item->AsJob();
	if (Job.IsValid())
	{
		OutChildItems.Append(Job->Children);
	}
}

FReply SMoviePipelineQueueEditor::OnDragDropTarget(TSharedPtr<FDragDropOperation> InOperation)
{
	if (InOperation)
	{
		if (InOperation->IsOfType<FAssetDragDropOp>())
		{
			TSharedPtr<FAssetDragDropOp> AssetDragDrop = StaticCastSharedPtr<FAssetDragDropOp>(InOperation);
			FScopedTransaction Transaction(FText::Format(LOCTEXT("CreateJob_Transaction", "Add {0}|plural(one=Job, other=Jobs)"), AssetDragDrop->GetAssets().Num()));

			for (const FAssetData& Asset : AssetDragDrop->GetAssets())
			{
				OnCreateJobFromAsset(Asset);
			}

			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

bool SMoviePipelineQueueEditor::CanDragDropTarget(TSharedPtr<FDragDropOperation> InOperation)
{
	bool bIsValid = false;
	if (InOperation)
	{
		if (InOperation->IsOfType<FAssetDragDropOp>())
		{
			TSharedPtr<FAssetDragDropOp> AssetDragDrop = StaticCastSharedPtr<FAssetDragDropOp>(InOperation);
			for (const FAssetData& Asset : AssetDragDrop->GetAssets())
			{
				ULevelSequence* LevelSequence = Cast<ULevelSequence>(Asset.GetAsset());
				if (LevelSequence)
				{
					// If at least one of them is a Level Sequence then we'll accept the drop.
					bIsValid = true;
					break;
				}
			}
		}
	}

	return bIsValid;
}

FReply SMoviePipelineQueueEditor::DeleteSelected()
{
	UMoviePipelineQueue* ActiveQueue = GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>()->GetQueue();
	check(ActiveQueue);

	if (ActiveQueue)
	{
		TArray<TSharedPtr<IMoviePipelineQueueTreeItem>> Items = TreeView->GetSelectedItems();

		FScopedTransaction Transaction(FText::Format(LOCTEXT("DeleteSelection", "Delete Selected {0}|plural(one=Job, other=Jobs)"), Items.Num()));
		ActiveQueue->Modify();

		for (TSharedPtr<IMoviePipelineQueueTreeItem> Item : Items)
		{
			Item->Delete(ActiveQueue);
		}
	}

	return FReply::Handled();
}

void SMoviePipelineQueueEditor::OnDeleteSelected()
{
	DeleteSelected();
}

bool SMoviePipelineQueueEditor::CanDeleteSelected() const
{
	return true;
}

void SMoviePipelineQueueEditor::OnDuplicateSelected()
{
	UMoviePipelineQueue* ActiveQueue = GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>()->GetQueue();
	check(ActiveQueue);

	if (ActiveQueue)
	{
		TArray<TSharedPtr<IMoviePipelineQueueTreeItem>> Items = TreeView->GetSelectedItems();

		FScopedTransaction Transaction(FText::Format(LOCTEXT("DuplicateSelection", "Duplicate Selected {0}|plural(one=Job, other=Jobs)"), Items.Num()));
		ActiveQueue->Modify();

		TArray<UMoviePipelineExecutorJob*> NewJobs;
		for (TSharedPtr<IMoviePipelineQueueTreeItem> Item : Items)
		{
			UMoviePipelineExecutorJob* NewJob = Item->Duplicate(ActiveQueue);
			if (NewJob)
			{
				NewJobs.Add(NewJob);
			}
		}

		PendingJobsToSelect = NewJobs;
	}
}

bool SMoviePipelineQueueEditor::CanDuplicateSelected() const
{
	return true;
}

void SMoviePipelineQueueEditor::OnResetStatus()
{
	UMoviePipelineQueue* ActiveQueue = GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>()->GetQueue();
	check(ActiveQueue);

	if (ActiveQueue)
	{
		TArray<TSharedPtr<IMoviePipelineQueueTreeItem>> Items = TreeView->GetSelectedItems();

		FScopedTransaction Transaction(FText::Format(LOCTEXT("ResetStatus", "Reset Status on {0}|plural(one=Job, other=Jobs)"), Items.Num()));
		ActiveQueue->Modify();

		for (TSharedPtr<IMoviePipelineQueueTreeItem> Item : Items)
		{
			Item->ResetStatus();
		}
	}
}

void SMoviePipelineQueueEditor::SetSelectedJobs_Impl(const TArray<UMoviePipelineExecutorJob*>& InJobs)
{
	TreeView->ClearSelection();

	TArray<TSharedPtr<IMoviePipelineQueueTreeItem>> AllTreeItems;

	// Get all of our items first
	for (TSharedPtr<IMoviePipelineQueueTreeItem> Item : RootNodes)
	{
		AllTreeItems.Add(Item);
		OnGetChildren(Item, AllTreeItems);
	}


	TArray<TSharedPtr<IMoviePipelineQueueTreeItem>> SelectedTreeItems;
	for (TSharedPtr<IMoviePipelineQueueTreeItem> Item : AllTreeItems)
	{
		TSharedPtr<FMoviePipelineQueueJobTreeItem> JobTreeItem = Item->AsJob();
		if (JobTreeItem.IsValid())
		{
			if (InJobs.Contains(JobTreeItem->WeakJob.Get()))
			{
				SelectedTreeItems.Add(Item);
			}
		}
	}

	TreeView->SetItemSelection(SelectedTreeItems, true, ESelectInfo::Direct);
}

void SMoviePipelineQueueEditor::OnJobSelectionChanged_Impl(TSharedPtr<IMoviePipelineQueueTreeItem> TreeItem, ESelectInfo::Type SelectInfo)
{
	TArray<UMoviePipelineExecutorJob*> SelectedJobs;
	if (TreeItem.IsValid())
	{
		// Iterate the tree and get all selected items.
		TArray<TSharedPtr<IMoviePipelineQueueTreeItem>> SelectedTreeItems = TreeView->GetSelectedItems();
		for (TSharedPtr<IMoviePipelineQueueTreeItem> Item : SelectedTreeItems)
		{
			UMoviePipelineExecutorJob* Job = Item->GetOwningJob();
			if (Job)
			{
				SelectedJobs.AddUnique(Job);
			}
		}
	}

	OnJobSelectionChanged.ExecuteIfBound(SelectedJobs);
}

#undef LOCTEXT_NAMESPACE