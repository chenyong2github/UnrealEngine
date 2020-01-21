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


#define LOCTEXT_NAMESPACE "SMoviePipelineQueueEditor"

struct FMoviePipelineQueueJobTreeItem;
struct FMoviePipelineMapTreeItem;
struct FMoviePipelineShotItem;
class SQueueJobListRow;

struct IMoviePipelineQueueTreeItem : TSharedFromThis<IMoviePipelineQueueTreeItem>
{
	virtual ~IMoviePipelineQueueTreeItem() {}

	virtual TSharedPtr<FMoviePipelineQueueJobTreeItem> AsJob() { return nullptr; }
	virtual void Delete(UMoviePipelineQueue* InOwningQueue) {}

	virtual TSharedRef<ITableRow> ConstructWidget(TWeakPtr<SMoviePipelineQueueEditor> InQueueWidget, const TSharedRef<STableViewBase>& OwnerTable) = 0;
};

class SQueueJobListRow : public SMultiColumnTableRow<TSharedPtr<IMoviePipelineQueueTreeItem>>
{
public:
	SLATE_BEGIN_ARGS(SQueueJobListRow) {}
		SLATE_ARGUMENT(TSharedPtr<FMoviePipelineQueueJobTreeItem>, Item)
		SLATE_EVENT(FOnMoviePipelineEditConfig, OnEditConfigRequested)
	SLATE_END_ARGS()

	static const FName NAME_Sequence;
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

	FOnMoviePipelineEditConfig Callback;

	explicit FMoviePipelineQueueJobTreeItem(UMoviePipelineExecutorJob* InJob, FOnMoviePipelineEditConfig InCallback)
		: WeakJob(InJob)
		, Callback(InCallback)
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

	virtual void Delete(UMoviePipelineQueue* InOwningQueue) override
	{
		InOwningQueue->DeleteJob(WeakJob.Get());
	}

public:
	FString GetSequencePath() const
	{
		UMoviePipelineExecutorJob* Job = WeakJob.Get();
		if (Job)
		{
			return Job->Sequence.ToString();
		}

		return FString();
	}

	void SetSequencePath(const FAssetData& AssetData)
	{
		UMoviePipelineExecutorJob* Job =WeakJob.Get();
		if (Job)
		{
			Job->Sequence = AssetData.ToSoftObjectPath();
		}
	}

	FString GetMasterConfigPath() const
	{
		UMoviePipelineExecutorJob* Job = WeakJob.Get();
		if (Job)
		{
			UMoviePipelineConfigBase* Config = Job->GetConfiguration();
			if (Config)
			{
				return Config->GetPathName();
			}
		}

		return FString();
	}

	void SetMasterConfigPath(const FAssetData& AssetData)
	{
		UMoviePipelineExecutorJob* Job = WeakJob.Get();
		if (Job)
		{
			Job->SetPresetOrigin(CastChecked<UMoviePipelineMasterConfig>(AssetData.GetAsset()));
		}
	}

	EVisibility IsMasterConfigModified() const
	{
		UMoviePipelineExecutorJob* Job = WeakJob.Get();
		if (Job)
		{
			
			return (Job->GetPresetOrigin() == nullptr) ? EVisibility::Visible : EVisibility::Collapsed;
		}
		
		return EVisibility::Collapsed;
	}

	FReply OnEditMasterConfigForJob()
	{
		Callback.ExecuteIfBound(WeakJob, nullptr);

		return FReply::Handled();
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
			// JobStatus 0 is Uninitialized, so we take one off.
			return FMath::Clamp(int32(Job->JobStatus) - 1, 0, 2);
		}

		return 0;
	}

	TOptional<float> GetProgressPercent() const
	{
		UMoviePipelineExecutorJob* Job = WeakJob.Get();
		return Job ? Job->GetProgressPercentage() : TOptional<float>();
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
	if (ColumnName == NAME_Sequence)
	{
		return SNew(SBox)
			.Padding(2.0f)
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
					SNew(SObjectPropertyEntryBox)
					.ObjectPath(Item.Get(), &FMoviePipelineQueueJobTreeItem::GetSequencePath)
					.AllowedClass(ULevelSequence::StaticClass())
					.OnObjectChanged(Item.Get(), &FMoviePipelineQueueJobTreeItem::SetSequencePath)
					.AllowClear(false)
					.DisplayUseSelected(false)
					.DisplayBrowse(true)
					.DisplayThumbnail(true)
					.DisplayCompactSize(false)
				]
			];
	}
	else if (ColumnName == NAME_Settings)
	{
		return SNew(SHorizontalBox)

		// Preset Label
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(2, 0)
		[
			SNew(SObjectPropertyEntryBox)
			.ObjectPath(Item.Get(), &FMoviePipelineQueueJobTreeItem::GetMasterConfigPath)
			.AllowedClass(UMoviePipelineMasterConfig::StaticClass())
			.OnObjectChanged(Item.Get(), &FMoviePipelineQueueJobTreeItem::SetMasterConfigPath)
			.AllowClear(false)
			.DisplayUseSelected(false)
			.DisplayBrowse(true)
			.DisplayThumbnail(true)
			.DisplayCompactSize(false)
		]

		// Modified button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ModifiedConfigLabel", "(Modified)"))
			.Visibility(Item.Get(), &FMoviePipelineQueueJobTreeItem::IsMasterConfigModified)
		]

		// Edit Button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.OnClicked(Item.Get(), &FMoviePipelineQueueJobTreeItem::OnEditMasterConfigForJob)
			.Content()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("EditMasterConfigButton_Label", "Edit"))
			]
		];
	}
	else if (ColumnName == NAME_Output)
	{
		return SNew(SBox)
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

			// Ready Label
			+ SWidgetSwitcher::Slot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				// .Style(FTakeRecorderStyle::Get(), "TakeRecorder.Source.Switch")
				.Text(LOCTEXT("PendingJobStatus_Label", "Ready"))
			]

			// Progress Bar
			+ SWidgetSwitcher::Slot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			[
				SNew(SProgressBar)
				.Percent(Item.Get(), &FMoviePipelineQueueJobTreeItem::GetProgressPercent)
			]

			// Completed
			+ SWidgetSwitcher::Slot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PendingJobStatus_Label", "Completed!"))
			];
	}

	return SNullWidget::NullWidget;
}

const FName SQueueJobListRow::NAME_Sequence = FName(TEXT("Sequence"));
const FName SQueueJobListRow::NAME_Settings = FName(TEXT("Settings"));
const FName SQueueJobListRow::NAME_Output = FName(TEXT("Output"));
const FName SQueueJobListRow::NAME_Status = FName(TEXT("Status"));

struct FMoviePipelineMapTreeItem : IMoviePipelineQueueTreeItem
{
	/** The job that this tree item represents */
	TWeakObjectPtr<UMoviePipelineExecutorJob> WeakJob;

	explicit FMoviePipelineMapTreeItem(UMoviePipelineExecutorJob* InJob)
		: WeakJob(InJob)
	{}

	virtual TSharedRef<ITableRow> ConstructWidget(TWeakPtr<SMoviePipelineQueueEditor> InQueueWidget, const TSharedRef<STableViewBase>& OwnerTable) override
	{
		return SNew(STableRow<TSharedPtr<IMoviePipelineQueueTreeItem>>, OwnerTable)
			.Content()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("MapRow_Label", "Target Map:"))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4, 0, 0, 0)
				.VAlign(VAlign_Center)
				[
					SNew(SObjectPropertyEntryBox)
					.ObjectPath(this, &FMoviePipelineMapTreeItem::GetMapPath)
					.AllowedClass(UWorld::StaticClass())
					.OnObjectChanged(this, &FMoviePipelineMapTreeItem::SetMapPath)
					.AllowClear(false)
					.DisplayUseSelected(false)
					.DisplayBrowse(true)
					.DisplayThumbnail(true)
					.DisplayCompactSize(false)
				]
			];
	}

public:
	FString GetMapPath() const
	{
		UMoviePipelineExecutorJob* Job = WeakJob.Get();
		if (Job)
		{
			UWorld* World = Cast<UWorld>(Job->Map.TryLoad());
			if (World)
			{
				return World->GetPathName();
			}
		}

		return FString();
	}

	void SetMapPath(const FAssetData& AssetData)
	{
		UMoviePipelineExecutorJob* Job = WeakJob.Get();
		if (Job)
		{
			Job->Map = CastChecked<UWorld>(AssetData.GetAsset());
		}
	}
};

struct FMoviePipelineShotItem : IMoviePipelineQueueTreeItem
{
	/** The job that this tree item represents */
	TWeakObjectPtr<UMoviePipelineExecutorJob> WeakJob;

	explicit FMoviePipelineShotItem(UMoviePipelineExecutorJob* InJob)
		: WeakJob(InJob)
	{}

	virtual TSharedRef<ITableRow> ConstructWidget(TWeakPtr<SMoviePipelineQueueEditor> InQueueWidget, const TSharedRef<STableViewBase>& OwnerTable) override
	{
		return SNew(STableRow<TSharedPtr<IMoviePipelineQueueTreeItem>>, OwnerTable)
			.Content()
			[
				SNullWidget::NullWidget
			];
	}

	/*virtual TSharedRef<SWidget> ConstructWidget(TWeakPtr<SMoviePipelineQueueEditor> InQueueWidget) override
	{
		return SNew(SHorizontalBox)

			// Toggle Checkbox for deciding to render or not.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(20, 4)
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				// .Style(FTakeRecorderStyle::Get(), "TakeRecorder.Source.Switch")
				.IsFocusable(false)
				.IsChecked(this, &FMoviePipelineShotItem::GetCheckState)
				.OnCheckStateChanged(this, &FMoviePipelineShotItem::SetCheckState, InQueueWidget)
			]
			
			// Shot Name Label
			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(20, 4)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &FMoviePipelineShotItem::GetShotLabel)
			]

			// Preset Label
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8, 0)
			[
				SNew(STextBlock)
				.Text(this, &FMoviePipelineShotItem::GetPresetLabel)
			]

			// Spacer
			+ SHorizontalBox::Slot()
			.FillWidth(1)
			[
				SNew(SBox)
			]

			// Status 
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8, 0, 24, 0)
			[
				SNew(SWidgetSwitcher)
				.WidgetIndex(this, &FMoviePipelineShotItem::GetStatusIndex)

				// Ready Label
				+ SWidgetSwitcher::Slot()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					// .Style(FTakeRecorderStyle::Get(), "TakeRecorder.Source.Switch")
					.Text(LOCTEXT("PendingJobStatus_Label", "Ready"))
				]

				// Progress Bar
				+ SWidgetSwitcher::Slot()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				[
					SNew(SProgressBar)
					// .Style(FTakeRecorderStyle::Get(), "TakeRecorder.Source.Switch")
					.Percent(this, &FMoviePipelineShotItem::GetProgressPercent)
				]

				// Completed
				+ SWidgetSwitcher::Slot()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PendingJobStatus_Label", "Completed!"))
				]
			]
			;
	}*/

	ECheckBoxState GetCheckState() const
	{
		return ECheckBoxState::Checked;
	}

	void SetCheckState(ECheckBoxState InNewState, TWeakPtr<SMoviePipelineQueueEditor> InQueueWidget)
	{

	}

	FText GetShotLabel() const
	{
		return LOCTEXT("ExampleShotName", "ShotName_001");
	}

	FText GetPresetLabel() const
	{
		return LOCTEXT("ExampleShotName2", "Use Master");
	}

	int32 GetStatusIndex() const
	{
		return 1;
	}

	TOptional<float> GetProgressPercent() const
	{
		return .1f;
	}
};

PRAGMA_DISABLE_OPTIMIZATION
void SMoviePipelineQueueEditor::Construct(const FArguments& InArgs)
{
	CachedQueueSerialNumber = uint32(-1);
	OnEditConfigRequested = InArgs._OnEditConfigRequested;

	TreeView = SNew(STreeView<TSharedPtr<IMoviePipelineQueueTreeItem>>)
		.TreeItemsSource(&RootNodes)
		// .OnSelectionChanged(InArgs._OnSelectionChanged)
		.OnGenerateRow(this, &SMoviePipelineQueueEditor::OnGenerateRow)
		.OnGetChildren(this, &SMoviePipelineQueueEditor::OnGetChildren)
		.HeaderRow
		(
			SNew(SHeaderRow)

			+ SHeaderRow::Column(SQueueJobListRow::NAME_Sequence)
			.FillWidth(0.3f)
			.DefaultLabel(LOCTEXT("QueueHeaderSequence_Text", "Sequence"))

			+ SHeaderRow::Column(SQueueJobListRow::NAME_Settings)
			.FillWidth(0.3)
			.DefaultLabel(LOCTEXT("QueueHeaderSettings_Text", "Settings"))

			+ SHeaderRow::Column(SQueueJobListRow::NAME_Output)
			.FillWidth(0.4)
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
		ActiveQueue->Modify();

		UMoviePipelineExecutorJob* NewJob = ActiveQueue->AllocateNewJob();
		NewJob->Modify();

		{
			// We'll assume they went to render from the current world - they can always override it later.
			FSoftObjectPath CurrentWorld = FSoftObjectPath(GEditor->GetEditorWorldContext().World());
			FSoftObjectPath Sequence = InAsset.ToSoftObjectPath();

			NewJob->Sequence = Sequence;
			NewJob->Map = CurrentWorld;
		}

		{
			// The job configuration is already set up with an empty configuration, but we'll try and use their last used preset 
			// (or a engine supplied default) for better user experience. 
			const UMovieRenderPipelineProjectSettings* ProjectSettings = GetDefault<UMovieRenderPipelineProjectSettings>();
			if (ProjectSettings->LastPresetOrigin.IsValid())
			{
				NewJob->SetPresetOrigin(ProjectSettings->LastPresetOrigin.Get());
			}
		}
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

		TSharedPtr< FMoviePipelineQueueJobTreeItem> JobTreeItem = MakeShared< FMoviePipelineQueueJobTreeItem>(Job, OnEditConfigRequested);
		TSharedPtr<FMoviePipelineMapTreeItem> MapTreeItem = MakeShared< FMoviePipelineMapTreeItem>(Job);
		JobTreeItem->Children.Add(MapTreeItem);
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

#undef LOCTEXT_NAMESPACE