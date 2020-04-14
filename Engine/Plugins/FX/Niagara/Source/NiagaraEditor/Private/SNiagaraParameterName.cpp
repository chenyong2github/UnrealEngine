// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SNiagaraParameterName.h"
#include "NiagaraEditorSettings.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraNodeParameterMapBase.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"

void SNiagaraParameterName::Construct(const FArguments& InArgs)
{
	EditableTextStyle = InArgs._EditableTextStyle;
	ReadOnlyTextStyle = InArgs._ReadOnlyTextStyle;
	ParameterName = InArgs._ParameterName;
	bIsReadOnly = InArgs._IsReadOnly;
	HighlightText = InArgs._HighlightText;
	OnVerifyNameChangeDelegate = InArgs._OnVerifyNameChange;
	OnNameChangedDelegate = InArgs._OnNameChanged;
	OnDoubleClickedDelegate = InArgs._OnDoubleClicked;
	IsSelected = InArgs._IsSelected;
	DecoratorHAlign = InArgs._DecoratorHAlign;
	Decorator = InArgs._Decorator.Widget;

	UpdateContent(ParameterName.Get());
}

void SNiagaraParameterName::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	FName CurrentParameterName = ParameterName.Get();
	if (DisplayedParameterName != CurrentParameterName)
	{
		UpdateContent(CurrentParameterName);
	}
}

FReply SNiagaraParameterName::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	if (OnDoubleClickedDelegate.IsBound())
	{
		return OnDoubleClickedDelegate.Execute(InMyGeometry, InMouseEvent);
	}
	else
	{
		return SCompoundWidget::OnMouseButtonDoubleClick(InMyGeometry, InMouseEvent);
	}
}

TSharedRef<SBorder> SNiagaraParameterName::CreateNamespaceWidget(FText NamespaceDisplayName, FText NamespaceDescription, FLinearColor NamespaceBorderColor, FName NamespaceForegroundStyle)
{
	return SNew(SBorder)
	.BorderImage(FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.ParameterName.NamespaceBorder"))
	.BorderBackgroundColor(NamespaceBorderColor)
	.ToolTipText(NamespaceDescription)
	.VAlign(VAlign_Center)
	.Padding(FMargin(5.0f, 1.0f, 5.0f, 1.0f))
	[
		SNew(STextBlock)
		.TextStyle(FNiagaraEditorStyle::Get(), NamespaceForegroundStyle)
		.Text(NamespaceDisplayName)
		.HighlightText(HighlightText)
	];
}

void SNiagaraParameterName::UpdateContent(FName InDisplayedParameterName)
{
	DisplayedParameterName = InDisplayedParameterName;

	FString DisplayedParameterNameString = DisplayedParameterName.ToString();
	TArray<FString> NamePartStrings;
	DisplayedParameterNameString.ParseIntoArray(NamePartStrings, TEXT("."));

	if (NamePartStrings.Num() == 0)
	{
		return;
	}

	TArray<FName> NameParts;
	for (int32 i = 0; i < NamePartStrings.Num(); i++)
	{
		NameParts.Add(*NamePartStrings[i]);
	}

	TSharedRef<SHorizontalBox> ContentBox = SNew(SHorizontalBox);

	// Add the namespace widget.
	FNiagaraNamespaceMetadata DefaultNamespaceMetadata = GetDefault<UNiagaraEditorSettings>()->GetMetaDataForNamespaces({ NAME_None });
	FNiagaraNamespaceMetadata NamespaceMetadata = GetDefault<UNiagaraEditorSettings>()->GetMetaDataForNamespaces(NameParts);
	TSharedPtr<SWidget> NamespaceWidget;
	if (NamespaceMetadata.IsValid())
	{
		NameParts.RemoveAt(0, NamespaceMetadata.Namespaces.Num());
		NamespaceWidget = CreateNamespaceWidget(
			NamespaceMetadata.DisplayName.ToUpper(), NamespaceMetadata.Description,
			NamespaceMetadata.BackgroundColor, NamespaceMetadata.ForegroundStyle);
	}
	else
	{
		FText NamespaceDisplayName = FText::FromString(FName::NameToDisplayString(NameParts[0].ToString(), false).ToUpper());
		NameParts.RemoveAt(0);
		NamespaceWidget = CreateNamespaceWidget(
			NamespaceDisplayName, DefaultNamespaceMetadata.Description,
			DefaultNamespaceMetadata.BackgroundColor, DefaultNamespaceMetadata.ForegroundStyle);
	}

	ContentBox->AddSlot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		.Padding(0.0f, 0.0f, 5.0f, 0.0f)
		[
			NamespaceWidget.ToSharedRef()
		];

	// Next the namespace modifier widget if there is a namespace modifier.
	if (NameParts.Num() > 1)
	{
		DisplayedNamespaceModifier = NameParts[0];
		FNiagaraNamespaceMetadata DisplayedNamespaceModifierMetadata = GetDefault<UNiagaraEditorSettings>()->GetMetaDataForNamespaceModifier(DisplayedNamespaceModifier);
		if (DisplayedNamespaceModifierMetadata.IsValid() == false)
		{
			DisplayedNamespaceModifierMetadata = DefaultNamespaceMetadata;
		}

		NameParts.RemoveAt(0);
		NamespaceModifierBorder = CreateNamespaceWidget(
			FText::FromString(FName::NameToDisplayString(DisplayedNamespaceModifier.ToString(), false).ToUpper()), DisplayedNamespaceModifierMetadata.Description,
			DisplayedNamespaceModifierMetadata.BackgroundColor, DisplayedNamespaceModifierMetadata.ForegroundStyle);

		ContentBox->AddSlot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(0.0f, 0.0f, 5.0f, 0.0f)
			[
				NamespaceModifierBorder.ToSharedRef()
			];
	}
	else
	{
		DisplayedNamespaceModifier = NAME_None;
		NamespaceModifierBorder.Reset();
	}

	// If there are extra namespaces found, add them to the UI without metadata.
	while (NameParts.Num() > 1)
	{
		FName ExtraNamespace = NameParts[0];
		NameParts.RemoveAt(0);

		ContentBox->AddSlot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(0.0f, 0.0f, 5.0f, 0.0f)
			[
				CreateNamespaceWidget(
					FText::FromString(FName::NameToDisplayString(ExtraNamespace.ToString(), false).ToUpper()), DefaultNamespaceMetadata.Description,
					DefaultNamespaceMetadata.BackgroundColor, DefaultNamespaceMetadata.ForegroundStyle)
			];
	}

	TSharedPtr<SWidget> NameWidget;
	if (NameParts.Num() > 0)
	{
		if (bIsReadOnly)
		{
			NameWidget = SNew(STextBlock)
				.TextStyle(ReadOnlyTextStyle)
				.Text(FText::FromName(NameParts[0]))
				.HighlightText(HighlightText);
		}
		else
		{
			NameWidget = SAssignNew(EditableTextBlock, SInlineEditableTextBlock)
				.Style(EditableTextStyle)
				.Text(FText::FromName(NameParts[0]))
				.IsSelected(IsSelected)
				.OnVerifyTextChanged(this, &SNiagaraParameterName::VerifyNameTextChange)
				.OnTextCommitted(this, &SNiagaraParameterName::NameTextCommitted)
				.HighlightText(HighlightText);
		}
	}

	if (NameWidget.IsValid())
	{
		ContentBox->AddSlot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			NameWidget.ToSharedRef()
		];
	}

	if (Decorator.IsValid())
	{
		ContentBox->AddSlot()
		.VAlign(VAlign_Center)
		.HAlign(DecoratorHAlign)
		.Padding(3.0f, 0.0f, 0.0f, 0.0f)
		[
			Decorator.ToSharedRef()
		];
	}

	ChildSlot
	[
		ContentBox
	];
}

FName SNiagaraParameterName::ReconstructNameFromEditText(const FText& InEditText)
{
	FString CurrentParameterNameString = ParameterName.Get().ToString();
	TArray<FString> NameParts;
	CurrentParameterNameString.ParseIntoArray(NameParts, TEXT("."));

	NameParts[NameParts.Num() - 1] = InEditText.ToString().Replace(TEXT("."), TEXT("_"));
	FString NewParameterNameString = FString::Join(NameParts, TEXT("."));
	return *NewParameterNameString;
}

FName SNiagaraParameterName::ReconstructNameFromNamespaceModifierEditText(const FText& InEditText)
{
	FString CurrentParameterNameString = ParameterName.Get().ToString();
	TArray<FString> NameParts;
	CurrentParameterNameString.ParseIntoArray(NameParts, TEXT("."));

	TArray<FName> Namespaces;
	for (int32 NamePartIndex = 0; NamePartIndex < NameParts.Num() - 1; NamePartIndex++)
	{
		Namespaces.Add(*NameParts[NamePartIndex]);
	}

	FNiagaraNamespaceMetadata NamespaceMetadata = GetDefault<UNiagaraEditorSettings>()->GetMetaDataForNamespaces(Namespaces);

	if (NamespaceMetadata.IsValid())
	{
		if (NameParts.Num() > NamespaceMetadata.Namespaces.Num() + 1)
		{
			NameParts[NamespaceMetadata.Namespaces.Num()] = InEditText.ToString().Replace(TEXT("."), TEXT("_"));
		}
		FString NewParameterNameString = FString::Join(NameParts, TEXT("."));
		return *NewParameterNameString;
	}
	else
	{
		// If there isn't valid namespace metadata for this parameter we can't safely edit the modifier
		// since there is not way to know how many parts the namespace actually has.
		return ParameterName.Get();
	}
}

FText SNiagaraParameterName::GetNamespaceModifierText()
{
	FString CurrentParameterNameString = ParameterName.Get().ToString();
	TArray<FString> NameParts;
	CurrentParameterNameString.ParseIntoArray(NameParts, TEXT("."));
	return NameParts.Num() >= 3 
		? FText::FromString(NameParts[1]) 
		: FText::FromName(NAME_None);
}

bool SNiagaraParameterName::VerifyNameTextChange(const FText& InNewNameText, FText& OutErrorMessage)
{
	FName NewParameterName = ReconstructNameFromEditText(InNewNameText);
	if (OnVerifyNameChangeDelegate.IsBound())
	{
		return OnVerifyNameChangeDelegate.Execute(NewParameterName, OutErrorMessage);
	}
	return true;
}

void SNiagaraParameterName::NameTextCommitted(const FText& InNewNameText, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnEnter)
	{
		FName NewParameterName = ReconstructNameFromEditText(InNewNameText);
		OnNameChangedDelegate.ExecuteIfBound(NewParameterName);
	}
}

bool SNiagaraParameterName::VerifyNamespaceModifierTextChange(const FText& InNewNameText, FText& OutErrorMessage)
{
	FName NewParameterName = ReconstructNameFromNamespaceModifierEditText(InNewNameText);
	if (OnVerifyNameChangeDelegate.IsBound())
	{
		return OnVerifyNameChangeDelegate.Execute(NewParameterName, OutErrorMessage);
	}
	return true;
}

void SNiagaraParameterName::NamespaceModifierTextCommitted(const FText& InNewNameText, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnEnter)
	{
		FName NewParameterName = ReconstructNameFromNamespaceModifierEditText(InNewNameText);
		OnNameChangedDelegate.ExecuteIfBound(NewParameterName);
	}

	if (NamespaceModifierBorder.IsValid())
	{
		NamespaceModifierBorder->SetContent(SNew(STextBlock)
			.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.ParameterName.NamespaceText")
			.Text(GetNamespaceModifierText()));
	}
}

void SNiagaraParameterName::EnterEditingMode()
{
	if (EditableTextBlock.IsValid())
	{
		EditableTextBlock->EnterEditingMode();
	}
}

void SNiagaraParameterName::EnterNamespaceModifierEditingMode()
{
	if (NamespaceModifierBorder.IsValid())
	{
		TSharedRef<SInlineEditableTextBlock> NamespaceModifierEditableTextBlock = SNew(SInlineEditableTextBlock)
			.Style(EditableTextStyle)
			.Text(GetNamespaceModifierText())
			.IsSelected(IsSelected)
			.OnVerifyTextChanged(this, &SNiagaraParameterName::VerifyNamespaceModifierTextChange)
			.OnTextCommitted(this, &SNiagaraParameterName::NamespaceModifierTextCommitted);
		NamespaceModifierBorder->SetContent(NamespaceModifierEditableTextBlock);
		NamespaceModifierEditableTextBlock->EnterEditingMode();
	}
}

void SNiagaraParameterNameTextBlock::Construct(const FArguments& InArgs)
{
	ParameterText = InArgs._ParameterText;
	OnVerifyNameTextChangedDelegate = InArgs._OnVerifyTextChanged;
	OnNameTextCommittedDelegate = InArgs._OnTextCommitted;

	ChildSlot
	[
		SAssignNew(ParameterName, SNiagaraParameterName)
		.EditableTextStyle(InArgs._EditableTextStyle)
		.ParameterName(this, &SNiagaraParameterNameTextBlock::GetParameterName)
		.IsReadOnly(InArgs._IsReadOnly)
		.HighlightText(InArgs._HighlightText)
		.IsSelected(InArgs._IsSelected)
		.OnVerifyNameChange(this, &SNiagaraParameterNameTextBlock::VerifyNameChange)
		.OnNameChanged(this, &SNiagaraParameterNameTextBlock::NameChanged)
		.Decorator()
		[
			InArgs._Decorator.Widget
		]
	];
}

FName SNiagaraParameterNameTextBlock::GetParameterName() const
{
	FText CurrentPinText = ParameterText.Get();
	if (CurrentPinText.IdenticalTo(DisplayedParameterTextCache) == false)
	{
		DisplayedParameterTextCache = CurrentPinText;
		ParameterNameCache = *DisplayedParameterTextCache.ToString();
	}
	return ParameterNameCache;
}

bool SNiagaraParameterNameTextBlock::VerifyNameChange(FName InNewName, FText& OutErrorMessage)
{
	if (OnVerifyNameTextChangedDelegate.IsBound())
	{
		return OnVerifyNameTextChangedDelegate.Execute(FText::FromName(InNewName), OutErrorMessage);
	}
	else
	{
		return true;
	}
}

void SNiagaraParameterNameTextBlock::NameChanged(FName InNewName)
{
	OnNameTextCommittedDelegate.ExecuteIfBound(FText::FromName(InNewName), ETextCommit::OnEnter);
}

void SNiagaraParameterNameTextBlock::EnterEditingMode()
{
	ParameterName->EnterEditingMode();
}

void SNiagaraParameterNameTextBlock::EnterNamespaceModifierEditingMode()
{
	ParameterName->EnterNamespaceModifierEditingMode();
}

void SNiagaraParameterNamePinLabel::Construct(const FArguments& InArgs, UEdGraphPin* InTargetPin)
{
	TargetPin = InTargetPin;

	SNiagaraParameterNameTextBlock::Construct(SNiagaraParameterNameTextBlock::FArguments()
		.EditableTextStyle(InArgs._EditableTextStyle)
		.ParameterText(InArgs._ParameterText)
		.IsReadOnly(InArgs._IsReadOnly)
		.HighlightText(InArgs._HighlightText)
		.OnVerifyTextChanged(InArgs._OnVerifyTextChanged)
		.OnTextCommitted(InArgs._OnTextCommitted)
		.IsSelected(InArgs._IsSelected)
		.Decorator()
		[
			InArgs._Decorator.Widget
		]);
}

void SNiagaraParameterNamePinLabel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SNiagaraParameterNameTextBlock::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	UNiagaraNodeParameterMapBase* ParameterMapNode = Cast<UNiagaraNodeParameterMapBase>(TargetPin->GetOwningNode());
	if (ParameterMapNode != nullptr)
	{
		if (ParameterMapNode->GetIsPinEditNamespaceModifierPending(TargetPin))
		{
			ParameterMapNode->SetIsPinEditNamespaceModifierPending(TargetPin, false);
			EnterNamespaceModifierEditingMode();
		}
	}
}