// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SNiagaraParameterName.h"
#include "NiagaraEditorSettings.h"
#include "NiagaraEditorStyle.h"

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
	OnVerifyNameChangeDelegate = InArgs._OnVerifyNameChange;
	OnNameChangedDelegate = InArgs._OnNameChanged;
	OnDoubleClickedDelegate = InArgs._OnDoubleClicked;
	IsSelected = InArgs._IsSelected;

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

TSharedRef<SBorder> SNiagaraParameterName::CreateNamespaceWidget(FText NamespaceDisplayName, FText NamespaceDescription, FLinearColor NamespaceBorderColor)
{
	return SNew(SBorder)
	.BorderImage(FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.ParameterName.NamespaceBorder"))
	.BorderBackgroundColor(NamespaceBorderColor)
	.ToolTipText(NamespaceDescription)
	.VAlign(VAlign_Center)
	.Padding(FMargin(4.0f, 1.0f, 4.0f, 1.0f))
	[
		SNew(STextBlock)
		.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.ParameterName.NamespaceText")
		.Text(NamespaceDisplayName)
	];
}

void SNiagaraParameterName::UpdateContent(FName InDisplayedParameterName)
{
	DisplayedParameterName = InDisplayedParameterName;
	
	FString DisplayedParameterNameString = DisplayedParameterName.ToString();
	TArray<FString> NameParts;
	DisplayedParameterNameString.ParseIntoArray(NameParts, TEXT("."));

	TArray<FName> Namespaces;
	for (int32 i = 0; i < NameParts.Num() - 1; i++)
	{
		Namespaces.Add(*NameParts[i]);
	}

	TSharedRef<SHorizontalBox> ContentBox = SNew(SHorizontalBox);

	FNiagaraNamespaceMetadata DefaultNamespaceMetadata = GetDefault<UNiagaraEditorSettings>()->GetMetaDataForNamespaces({ NAME_None });

	// Add the namespace widget.
	FNiagaraNamespaceMetadata NamespaceMetadata = GetDefault<UNiagaraEditorSettings>()->GetMetaDataForNamespaces(Namespaces);
	TSharedPtr<SWidget> NamespaceWidget;
	if (NamespaceMetadata.IsValid())
	{
		Namespaces.RemoveAt(0, NamespaceMetadata.Namespaces.Num());
		NamespaceWidget = CreateNamespaceWidget(NamespaceMetadata.DisplayName.ToUpper(), NamespaceMetadata.Description, NamespaceMetadata.BackgroundColor);
	}
	else
	{
		FText NamespaceDisplayName = FText::FromString(FName::NameToDisplayString(Namespaces[0].ToString(), false).ToUpper());
		Namespaces.RemoveAt(0);
		CreateNamespaceWidget(
			NamespaceDisplayName,
			DefaultNamespaceMetadata.Description,
			DefaultNamespaceMetadata.BackgroundColor);
	}

	ContentBox->AddSlot()
	.VAlign(VAlign_Center)
	.AutoWidth()
	.Padding(0.0f, 0.0f, 5.0f, 0.0f)
	[
		NamespaceWidget.ToSharedRef()
	];

	// Next the namespace modifier widget is there is a namespace modifier.
	if (Namespaces.Num() > 0)
	{
		DisplayedNamespaceModifier = Namespaces[0];
		FNiagaraNamespaceMetadata DisplayedNamespaceModifierMetadata = GetDefault<UNiagaraEditorSettings>()->GetMetaDataForNamespaceModifier(DisplayedNamespaceModifier);
		if (DisplayedNamespaceModifierMetadata.IsValid() == false)
		{
			DisplayedNamespaceModifierMetadata = DefaultNamespaceMetadata;
		}

		Namespaces.RemoveAt(0);
		NamespaceModifierBorder = CreateNamespaceWidget(
			FText::FromString(FName::NameToDisplayString(DisplayedNamespaceModifier.ToString(), false).ToUpper()),
			DisplayedNamespaceModifierMetadata.Description,
			DisplayedNamespaceModifierMetadata.BackgroundColor);

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
	for (FName Namespace : Namespaces)
	{
		ContentBox->AddSlot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		.Padding(0.0f, 0.0f, 5.0f, 0.0f)
		[
			CreateNamespaceWidget(
				FText::FromString(FName::NameToDisplayString(Namespace.ToString(), false).ToUpper()),
				DefaultNamespaceMetadata.Description,
				DefaultNamespaceMetadata.BackgroundColor)
		];
	}

	TSharedPtr<SWidget> NameWidget;
	if (bIsReadOnly)
	{
		NameWidget = SNew(STextBlock)
			.TextStyle(ReadOnlyTextStyle)
			.Text(FText::FromString(NameParts.Last()));
	}
	else
	{
		NameWidget = SAssignNew(EditableTextBlock, SInlineEditableTextBlock)
		.Style(EditableTextStyle)
		.Text(FText::FromString(NameParts.Last()))
		.IsSelected(IsSelected)
		.OnVerifyTextChanged(this, &SNiagaraParameterName::VerifyNameTextChange)
		.OnTextCommitted(this, &SNiagaraParameterName::NameTextCommitted);
	}

	ContentBox->AddSlot()
	.VAlign(VAlign_Center)
	.AutoWidth()
	[
		NameWidget.ToSharedRef()
	];

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

	if (NameParts.Num() >= 3)
	{
		NameParts[1] = InEditText.ToString().Replace(TEXT("."), TEXT("_"));
	}
	FString NewParameterNameString = FString::Join(NameParts, TEXT("."));
	return *NewParameterNameString;
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
		.IsSelected(InArgs._IsSelected)
		.OnVerifyNameChange(this, &SNiagaraParameterNameTextBlock::VerifyNameChange)
		.OnNameChanged(this, &SNiagaraParameterNameTextBlock::NameChanged)
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