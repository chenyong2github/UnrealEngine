// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayTagContainerCustomization.h"
#include "Widgets/Input/SComboButton.h"

#include "Widgets/Input/SButton.h"


#include "Editor.h"
#include "PropertyHandle.h"
#include "DetailWidgetRow.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SHyperlink.h"
#include "EditorFontGlyphs.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "GameplayTagContainerCustomization"

void FGameplayTagContainerCustomization::CustomizeHeader(TSharedRef<class IPropertyHandle> InStructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructPropertyHandle = InStructPropertyHandle;

	FSimpleDelegate OnTagContainerChanged = FSimpleDelegate::CreateSP(this, &FGameplayTagContainerCustomization::RefreshTagList);
	StructPropertyHandle->SetOnPropertyValueChanged(OnTagContainerChanged);

	OnObjectPostEditChangeHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &FGameplayTagContainerCustomization::OnObjectPostEditChange);

	BuildEditableContainerList();

	FUIAction SearchForReferencesAction(FExecuteAction::CreateSP(this, &FGameplayTagContainerCustomization::OnWholeContainerSearchForReferences));

	HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MaxDesiredWidth(512)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(EditButton, SComboButton)
					.OnGetMenuContent(this, &FGameplayTagContainerCustomization::GetListContent)
					.OnMenuOpenChanged(this, &FGameplayTagContainerCustomization::OnGameplayTagListMenuOpenStateChanged)
					.ContentPadding(FMargin(2.0f, 2.0f))
					.MenuPlacement(MenuPlacement_BelowAnchor)
					.ButtonContent()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("GameplayTagContainerCustomization_Edit", "Edit..."))
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SButton)
					.IsEnabled(!StructPropertyHandle->IsEditConst())
					.Text(LOCTEXT("GameplayTagContainerCustomization_Clear", "Clear All"))
					.OnClicked(this, &FGameplayTagContainerCustomization::OnClearAllButtonClicked)
					.Visibility(this, &FGameplayTagContainerCustomization::GetClearAllVisibility)
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBorder)
				.Padding(4.0f)
				.Visibility(this, &FGameplayTagContainerCustomization::GetTagsListVisibility)
				[
					ActiveTags()
				]
			]
		]
		.AddCustomContextMenuAction(SearchForReferencesAction,
			LOCTEXT("WholeContainerSearchForReferences", "Search For References"),
			LOCTEXT("WholeContainerSearchForReferencesTooltip", "Find referencers that reference *any* of the tags in this container"),
			FSlateIcon());

	GEditor->RegisterForUndo(this);
}

TSharedRef<SWidget> FGameplayTagContainerCustomization::ActiveTags()
{	
	RefreshTagList();
	
	SAssignNew( TagListView, SListView<TSharedPtr<FGameplayTag>> )
	.ListItemsSource(&TagList)
	.SelectionMode(ESelectionMode::None)
	.OnGenerateRow(this, &FGameplayTagContainerCustomization::MakeListViewWidget);

	return TagListView->AsShared();
}

void FGameplayTagContainerCustomization::RefreshTagList()
{
	// Rebuild Editable Containers as container references can become unsafe
	BuildEditableContainerList();

	// Build the set of tags on any instance, collapsing common tags together
	TSet<FGameplayTag> CurrentTagSet;
	for (int32 ContainerIdx = 0; ContainerIdx < EditableContainers.Num(); ++ContainerIdx)
	{
		if (const FGameplayTagContainer* Container = EditableContainers[ContainerIdx].TagContainer)
		{
			for (auto It = Container->CreateConstIterator(); It; ++It)
			{
				CurrentTagSet.Add(*It);
			}
		}
	}

	// Convert the set into pointers for the combo
	TagList.Empty(CurrentTagSet.Num());
	for (const FGameplayTag& CurrentTag : CurrentTagSet)
	{
		TagList.Add(MakeShared<FGameplayTag>(CurrentTag));
	}
	TagList.StableSort([](const TSharedPtr<FGameplayTag>& One, const TSharedPtr<FGameplayTag>& Two)
	{
		return *One < *Two;
	});

	// Refresh the slate list
	if( TagListView.IsValid() )
	{
		TagListView->RequestListRefresh();
	}
}

TSharedRef<ITableRow> FGameplayTagContainerCustomization::MakeListViewWidget(TSharedPtr<FGameplayTag> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	TSharedPtr<SWidget> TagItem;

	const FString TagName = Item->ToString();
	if (UGameplayTagsManager::Get().ShowGameplayTagAsHyperLinkEditor(TagName))
	{
		TagItem = SNew(SHyperlink)
			.Text(FText::FromString(TagName))
			.OnNavigate(this, &FGameplayTagContainerCustomization::OnTagDoubleClicked, *Item.Get());
	}
	else
	{
		TagItem = SNew(STextBlock)
			.Text(FText::FromString(TagName));
	}

	return SNew( STableRow< TSharedPtr<FString> >, OwnerTable )
	[
		SNew(SBorder)
		.OnMouseButtonDown(this, &FGameplayTagContainerCustomization::OnSingleTagMouseButtonPressed, TagName)
		.Padding(0.0f)
		.BorderImage(FEditorStyle::GetBrush("NoBorder"))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0,0,2,0)
			[
				SNew(SButton)
				.IsEnabled(!StructPropertyHandle->IsEditConst())
				.ContentPadding(FMargin(0))
				.ButtonStyle(FEditorStyle::Get(), "FlatButton.Danger")
				.ForegroundColor(FSlateColor::UseForeground())
				.OnClicked(this, &FGameplayTagContainerCustomization::OnRemoveTagClicked, *Item.Get())
				[
					SNew(STextBlock)
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.9"))
					.Text(FEditorFontGlyphs::Times)
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				TagItem.ToSharedRef()
			]
		]
	];
}

FReply FGameplayTagContainerCustomization::OnSingleTagMouseButtonPressed(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, FString TagName)
{
	if (MouseEvent.IsMouseButtonDown(EKeys::RightMouseButton))
	{
		FMenuBuilder MenuBuilder(/*bShouldCloseWindowAfterMenuSelection=*/ true, /*CommandList=*/ nullptr);

		FUIAction SearchForReferencesAction(FExecuteAction::CreateSP(this, &FGameplayTagContainerCustomization::OnSingleTagSearchForReferences, TagName));

		MenuBuilder.BeginSection(NAME_None, FText::Format(LOCTEXT("SingleTagMenuHeading", "Tag Actions ({0})"), FText::AsCultureInvariant(TagName)));
		MenuBuilder.AddMenuEntry(
			LOCTEXT("SingleTagSearchForReferences", "Search For References"),
			FText::Format(LOCTEXT("SingleTagSearchForReferencesTooltip", "Find references to the tag {0}"), FText::AsCultureInvariant(TagName)),
			FSlateIcon(),
			SearchForReferencesAction);
		MenuBuilder.EndSection();

		// Spawn context menu
		FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
		FSlateApplication::Get().PushMenu(TagListView.ToSharedRef(), WidgetPath, MenuBuilder.MakeWidget(), MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void FGameplayTagContainerCustomization::OnSingleTagSearchForReferences(FString TagName)
{
	FName TagFName(*TagName, FNAME_Find);
	if (FEditorDelegates::OnOpenReferenceViewer.IsBound() && !TagFName.IsNone())
	{
		TArray<FAssetIdentifier> AssetIdentifiers;
		AssetIdentifiers.Emplace(FGameplayTag::StaticStruct(), TagFName);
		FEditorDelegates::OnOpenReferenceViewer.Broadcast(AssetIdentifiers, FReferenceViewerParams());
	}
}

void FGameplayTagContainerCustomization::OnWholeContainerSearchForReferences()
{
	if (FEditorDelegates::OnOpenReferenceViewer.IsBound())
	{
		TArray<FAssetIdentifier> AssetIdentifiers;
		AssetIdentifiers.Reserve(TagList.Num());
		for (const TSharedPtr<FGameplayTag>& TagPtr : TagList)
		{
			if (TagPtr->IsValid())
			{
				AssetIdentifiers.Emplace(FGameplayTag::StaticStruct(), TagPtr->GetTagName());
			}
		}

		FEditorDelegates::OnOpenReferenceViewer.Broadcast(AssetIdentifiers, FReferenceViewerParams());
	}
}

void FGameplayTagContainerCustomization::OnTagDoubleClicked(FGameplayTag Tag)
{
	UGameplayTagsManager::Get().NotifyGameplayTagDoubleClickedEditor(Tag.ToString());
}

FReply FGameplayTagContainerCustomization::OnRemoveTagClicked(FGameplayTag Tag)
{
	TArray<FString> NewValues;
	for (int32 ContainerIdx = 0; ContainerIdx < EditableContainers.Num(); ++ContainerIdx)
	{
		FGameplayTagContainer TagContainerCopy;
		if (const FGameplayTagContainer* Container = EditableContainers[ContainerIdx].TagContainer)
		{
			TagContainerCopy = *Container;
		}
		TagContainerCopy.RemoveTag(Tag);

		NewValues.Add(TagContainerCopy.ToString());
	}

	{
		FScopedTransaction Transaction(LOCTEXT("RemoveGameplayTagFromContainer", "Remove Gameplay Tag"));
		StructPropertyHandle->SetPerObjectValues(NewValues);
	}

	RefreshTagList();

	return FReply::Handled();
}

void FGameplayTagContainerCustomization::OnObjectPostEditChange(class UObject* Object, FPropertyChangedEvent& PropertyChangedEvent)
{
	if (StructPropertyHandle.IsValid() && StructPropertyHandle->GetProperty() && StructPropertyHandle->GetProperty()->GetFName() == PropertyChangedEvent.GetPropertyName())
	{
		RefreshTagList();
	}
}

TSharedRef<SWidget> FGameplayTagContainerCustomization::GetListContent()
{
	if (!StructPropertyHandle.IsValid() || StructPropertyHandle->GetProperty() == nullptr)
	{
		return SNullWidget::NullWidget;
	}

	FString Categories = UGameplayTagsManager::Get().GetCategoriesMetaFromPropertyHandle(StructPropertyHandle);

	bool bReadOnly = StructPropertyHandle->IsEditConst();

	TSharedRef<SGameplayTagWidget> TagWidget = SNew(SGameplayTagWidget, EditableContainers)
		.Filter(Categories)
		.ReadOnly(bReadOnly)
		.TagContainerName(StructPropertyHandle->GetPropertyDisplayName().ToString())
		.OnTagChanged(this, &FGameplayTagContainerCustomization::RefreshTagList)
		.PropertyHandle(StructPropertyHandle);

	LastTagWidget = TagWidget;

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.MaxHeight(400)
		[
			TagWidget
		];
}

void FGameplayTagContainerCustomization::OnGameplayTagListMenuOpenStateChanged(bool bIsOpened)
{
	if (bIsOpened)
	{
		TSharedPtr<SGameplayTagWidget> TagWidget = LastTagWidget.Pin();
		if (TagWidget.IsValid())
		{
			EditButton->SetMenuContentWidgetToFocus(TagWidget->GetWidgetToFocusOnOpen());
		}
	}
}

FReply FGameplayTagContainerCustomization::OnClearAllButtonClicked()
{
	{
		FScopedTransaction Transaction(LOCTEXT("GameplayTagContainerCustomization_RemoveAllTags", "Remove All Gameplay Tags"));
		StructPropertyHandle->SetValueFromFormattedString(FGameplayTagContainer().ToString());
	}

	RefreshTagList();

	return FReply::Handled();
}

EVisibility FGameplayTagContainerCustomization::GetClearAllVisibility() const
{
	return TagList.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility FGameplayTagContainerCustomization::GetTagsListVisibility() const
{
	return TagList.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
}

void FGameplayTagContainerCustomization::PostUndo( bool bSuccess )
{
	if( bSuccess )
	{
		RefreshTagList();
	}
}

void FGameplayTagContainerCustomization::PostRedo( bool bSuccess )
{
	if( bSuccess )
	{
		RefreshTagList();
	}
}

FGameplayTagContainerCustomization::~FGameplayTagContainerCustomization()
{
	GEditor->UnregisterForUndo(this);
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(OnObjectPostEditChangeHandle);
	OnObjectPostEditChangeHandle.Reset();

}

void FGameplayTagContainerCustomization::BuildEditableContainerList()
{
	EditableContainers.Empty();

	if( StructPropertyHandle.IsValid() )
	{
		TArray<void*> RawStructData;
		StructPropertyHandle->AccessRawData(RawStructData);

		for (int32 ContainerIdx = 0; ContainerIdx < RawStructData.Num(); ++ContainerIdx)
		{
			EditableContainers.Add(SGameplayTagWidget::FEditableGameplayTagContainerDatum(nullptr, (FGameplayTagContainer*)RawStructData[ContainerIdx]));
		}
	}	
}

#undef LOCTEXT_NAMESPACE
