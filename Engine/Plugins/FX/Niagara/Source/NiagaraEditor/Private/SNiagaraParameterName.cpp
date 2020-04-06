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
	auto AddNamespaceSlot = [](TSharedRef<SHorizontalBox> ContentBox, FText NamespaceDisplayName, FText NamespaceDescription, FLinearColor NamespaceBorderColor)
	{
		ContentBox->AddSlot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		.Padding(0.0f, 0.0f, 5.0f, 0.0f)
		[

				SNew(SBorder)
				.BorderImage(FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.ParameterName.NamespaceBorder"))
				.BorderBackgroundColor(NamespaceBorderColor)
				.ToolTipText(NamespaceDescription)
				.VAlign(VAlign_Center)
				.Padding(FMargin(4.0f, 1.0f, 4.0f, 1.0f))
				[
					SNew(STextBlock)
					.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.ParameterName.NamespaceText")
					.Text(NamespaceDisplayName)
				]
		];
	};

	FNiagaraNamespaceMetadata NamespaceMetadata = GetDefault<UNiagaraEditorSettings>()->GetMetaDataForNamespaces(Namespaces);
	if (NamespaceMetadata.IsValid())
	{
		Namespaces.RemoveAt(0, NamespaceMetadata.Namespaces.Num());
		ContentBox->AddSlot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		.Padding(0.0f, 0.0f, 5.0f, 0.0f)
		[
			CreateNamespaceWidget(NamespaceMetadata.DisplayName.ToUpper(), NamespaceMetadata.Description, NamespaceMetadata.BackgroundColor)
		];
	}

	FNiagaraNamespaceMetadata DefaultNamespaceMetadata = GetDefault<UNiagaraEditorSettings>()->GetMetaDataForNamespaces({NAME_None});
	if (Namespaces.Num() > 0)
	{
		DisplayedSubnamespace = Namespaces[0];
		Namespaces.RemoveAt(0);
		SubnamespaceBorder = CreateNamespaceWidget(
			FText::FromString(FName::NameToDisplayString(DisplayedSubnamespace.ToString(), false).ToUpper()),
			DefaultNamespaceMetadata.Description,
			DefaultNamespaceMetadata.BackgroundColor);

		ContentBox->AddSlot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		.Padding(0.0f, 0.0f, 5.0f, 0.0f)
		[
			SubnamespaceBorder.ToSharedRef()
		];
	}
	else
	{
		DisplayedSubnamespace = NAME_None;
		SubnamespaceBorder.Reset();
	}

	if (ensureMsgf(Namespaces.Num() == 0, TEXT("Extra namespaces in parameter.")) == false)
	{
		// If there are extra namespaces found, add them to the UI.
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

FName SNiagaraParameterName::ReconstructNameFromSubnamespaceEditText(const FText& InEditText)
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

FText SNiagaraParameterName::GetSubnamespaceText()
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

bool SNiagaraParameterName::VerifySubnamespaceTextChange(const FText& InNewNameText, FText& OutErrorMessage)
{
	FName NewParameterName = ReconstructNameFromSubnamespaceEditText(InNewNameText);
	if (OnVerifyNameChangeDelegate.IsBound())
	{
		return OnVerifyNameChangeDelegate.Execute(NewParameterName, OutErrorMessage);
	}
	return true;
}

void SNiagaraParameterName::SubnamespaceTextCommitted(const FText& InNewNameText, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnEnter)
	{
		FName NewParameterName = ReconstructNameFromSubnamespaceEditText(InNewNameText);
		OnNameChangedDelegate.ExecuteIfBound(NewParameterName);
	}

	if (SubnamespaceBorder.IsValid())
	{
		SubnamespaceBorder->SetContent(SNew(STextBlock)
			.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.ParameterName.NamespaceText")
			.Text(GetSubnamespaceText()));
	}
}

void SNiagaraParameterName::EnterEditingMode()
{
	if (EditableTextBlock.IsValid())
	{
		EditableTextBlock->EnterEditingMode();
	}
}

void SNiagaraParameterName::EnterSubnamespaceEditingMode()
{
	if (SubnamespaceBorder.IsValid())
	{
		TSharedRef<SInlineEditableTextBlock> SubnamespaceEditableTextBlock = SNew(SInlineEditableTextBlock)
			.Style(EditableTextStyle)
			.Text(GetSubnamespaceText())
			.IsSelected(IsSelected)
			.OnVerifyTextChanged(this, &SNiagaraParameterName::VerifySubnamespaceTextChange)
			.OnTextCommitted(this, &SNiagaraParameterName::SubnamespaceTextCommitted);
		SubnamespaceEditableTextBlock->EnterEditingMode();
		SubnamespaceBorder->SetContent(SubnamespaceEditableTextBlock);
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

void SNiagaraParameterNameTextBlock::EnterSubnamespaceEditingMode()
{
	ParameterName->EnterSubnamespaceEditingMode();
}