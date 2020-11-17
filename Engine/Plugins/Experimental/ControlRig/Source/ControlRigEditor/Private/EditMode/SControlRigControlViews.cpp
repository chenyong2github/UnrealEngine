// Copyright Epic Games, Inc. All Rights Reserved.


#include "SControlRigControlViews.h"
#include "ControlRig.h"
#include "Tools/ControlRigPose.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "EditorStyleSet.h"
#include "ScopedTransaction.h"
#include "Editor/EditorEngine.h"
#include "AssetViewUtils.h"
#include "AssetRegistryModule.h"
#include "IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "AssetThumbnail.h"
#include "FileHelpers.h"
#include "Tools/ControlRigPoseMirrorSettings.h"
#include "ControlRigEditMode.h"
#include "EditorModeManager.h"

#define LOCTEXT_NAMESPACE "ControlRigBaseListWidget"


void FControlRigView::CaptureThumbnail(UObject* Asset)
{
	FViewport* Viewport = GEditor->GetActiveViewport();

	if (GCurrentLevelEditingViewportClient && Viewport)
	{
		//have to re-render the requested viewport
		FLevelEditorViewportClient* OldViewportClient = GCurrentLevelEditingViewportClient;
		//remove selection box around client during render
		GCurrentLevelEditingViewportClient = NULL;
		Viewport->Draw();

		TArray<FAssetData> SelectedAssets;
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		FName PathName = *Asset->GetPathName();
		FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(PathName);
		SelectedAssets.Emplace(AssetData);
		AssetViewUtils::CaptureThumbnailFromViewport(Viewport, SelectedAssets);

		//redraw viewport to have the yellow highlight again
		GCurrentLevelEditingViewportClient = OldViewportClient;
		Viewport->Draw();
	}
	
}

/** Widget wraps an editable text box for editing name of the asset */
class SControlRigAssetEditableTextBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SControlRigAssetEditableTextBox) {}

		SLATE_ARGUMENT(TWeakObjectPtr<UObject>, Asset)

		SLATE_END_ARGS()

		/**
		 * Construct this widget
		 *
		 * @param	InArgs	The declaration data for this widget
		 */
		void Construct(const FArguments& InArgs);


private:

	/** Getter for the Text attribute of the editable text inside this widget */
	FText GetNameText() const;

	/** Getter for the ToolTipText attribute of the editable text inside this widget */
	FText GetNameTooltipText() const;


	/** Getter for the OnTextCommitted event of the editable text inside this widget */
	void OnNameTextCommitted(const FText& NewText, ETextCommit::Type InTextCommit);

	/** Callback to verify a text change */
	void OnTextChanged(const FText& InLabel);

	/** The list of objects whose names are edited by the widget */
	TWeakObjectPtr<UObject> Asset;

	/** The text box used to edit object names */
	TSharedPtr< SEditableTextBox > TextBox;

};

void SControlRigAssetEditableTextBox::Construct(const FArguments& InArgs)
{
	Asset = InArgs._Asset;
	ChildSlot
		[
			SAssignNew(TextBox, SEditableTextBox)
			.Text(this, &SControlRigAssetEditableTextBox::GetNameText)
			.ToolTipText(this, &SControlRigAssetEditableTextBox::GetNameTooltipText)
			.OnTextCommitted(this, &SControlRigAssetEditableTextBox::OnNameTextCommitted)
			.OnTextChanged(this, &SControlRigAssetEditableTextBox::OnTextChanged)
			.RevertTextOnEscape(true)
		];
}

FText SControlRigAssetEditableTextBox::GetNameText() const
{
	if (Asset.IsValid())
	{
		FString Result = Asset.Get()->GetName();
		return FText::FromString(Result);
	}
	return FText();
}

FText SControlRigAssetEditableTextBox::GetNameTooltipText() const
{
	FText Result = FText::Format(LOCTEXT("AssetRenameTooltip", "Rename the selected {0}"), FText::FromString(Asset.Get()->GetClass()->GetName()));
	
	return Result;
}


void SControlRigAssetEditableTextBox::OnNameTextCommitted(const FText& NewText, ETextCommit::Type InTextCommit)
{

	if (InTextCommit != ETextCommit::OnCleared)
	{
		FText TrimmedText = FText::TrimPrecedingAndTrailing(NewText);

		if (!TrimmedText.IsEmpty())
		{

			IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
			FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FName(*Asset->GetPathName()));
			const FString PackagePath = FPackageName::GetLongPackagePath(Asset->GetOutermost()->GetName());

			//Need to save asset before renaming else may lose snapshot
			// save existing play list asset
			TArray<UPackage*> PackagesToSave;
			PackagesToSave.Add(Asset->GetPackage());
			FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, false /*bCheckDirty*/, false /*bPromptToSave*/);

			FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

			TArray<FAssetRenameData> AssetsAndNames;
			AssetsAndNames.Emplace(FAssetRenameData(Asset, PackagePath, TrimmedText.ToString()));
			AssetToolsModule.Get().RenameAssetsWithDialog(AssetsAndNames);

		}
			
		// Remove ourselves from the window focus so we don't get automatically reselected when scrolling around the context menu.
		TSharedPtr< SWindow > ParentWindow = FSlateApplication::Get().FindWidgetWindow(SharedThis(this));
		if (ParentWindow.IsValid())
		{
			ParentWindow->SetWidgetToFocusOnActivate(NULL);
		}
	}

	// Clear Error 
	TextBox->SetError(FText::GetEmpty());
}

void SControlRigAssetEditableTextBox::OnTextChanged(const FText& InLabel)
{
	const FString PackagePath = FPackageName::GetLongPackagePath(Asset->GetOutermost()->GetName());
	const FString PackageName = PackagePath / InLabel.ToString();
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *(InLabel.ToString()));

	FText OutErrorMessage;
	if (!AssetViewUtils::IsValidObjectPathForCreate(ObjectPath, OutErrorMessage))
	{
		TextBox->SetError(OutErrorMessage);
	}
	else
	{
		TextBox->SetError(FText::GetEmpty());
	}
}

void SControlRigPoseView::Construct(const FArguments& InArgs)
{
	PoseAsset = InArgs._PoseAsset;

	bIsKey = false;
	bIsMirror = false;
	PoseBlendValue = 0.0f;
	bIsBlending = false;
	bSliderStartedTransaction = false;
	InitialControlValues.SetNum(0);

	TSharedRef<SWidget> Thumbnail = GetThumbnailWidget();
	TSharedRef <SControlRigAssetEditableTextBox> ObjectNameBox = SNew(SControlRigAssetEditableTextBox).Asset(PoseAsset);

	//Not used currently CreateControlList();

	//for mirror settings
	UControlRigPoseMirrorSettings* MirrorSettings = GetMutableDefault<UControlRigPoseMirrorSettings>();
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bShowOptions = false;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.bShowPropertyMatrixButton = false;
	DetailsViewArgs.bUpdatesFromSelection = false;
	DetailsViewArgs.bLockable = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.ViewIdentifier = "Create Control Asset";

	MirrorDetailsView = PropertyEditor.CreateDetailView(DetailsViewArgs);
	MirrorDetailsView->SetObject(MirrorSettings);
			
	ChildSlot
	[

		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		
			.FillHeight(1)
			.Padding(0, 0, 0, 4)
			[
			SNew(SSplitter)

				+ SSplitter::Slot()
				.Value(0.33f)
				[
					SNew(SBorder)
					.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.Padding(5.f)
						[
							SNew(SBox)
							.VAlign(VAlign_Center)
						[
							ObjectNameBox
						]
						]

					+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.Padding(5.f)
						[
							SNew(SBox)
							.VAlign(VAlign_Center)
						[
							Thumbnail
						]
						]
					+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.Padding(5.f)

						[
							SNew(SButton)
							.ContentPadding(FMargin(10, 5))
						.Text(LOCTEXT("CaptureThmbnail", "Capture Thumbnail"))
						.ToolTipText(LOCTEXT("CaptureThmbnailTooltip", "Captures a thumbnail from the active viewport"))
						.OnClicked(this, &SControlRigPoseView::OnCaptureThumbnail)
						]
					]
				]

			+ SSplitter::Slot()
				.Value(0.33f)
				[
					SNew(SBorder)
					.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							.Padding(5.f)

							[
							SNew(SButton)
								.ContentPadding(FMargin(10, 5))
							.Text(LOCTEXT("PastePose", "Paste Pose"))
							.OnClicked(this, &SControlRigPoseView::OnPastePose)
							]
						+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							.Padding(5.f)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
							.AutoWidth()
							.HAlign(HAlign_Center)
							.Padding(5.f)
							[
								SNew(SCheckBox)
								.IsChecked(this, &SControlRigPoseView::IsKeyPoseChecked)
							.OnCheckStateChanged(this, &SControlRigPoseView::OnKeyPoseChecked)
							.Padding(5.0f)

							[
								SNew(STextBlock).Text(LOCTEXT("Key", "Key"))

							]
							]

						+ SHorizontalBox::Slot()
							.AutoWidth()

							.HAlign(HAlign_Center)

							.Padding(5.f)
							[
								SNew(SCheckBox)
								.IsChecked(this, &SControlRigPoseView::IsMirrorPoseChecked)
							.OnCheckStateChanged(this, &SControlRigPoseView::OnMirrorPoseChecked)
							.IsEnabled(this, &SControlRigPoseView::IsMirrorEnabled)
							.Padding(1.0f)
							[
								SNew(STextBlock).Text(LOCTEXT("Mirror", "Mirror"))
								.IsEnabled(this, &SControlRigPoseView::IsMirrorEnabled)
							]
							]
							]
						+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							.Padding(5.f)
							[

								SNew(SNumericEntryBox<float>)
								// Only allow spinning if we have a single value
								.Value(this, &SControlRigPoseView::OnGetPoseBlendValue)
								.AllowSpin(true)
								.MinValue(-1.0f)
								.MaxValue(2.0f)
								.MinSliderValue(-1.0f)
								.MaxSliderValue(2.0f)
								.SliderExponent(1)
								.Delta(0.005f)
								.OnValueChanged(this, &SControlRigPoseView::OnPoseBlendChanged)
								.OnValueCommitted(this, &SControlRigPoseView::OnPoseBlendCommited)
								.OnBeginSliderMovement(this,&SControlRigPoseView::OnBeginSliderMovement)
								.OnEndSliderMovement(this,&SControlRigPoseView::OnEndSliderMovement)

							]
					]
				]
			+ SSplitter::Slot()
				.Value(0.33f)
				[
					MirrorDetailsView.ToSharedRef()
				]
			/*  todo may want to put this back, it let's you see the controls...
			+ SSplitter::Slot()
				.Value(0.33f)
				[
					SNew(SBorder)
					.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							.Padding(2.f)
							[

							SNew(SButton)
								.ContentPadding(FMargin(5, 5))
								.Text(LOCTEXT("SelectControls", "Select Controls"))
								.ToolTipText(LOCTEXT("SelectControlsTooltip", "Select Controls From This Asset"))
								.OnClicked(this, &SControlRigPoseView::OnSelectControls)
				
							]
						+ SVerticalBox::Slot()
							.VAlign(VAlign_Fill)
							.HAlign(HAlign_Center)
							.Padding(5.f)

							[
							SNew(SListView< TSharedPtr<FString> >)
								.ItemHeight(24)
								.ListItemsSource(&ControlList)
								.SelectionMode(ESelectionMode::None)
								.OnGenerateRow(this, &SControlRigPoseView::OnGenerateWidgetForList)
							]
					]
				]
				*/
			]
			

	];

}

ECheckBoxState SControlRigPoseView::IsKeyPoseChecked() const
{
	if (bIsKey)
	{
		return ECheckBoxState::Checked;
	}
	return ECheckBoxState::Unchecked;
}

void SControlRigPoseView::OnKeyPoseChecked(ECheckBoxState NewState)
{
	if (NewState == ECheckBoxState::Checked)
	{
		bIsKey = true;
	}
	else
	{
		bIsKey = false;
	}
}
ECheckBoxState SControlRigPoseView::IsMirrorPoseChecked() const
{
	if (bIsMirror)
	{
		return ECheckBoxState::Checked;
	}
	return ECheckBoxState::Unchecked;
}

void SControlRigPoseView::OnMirrorPoseChecked(ECheckBoxState NewState)
{
	if (NewState == ECheckBoxState::Checked)
	{
		bIsMirror = true;
	}
	else
	{
		bIsMirror = false;
	}
}

bool SControlRigPoseView::IsMirrorEnabled() const
{
	return true;
}


FReply SControlRigPoseView::OnPastePose()
{
	if (GetControlRig() && PoseAsset.IsValid())
	{
		PoseAsset->PastePose(GetControlRig(), bIsKey,bIsMirror);
	}
	return FReply::Handled();
}

FReply SControlRigPoseView::OnSelectControls()
{
	if (GetControlRig() && PoseAsset.IsValid())
	{
		PoseAsset->SelectControls(GetControlRig());
	}
	return FReply::Handled();
}

void SControlRigPoseView::OnPoseBlendChanged(float ChangedVal)
{
	UControlRig* ControlRig = GetControlRig();
	if (ControlRig && PoseAsset.IsValid())
	{

		PoseBlendValue = ChangedVal;
		if (!bIsBlending)
		{
			bIsBlending = true;
			InitialControlValues = PoseAsset->GetCurrentPose(ControlRig);
		}

		PoseAsset->BlendWithInitialPoses(InitialControlValues, ControlRig, false, bIsMirror, PoseBlendValue);
	}
}
void SControlRigPoseView::OnBeginSliderMovement()
{
	if (bSliderStartedTransaction == false)
	{
		bSliderStartedTransaction = true;
		GEditor->BeginTransaction(LOCTEXT("PastePoseTransation", "Paste Pose"));
	}
}
void SControlRigPoseView::OnEndSliderMovement(float NewValue)
{
	if (bSliderStartedTransaction)
	{
		GEditor->EndTransaction();
		bSliderStartedTransaction = false;

	}
}

void SControlRigPoseView::OnPoseBlendCommited(float ChangedVal, ETextCommit::Type Type)
{
	UControlRig* ControlRig = GetControlRig();
	if (ControlRig && PoseAsset.IsValid())
	{
		FScopedTransaction ScopedTransaction(LOCTEXT("PastePoseTransaction", "Paste Pose"));
		PoseBlendValue = ChangedVal;
		PoseAsset->BlendWithInitialPoses(InitialControlValues, ControlRig, bIsKey, bIsMirror, PoseBlendValue);
		bIsBlending = false;
		PoseBlendValue = 0.0f;
	}
}

FReply SControlRigPoseView::OnCaptureThumbnail()
{
	FControlRigView::CaptureThumbnail(PoseAsset.Get());
	return FReply::Handled();
}

TSharedRef<SWidget> SControlRigPoseView::GetThumbnailWidget()
{
	TSharedPtr<SWidget> ThumbnailWidget = nullptr;

	ThumbnailPool = MakeShared<FAssetThumbnailPool>(1, false);

	const int32 ThumbnailSize = 128;

	TSharedRef<FAssetThumbnail> AssetThumbnail = MakeShared<FAssetThumbnail>(PoseAsset.Get(), ThumbnailSize, ThumbnailSize, ThumbnailPool);
	ThumbnailWidget = AssetThumbnail->MakeThumbnailWidget();


	return SNew(SBox)
		.WidthOverride(ThumbnailSize + 5)
		.HeightOverride(ThumbnailSize + 5)
		[
			ThumbnailWidget.IsValid() ? ThumbnailWidget.ToSharedRef() : SNullWidget::NullWidget
		];
}

UControlRig* SControlRigPoseView::GetControlRig()
{
	if (FControlRigEditMode* EditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName)))
	{
		return EditMode->GetControlRig(true);
	}
	return nullptr;
}

/* We may want to list the Controls in it (design said no but animators said yes)
void SControlRigPoseView::CreateControlList()
{
	if (PoseAsset.IsValid())
	{
		const TArray<FName>& Controls = PoseAsset.Get()->GetControlNames();
		for (const FName& ControlName : Controls)
		{
			ControlList.Add(MakeShared<FString>(ControlName.ToString()));
		}
	}
}
*/

#undef LOCTEXT_NAMESPACE
