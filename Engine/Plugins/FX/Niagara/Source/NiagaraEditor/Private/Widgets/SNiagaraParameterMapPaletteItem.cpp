// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraParameterMapPaletteItem.h"
#include "EdGraphSchema_Niagara.h"
#include "EditorFontGlyphs.h"
#include "EditorStyleSet.h"
#include "NiagaraActions.h"
#include "NiagaraEditorSettings.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraGraph.h"
#include "TutorialMetaData.h"
#include "Widgets/SNiagaraParameterMapView.h"
#include "Widgets/SNiagaraParameterName.h"
#include "Widgets/Input/SComboButton.h"

#define LOCTEXT_NAMESPACE "NiagaraParameterMapPalleteItem"

void SNiagaraParameterMapPalleteItem::Construct(const FArguments& InArgs, FCreateWidgetForActionData* const InCreateData )
{
	this->OnItemRenamed = InArgs._OnItemRenamed;

	TAttribute<bool> bIsReadOnly = false;
	TAttribute<bool> bIsEditingEnabled = true;

	check(InCreateData->Action.IsValid());
	TSharedPtr<FNiagaraParameterAction> ParameterAction = StaticCastSharedPtr<FNiagaraParameterAction>(InCreateData->Action);
	ActionPtr = InCreateData->Action;

	FTutorialMetaData TagMeta("PaletteItem");

	const FLinearColor TypeColor = UEdGraphSchema_Niagara::GetTypeColor(ParameterAction->GetParameter().GetType());
	FSlateBrush const* IconBrush = FEditorStyle::GetBrush(TEXT("Kismet.AllClasses.VariableIcon"));
	FSlateBrush const* SecondaryBrush = FEditorStyle::GetBrush(TEXT("NoBrush"));
	FSlateColor        IconColor = FSlateColor(TypeColor);
	FSlateColor        SecondaryIconColor = IconColor;
	FText			   IconToolTip = FText::GetEmpty();
	FString			   IconDocLink, IconDocExcerpt;
	TSharedRef<SWidget> IconWidget = CreateIconWidget(IconToolTip, IconBrush, IconColor, IconDocLink, IconDocExcerpt, SecondaryBrush, SecondaryIconColor);
	IconWidget->SetEnabled(bIsEditingEnabled);

	static const FName BoldFontName = FName("Bold");
	static const FName ItalicFontName = FName("Italic");
	const FName FontType = ItalicFontName;
	FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(FontType, 10);

	if (InCreateData->bHandleMouseButtonDown)
	{
		MouseButtonDownDelegate = InCreateData->MouseButtonDownDelegate;
	}

	TAttribute<FText> ParameterToolTipText;
	ParameterToolTipText.Bind(this, &SNiagaraParameterMapPalleteItem::GetItemTooltip);
	SetToolTipText(ParameterToolTipText);

	TArray<FName> Namespaces;
	NiagaraParameterMapSectionID::OnGetSectionNamespaces((NiagaraParameterMapSectionID::Type)InCreateData->Action->SectionID, Namespaces);
	FNiagaraNamespaceMetadata NamespaceMetadata = GetDefault<UNiagaraEditorSettings>()->GetMetaDataForNamespaces(Namespaces);
	bool bForceReadOnly = NamespaceMetadata.IsValid() == false || NamespaceMetadata.Options.Contains(ENiagaraNamespaceMetadataOptions::PreventEditingName);

	ParameterNameTextBlock = SNew(SNiagaraParameterNameTextBlock)
		.ParameterText(FText::FromName(ParameterAction->GetParameter().GetName()))
		.HighlightText(InCreateData->HighlightText)
		.OnTextCommitted(this, &SNiagaraParameterMapPalleteItem::OnNameTextCommitted)
		.OnVerifyTextChanged(this, &SNiagaraParameterMapPalleteItem::OnNameTextVerifyChanged)
		.IsSelected(InCreateData->IsRowSelectedDelegate)
		.IsReadOnly(InCreateData->bIsReadOnly || bForceReadOnly || ParameterAction->GetIsExternallyReferenced())
		.Decorator()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 5, 0)
			[
				SNew(STextBlock)
				.Visibility(ParameterAction->GetIsExternallyReferenced() ? EVisibility::Visible : EVisibility::Collapsed)
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.8"))
				.Text(FEditorFontGlyphs::Lock)
				.ToolTipText(LOCTEXT("LockedToolTip", "This parameter is used in a referenced external graph and can't be edited directly."))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 5, 0)
			[
				SNew(STextBlock)
				.Visibility(ParameterAction->GetIsSourcedFromCustomStackContext() ? EVisibility::Visible : EVisibility::Collapsed)
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.8"))
				.Text(FEditorFontGlyphs::Database)
				.ToolTipText(LOCTEXT("DataInterfaceSourceToolTip", "This parameter is a child variable of an existing Data Interface, meant to be used in Simulation Stage based stacks where the parent Data Interface is the Iteration Source.") )
			]
		];

	InCreateData->OnRenameRequest->BindSP(ParameterNameTextBlock.ToSharedRef(), &SNiagaraParameterNameTextBlock::EnterEditingMode);

	// now, create the actual widget
	ChildSlot
	[
		SNew(SHorizontalBox)
		.AddMetaData<FTutorialMetaData>(TagMeta)
		// icon slot
		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			IconWidget
		]
		// name slot
		+SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.Padding(5,0)
		[
			ParameterNameTextBlock.ToSharedRef()
		]
		// reference count
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(3, 0)
		[
			SNew(SComboButton)
			.HasDownArrow(false)
			.ButtonStyle(FEditorStyle::Get(), "RoundButton")
			.ForegroundColor(FSlateColor::UseForeground())
			.ContentPadding(FMargin(2.0f))
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text(this, &SNiagaraParameterMapPalleteItem::GetReferenceCount)
				.Font(Font)
			]
		]

	];
}

void SNiagaraParameterMapPalleteItem::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SGraphPaletteItem::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (ParameterNameTextBlock.IsValid())
	{
		TSharedPtr<FNiagaraParameterAction> ParameterAction = StaticCastSharedPtr<FNiagaraParameterAction>(ActionPtr.Pin());
		if (ParameterAction.IsValid() && ParameterAction->GetIsNamespaceModifierRenamePending())
		{
			ParameterAction->SetIsNamespaceModifierRenamePending(false);
			ParameterNameTextBlock->EnterNamespaceModifierEditingMode();
		}
	}
}

FText SNiagaraParameterMapPalleteItem::GetItemTooltip() const
{
	return SGraphPaletteItem::GetItemTooltip();
}

void SNiagaraParameterMapPalleteItem::OnNameTextCommitted(const FText& NewText, ETextCommit::Type InTextCommit)
{
	TSharedPtr<FNiagaraParameterAction> ParameterAction = StaticCastSharedPtr<FNiagaraParameterAction>(ActionPtr.Pin());
	if (ParameterAction.IsValid())
	{
		OnItemRenamed.ExecuteIfBound(NewText, ParameterAction.ToSharedRef());
	}
}

FText SNiagaraParameterMapPalleteItem::GetReferenceCount() const
{
	TSharedPtr<FNiagaraParameterAction> ParameterAction = StaticCastSharedPtr<FNiagaraParameterAction>(ActionPtr.Pin());
	if (ParameterAction.IsValid())
	{
		int32 TotalCount = 0;
		for (const FNiagaraGraphParameterReferenceCollection& ReferenceCollection : ParameterAction->GetReferenceCollection())
		{
			for (const FNiagaraGraphParameterReference& ParamReference : ReferenceCollection.ParameterReferences)
			{
				if (ParamReference.bIsUserFacing)
				{
					TotalCount++;
				}
			}
		}
		return FText::AsNumber(TotalCount);
	}
	return FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE // "SNiagaraParameterMapPalleteItem"