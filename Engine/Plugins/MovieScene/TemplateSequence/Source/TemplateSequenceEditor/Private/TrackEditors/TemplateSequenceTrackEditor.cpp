// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrackEditors/TemplateSequenceTrackEditor.h"
#include "AssetData.h"
#include "AssetRegistryModule.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraAnim.h"
#include "CameraAnimationSequence.h"
#include "CollectionManagerModule.h"
#include "CommonMovieSceneTools.h"
#include "ContentBrowserModule.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "ICollectionManager.h"
#include "IContentBrowserSingleton.h"
#include "MovieSceneCommonHelpers.h"
#include "MovieSceneTimeHelpers.h"
#include "Sections/MovieSceneCameraAnimSection.h"
#include "Sections/TemplateSequenceSection.h"
#include "SequencerSectionPainter.h"
#include "SequencerUtilities.h"
#include "TemplateSequence.h"
#include "TrackEditors/CameraAnimTrackEditorHelper.h"
#include "Tracks/MovieSceneCameraAnimTrack.h"
#include "Tracks/TemplateSequenceTrack.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"

#define LOCTEXT_NAMESPACE "FTemplateSequenceTrackEditor"

FTemplateSequenceTrackEditor::FTemplateSequenceTrackEditor(TSharedRef<ISequencer> InSequencer)
	: FMovieSceneTrackEditor(InSequencer)
{
}

TSharedRef<ISequencerTrackEditor> FTemplateSequenceTrackEditor::CreateTrackEditor(TSharedRef<ISequencer> InSequencer)
{
	return MakeShareable(new FTemplateSequenceTrackEditor(InSequencer));
}

bool FTemplateSequenceTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> Type) const
{
	return Type == UTemplateSequenceTrack::StaticClass();
}

void FTemplateSequenceTrackEditor::BuildObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass)
{
	bool bIsCameraAnimMenu = true;
	for (FGuid ObjectBinding : ObjectBindings)
	{
		const UCameraComponent* CameraComponent = AcquireCameraComponentFromObjectGuid(ObjectBinding);
		if (CameraComponent == nullptr)
		{
			bIsCameraAnimMenu = false;
			break;
		}
	}

	FText SubMenuEntryText = LOCTEXT("AddTemplateSequence", "Template Sequence");
	FText SubMenuEntryTooltip = LOCTEXT("AddTemplateSequenceTooltip", "Adds a track that can play a template sequence asset using the parent binding.");
	if (bIsCameraAnimMenu)
	{
		SubMenuEntryText = LOCTEXT("AddCameraAnimationSequence", "Camera Animation");
		SubMenuEntryTooltip = LOCTEXT("AddCameraAnimationSequenceTooltip", "Adds a camera animation template sequence on the parent binding.");
	}
	
	MenuBuilder.AddSubMenu(
		SubMenuEntryText, SubMenuEntryTooltip,
		FNewMenuDelegate::CreateRaw(this, &FTemplateSequenceTrackEditor::AddTemplateSequenceAssetSubMenu, ObjectBindings, ObjectClass, bIsCameraAnimMenu)
	);
}

TSharedPtr<SWidget> FTemplateSequenceTrackEditor::BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params)
{
	const UClass* ObjectClass = AcquireObjectClassFromObjectGuid(ObjectBinding);

	if (ObjectClass != nullptr)
	{
		const UCameraComponent* CameraComponent = AcquireCameraComponentFromObjectGuid(ObjectBinding);
		const bool bIsCameraAnimMenu = (CameraComponent != nullptr);

		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				FSequencerUtilities::MakeAddButton(
					LOCTEXT("TemplateSequenceAddButton", "Template Sequence"),
					FOnGetContent::CreateSP(this, &FTemplateSequenceTrackEditor::BuildTemplateSequenceAssetSubMenu, ObjectBinding, ObjectClass, bIsCameraAnimMenu),
					Params.NodeIsHovered, GetSequencer())
			];
	}
	else
	{
		return TSharedPtr<SWidget>();
	}
}

TSharedRef<ISequencerSection> FTemplateSequenceTrackEditor::MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding)
{
	check( SupportsType( SectionObject.GetOuter()->GetClass() ) );
	
	return MakeShareable(new FTemplateSequenceSection(GetSequencer(), *CastChecked<UTemplateSequenceSection>(&SectionObject)));
}

class STemplateSequenceAssetSubMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STemplateSequenceAssetSubMenu)
	{}
	SLATE_END_ARGS();

	void Construct(const FArguments& Args, TSharedPtr<FTemplateSequenceTrackEditor> InTrackEditor, TArray<FGuid> ObjectBindings, const UClass* InBaseClass, const UClass* InLegacyBaseClass)
	{
		check(InBaseClass != nullptr);
		BaseClass = InBaseClass;
		LegacyBaseClass = InLegacyBaseClass;

		TrackEditor = InTrackEditor;

		// Find all the class names that derive from this class.
		FARFilter Filter;
		Filter.bRecursiveClasses = true;
		Filter.ClassNames.Add(BaseClass->GetFName());

		IAssetRegistry& AssetRegistry = FAssetRegistryModule::GetRegistry();
		FARFilter ExpandedFilter;
		AssetRegistry.ExpandRecursiveFilter(Filter, ExpandedFilter);
		TSet<FName> ChildClassNames(ExpandedFilter.ClassNames);

		FAssetPickerConfig AssetPickerConfig;
		{
			TSharedRef<FTemplateSequenceTrackEditor> TrackEditorRef = TrackEditor.ToSharedRef();

			AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(TrackEditorRef, &FTemplateSequenceTrackEditor::OnTemplateSequenceAssetSelected, ObjectBindings);
			AssetPickerConfig.OnAssetEnterPressed = FOnAssetEnterPressed::CreateSP(TrackEditorRef, &FTemplateSequenceTrackEditor::OnTemplateSequenceAssetEnterPressed, ObjectBindings);
			AssetPickerConfig.RefreshAssetViewDelegates.Add(&RefreshAssetViewDelegate);
			AssetPickerConfig.bAllowNullSelection = false;
			AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
			AssetPickerConfig.Filter.bRecursiveClasses = true;
			AssetPickerConfig.Filter.ClassNames.Add(UTemplateSequence::StaticClass()->GetFName());
			if (LegacyBaseClass != nullptr)
			{
				AssetPickerConfig.Filter.ClassNames.Add(LegacyBaseClass->GetFName());
			}

			AssetPickerConfig.OnShouldFilterAsset = FOnShouldFilterAsset::CreateLambda(
				[this, ChildClassNames](const FAssetData& AssetData) -> bool
				{
					if (LegacyBaseClass == nullptr || AssetData.AssetClass != LegacyBaseClass->GetFName())
					{
						const FAssetDataTagMapSharedView::FFindTagResult FoundBoundActorClass = AssetData.TagsAndValues.FindTag("BoundActorClass");
						if (FoundBoundActorClass.IsSet())
						{
							// Filter this out if it's got an incompatible bound actor class.
							const FName FoundBoundActorClassName(*FoundBoundActorClass.GetValue());
							return !ChildClassNames.Contains(FoundBoundActorClassName);
						}
						else
						{
							// Old asset, hasn't been saved since we added the bound actor class in the tags.
							++NumAssetsRequiringSave;
							// Don't filter if we're showing old assets, do filter if we only want compatible assets.
							return bShowingHiddenAssets ? false : true;
						}
					}
					else
					{
						// Legacy asset (e.g. UCameraAnim asset that has yet to be upgraded to a template sequence).
						++NumLegacyAssets;
						return bShowingHiddenAssets ? false : true;
					}
				});
		}

		FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		AssetPicker = ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig);

		ChildSlot
		[
			SNew(SBox)
			.WidthOverride(300.0f)
			.HeightOverride(300.f)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				[
					AssetPicker.ToSharedRef()
				]
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.VAlign(EVerticalAlignment::VAlign_Center)
					.FillWidth(1.f)
					[
						SNew(STextBlock)
						.Margin(FMargin(5.f))
						.TextStyle(FEditorStyle::Get(), "SmallText.Subdued")
						.Visibility(this, &STemplateSequenceAssetSubMenu::GetBottomRowVisibility)
						.Text_Lambda([this]()
							{
								return !bShowingHiddenAssets ? 
									FText::Format(LOCTEXT("OutdatedAssetsWarning", "Hiding {0} outdated assets"), FText::AsNumber(NumAssetsRequiringSave + NumLegacyAssets)) :
									LOCTEXT("OutdatedAssetsMessage", "Showing outdated assets");
							})
					]
					+SHorizontalBox::Slot()
					.VAlign(EVerticalAlignment::VAlign_Center)
					.AutoWidth()
					[
						SNew(SButton)
						.HAlign(EHorizontalAlignment::HAlign_Center)
						.VAlign(EVerticalAlignment::VAlign_Center)
						.TextStyle(FEditorStyle::Get(), "SmallText")
						.Visibility(this, &STemplateSequenceAssetSubMenu::GetBottomRowVisibility)
						.Text_Lambda([this]() { return !bShowingHiddenAssets ? 
								LOCTEXT("ShowOutdatedAssetsButton", "Show Outdated Assets") : 
								LOCTEXT("HideOutdatedAssetsButton", "Hide Outdated Assets"); })
						.ToolTipText(
							LOCTEXT("ShowAssetsRequiringSaveButtonTooltip", "Show or hide legacy assets (like CameraAnim) and template sequences that are potentially incompatible with the currently selected object binding (re-save those to fix the issue).")
							)
						.OnClicked(this, &STemplateSequenceAssetSubMenu::OnShowHiddenAssets)
					]
				]
			]
		];
	}

	EVisibility GetBottomRowVisibility() const
	{
		return (NumAssetsRequiringSave + NumLegacyAssets) > 0 ? EVisibility::Visible : EVisibility::Collapsed;
	}

	FReply OnShowHiddenAssets()
	{
		bShowingHiddenAssets = !bShowingHiddenAssets;
		NumLegacyAssets = 0;
		NumAssetsRequiringSave = 0;
		RefreshAssetViewDelegate.ExecuteIfBound(true);
		return FReply::Handled();
	}

private:
	TSharedPtr<FTemplateSequenceTrackEditor> TrackEditor;
	const UClass* BaseClass;
	const UClass* LegacyBaseClass;

	TSharedPtr<SWidget> AssetPicker;
	uint32 NumLegacyAssets = 0;
	uint32 NumAssetsRequiringSave = 0;
	bool bShowingHiddenAssets = false;
	FRefreshAssetViewDelegate RefreshAssetViewDelegate;
};

void FTemplateSequenceTrackEditor::AddTemplateSequenceAssetSubMenu(FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings, const UClass* RootBindingClass, bool bIsCameraAnimMenu)
{
	if (ObjectBindings.Num() > 0 && RootBindingClass != nullptr)
	{
		const UClass* LegacyAssetClass = bIsCameraAnimMenu ? UCameraAnim::StaticClass() : nullptr;
		TSharedPtr<STemplateSequenceAssetSubMenu> MenuEntry = SNew(STemplateSequenceAssetSubMenu, SharedThis(this), ObjectBindings, RootBindingClass, LegacyAssetClass);
		MenuBuilder.AddWidget(MenuEntry.ToSharedRef(), FText::GetEmpty(), true);
	}
}

TSharedRef<SWidget> FTemplateSequenceTrackEditor::BuildTemplateSequenceAssetSubMenu(FGuid ObjectBinding, const UClass* RootBindingClass, bool bIsCameraAnimMenu)
{
	check(RootBindingClass != nullptr);
	
	TArray<FGuid> ObjectBindings;
	ObjectBindings.Add(ObjectBinding);
	const UClass* LegacyAssetClass = bIsCameraAnimMenu ? UCameraAnim::StaticClass() : nullptr;
	return SNew(STemplateSequenceAssetSubMenu, SharedThis(this), ObjectBindings, RootBindingClass, LegacyAssetClass);
}

void FTemplateSequenceTrackEditor::OnTemplateSequenceAssetSelected(const FAssetData& AssetData, TArray<FGuid> ObjectBindings)
{
	FSlateApplication::Get().DismissAllMenus();

	if (UTemplateSequence* SelectedSequence = Cast<UTemplateSequence>(AssetData.GetAsset()))
	{
		const FScopedTransaction Transaction(LOCTEXT("AddTemplateSequence_Transaction", "Add Template Animation"));

		AnimatablePropertyChanged(FOnKeyProperty::CreateRaw(this, &FTemplateSequenceTrackEditor::AddKeyInternal, ObjectBindings, SelectedSequence));
	}
	else if (UCameraAnim* SelectedCameraAnim = Cast<UCameraAnim>(AssetData.GetAsset()))
	{
		TArray<TWeakObjectPtr<>> OutObjects;
		for (FGuid ObjectBinding : ObjectBindings)
		{
			for (TWeakObjectPtr<> Object : GetSequencer()->FindObjectsInCurrentSequence(ObjectBinding))
			{
				OutObjects.Add(Object);
			}
		}
		
		const FScopedTransaction Transaction(LOCTEXT("AddCameraAnim_Transaction", "Add Camera Anim"));

		AnimatablePropertyChanged(FOnKeyProperty::CreateRaw(this, &FTemplateSequenceTrackEditor::AddLegacyCameraAnimKeyInternal, OutObjects, SelectedCameraAnim));
	}
}

void FTemplateSequenceTrackEditor::OnTemplateSequenceAssetEnterPressed(const TArray<FAssetData>& AssetData, TArray<FGuid> ObjectBindings)
{
	if (AssetData.Num() > 0)
	{
		OnTemplateSequenceAssetSelected(AssetData[0].GetAsset(), ObjectBindings);
	}
}

void FTemplateSequenceTrackEditor::AddKey(const FGuid& ObjectGuid)
{
	const UClass* RootBindingClass = AcquireObjectClassFromObjectGuid(ObjectGuid);

	const UCameraComponent* CameraComponent = AcquireCameraComponentFromObjectGuid(ObjectGuid);
	const bool bIsCameraAnimMenu = (CameraComponent != nullptr);

	TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
	if (ParentWindow.IsValid() && RootBindingClass != nullptr)
	{
		TSharedPtr<SBox> MenuEntry = SNew(SBox)
			.WidthOverride(300.0f)
			.HeightOverride(300.f)
			[
				BuildTemplateSequenceAssetSubMenu(ObjectGuid, RootBindingClass, bIsCameraAnimMenu)
			];

		FMenuBuilder MenuBuilder(true, nullptr);
		MenuBuilder.AddWidget(MenuEntry.ToSharedRef(), FText::GetEmpty(), true);
		TSharedRef<SWidget> MenuContainer = MenuBuilder.MakeWidget();

		FSlateApplication::Get().PushMenu(
				ParentWindow.ToSharedRef(),
				FWidgetPath(),
				MenuContainer,
				FSlateApplication::Get().GetCursorPos(),
				FPopupTransitionEffect(FPopupTransitionEffect::TypeInPopup));
	}
}

FKeyPropertyResult FTemplateSequenceTrackEditor::AddKeyInternal(FFrameNumber KeyTime, FGuid ObjectBinding, UTemplateSequence* TemplateSequence)
{
	TArray<FGuid> ObjectBindings;
	ObjectBindings.Add(ObjectBinding);
	return AddKeyInternal(KeyTime, ObjectBindings, TemplateSequence);
}

FKeyPropertyResult FTemplateSequenceTrackEditor::AddKeyInternal(FFrameNumber KeyTime, TArray<FGuid> ObjectBindings, UTemplateSequence* TemplateSequence)
{
	FKeyPropertyResult KeyPropertyResult;

	if (TemplateSequence->GetMovieScene()->GetPlaybackRange().IsEmpty())
	{
		FNotificationInfo Info(FText::Format(LOCTEXT("InvalidSequenceDuration", "Invalid template sequence {0}. The template sequence has no duration."), TemplateSequence->GetDisplayName()));
		Info.bUseLargeFont = false;
		FSlateNotificationManager::Get().AddNotification(Info);
		return KeyPropertyResult;
	}

	if (!CanAddSubSequence(*TemplateSequence))
	{
		FNotificationInfo Info(FText::Format(LOCTEXT("InvalidSequenceCycle", "Invalid template sequence {0}. There could be a circular dependency."), TemplateSequence->GetDisplayName()));
		Info.bUseLargeFont = false;
		FSlateNotificationManager::Get().AddNotification(Info);
		return KeyPropertyResult;
	}

	TArray<UMovieSceneSection*> NewSections;
	TSharedPtr<ISequencer> SequencerPtr = GetSequencer();
	if (SequencerPtr.IsValid())
	{
		const FFrameRate OuterTickResolution = SequencerPtr->GetFocusedTickResolution();
		const FFrameRate SubTickResolution = TemplateSequence->GetMovieScene()->GetTickResolution();
		if (SubTickResolution != OuterTickResolution)
		{
			FNotificationInfo Info(FText::Format(LOCTEXT("TickResolutionMismatch", "The parent sequence has a different tick resolution {0} than the newly added sequence {1}"), OuterTickResolution.ToPrettyText(), SubTickResolution.ToPrettyText()));
			Info.bUseLargeFont = false;
			FSlateNotificationManager::Get().AddNotification(Info);
		}

		for (const FGuid& ObjectBindingGuid : ObjectBindings)
		{
			if (ObjectBindingGuid.IsValid())
			{
				FFindOrCreateTrackResult TrackResult = FindOrCreateTrackForObject(ObjectBindingGuid, UTemplateSequenceTrack::StaticClass());
				UMovieSceneTrack* Track = TrackResult.Track;
				KeyPropertyResult.bTrackCreated |= TrackResult.bWasCreated;

				if (ensure(Track))
				{
					UMovieSceneSection* NewSection = Cast<UTemplateSequenceTrack>(Track)->AddNewTemplateSequenceSection(KeyTime, TemplateSequence);
					KeyPropertyResult.bTrackModified = true;
					NewSections.Add(NewSection);
				}
			}
		}
	}

	if (NewSections.Num() > 0)
	{
		GetSequencer()->EmptySelection();
		for (UMovieSceneSection* NewSection : NewSections)
		{
			GetSequencer()->SelectSection(NewSection);
		}
		GetSequencer()->ThrobSectionSelection();
	}

	return KeyPropertyResult;
}

FKeyPropertyResult FTemplateSequenceTrackEditor::AddLegacyCameraAnimKeyInternal(FFrameNumber KeyTime, const TArray<TWeakObjectPtr<UObject>> Objects, UCameraAnim* CameraAnim)
{
	return FCameraAnimTrackEditorHelper::AddCameraAnimKey(*this, KeyTime, Objects, CameraAnim);
}

bool FTemplateSequenceTrackEditor::CanAddSubSequence(const UMovieSceneSequence& Sequence) const
{
	UMovieSceneSequence* FocusedSequence = GetSequencer()->GetFocusedMovieSceneSequence();
	return FSubTrackEditorUtil::CanAddSubSequence(FocusedSequence, Sequence);
}

const UClass* FTemplateSequenceTrackEditor::AcquireObjectClassFromObjectGuid(const FGuid& Guid)
{
	if (!Guid.IsValid())
	{
		return nullptr;
	}

	TSharedPtr<ISequencer> SequencerPtr = GetSequencer();
	if (!SequencerPtr.IsValid() || SequencerPtr->GetFocusedMovieSceneSequence() == nullptr ||
			SequencerPtr->GetFocusedMovieSceneSequence()->GetMovieScene() == nullptr)
	{
		return nullptr;
	}

	UMovieScene* FocusedMovieScene = SequencerPtr->GetFocusedMovieSceneSequence()->GetMovieScene();
	const FMovieSceneBinding* ObjectBinding = FocusedMovieScene->FindBinding(Guid);
	if (ObjectBinding == nullptr)
	{
		return nullptr;
	}

	const UClass* ObjectClass = nullptr;
	if (FMovieScenePossessable* Possessable = FocusedMovieScene->FindPossessable(Guid))
	{
		ObjectClass = Possessable->GetPossessedObjectClass();
	}
	else if (FMovieSceneSpawnable* Spawnable = FocusedMovieScene->FindSpawnable(Guid))
	{
		if (Spawnable->GetObjectTemplate() != nullptr)
		{
			ObjectClass = Spawnable->GetObjectTemplate()->GetClass();
		}
	}
	return ObjectClass;
}

UCameraComponent* FTemplateSequenceTrackEditor::AcquireCameraComponentFromObjectGuid(const FGuid& Guid)
{
	USkeleton* Skeleton = nullptr;
	for (TWeakObjectPtr<> WeakObject : GetSequencer()->FindObjectsInCurrentSequence(Guid))
	{
		UObject* const Obj = WeakObject.Get();
	
		if (AActor* const Actor = Cast<AActor>(Obj))
		{
			UCameraComponent* const CameraComp = MovieSceneHelpers::CameraComponentFromActor(Actor);
			if (CameraComp)
			{
				return CameraComp;
			}
		}
		else if (UCameraComponent* const CameraComp = Cast<UCameraComponent>(Obj))
		{
			if (CameraComp->IsActive())
			{
				return CameraComp;
			}
		}
	}

	return nullptr;
}

FTemplateSequenceSection::FTemplateSequenceSection(TSharedPtr<ISequencer> InSequencer, UTemplateSequenceSection& InSection)
	: TSubSectionMixin(InSequencer, InSection)
{
}

#undef LOCTEXT_NAMESPACE
