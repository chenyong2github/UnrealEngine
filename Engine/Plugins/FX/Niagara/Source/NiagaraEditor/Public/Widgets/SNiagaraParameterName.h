// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "EditorStyleSet.h"

class SInlineEditableTextBlock;

class NIAGARAEDITOR_API SNiagaraParameterName : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnNameChanged, FName /* InNewName */);
	DECLARE_DELEGATE_RetVal_TwoParams(bool, FOnVerifyNameChange, FName /* InNewName */, FText& /* OutErrorMessage */)

public:
	SLATE_BEGIN_ARGS(SNiagaraParameterName) 
		: _EditableTextStyle(&FEditorStyle::Get().GetWidgetStyle<FInlineEditableTextBlockStyle>("InlineEditableTextBlockStyle"))
		, _ReadOnlyTextStyle(&FEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
		, _IsReadOnly(false)
	{}
	SLATE_STYLE_ARGUMENT(FInlineEditableTextBlockStyle, EditableTextStyle)
		SLATE_STYLE_ARGUMENT(FTextBlockStyle, ReadOnlyTextStyle)
		SLATE_ATTRIBUTE(FName, ParameterName)
		SLATE_ARGUMENT(bool, IsReadOnly)
		SLATE_EVENT(FOnVerifyNameChange, OnVerifyNameChange)
		SLATE_EVENT(FOnNameChanged, OnNameChanged)
		SLATE_EVENT(FIsSelected, IsSelected)
		SLATE_EVENT(FPointerEventHandler, OnDoubleClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override;

	void EnterEditingMode();

	void EnterSubnamespaceEditingMode();

private:
	TSharedRef<SBorder> CreateNamespaceWidget(FText NamespaceDisplayName, FText NamespaceDescription, FLinearColor NamespaceBorderColor);

	void UpdateContent(FName InDisplayedParameterName);

	FName ReconstructNameFromEditText(const FText& InEditText);

	FName ReconstructNameFromSubnamespaceEditText(const FText& InEditText);

	FText GetSubnamespaceText();

	bool VerifyNameTextChange(const FText& InNewNameText, FText& OutErrorMessage);

	void NameTextCommitted(const FText& InNewNameText, ETextCommit::Type CommitType);

	bool VerifySubnamespaceTextChange(const FText& InNewNameText, FText& OutErrorMessage);

	void SubnamespaceTextCommitted(const FText& InNewNameText, ETextCommit::Type CommitType);

private:
	const FInlineEditableTextBlockStyle* EditableTextStyle;
	const FTextBlockStyle* ReadOnlyTextStyle;
	TAttribute<FName> ParameterName;
	bool bIsReadOnly;
	FOnVerifyNameChange OnVerifyNameChangeDelegate;
	FOnNameChanged OnNameChangedDelegate;
	FPointerEventHandler OnDoubleClickedDelegate;
	FIsSelected IsSelected;
	FName DisplayedParameterName;
	FName DisplayedSubnamespace;
	TSharedPtr<SInlineEditableTextBlock> EditableTextBlock;
	TSharedPtr<SBorder> SubnamespaceBorder;
};

class NIAGARAEDITOR_API SNiagaraParameterNameTextBlock : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraParameterNameTextBlock)
		: _EditableTextStyle(&FEditorStyle::Get().GetWidgetStyle<FInlineEditableTextBlockStyle>("InlineEditableTextBlockStyle"))
	{}
		SLATE_ATTRIBUTE(FText, ParameterText)
		SLATE_STYLE_ARGUMENT(FInlineEditableTextBlockStyle, EditableTextStyle)
		SLATE_ARGUMENT(bool, IsReadOnly)
		SLATE_EVENT(FOnVerifyTextChanged, OnVerifyTextChanged)
		SLATE_EVENT(FOnTextCommitted, OnTextCommitted)
		SLATE_EVENT(FIsSelected, IsSelected)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void EnterEditingMode();

	void EnterSubnamespaceEditingMode();

private:
	FName GetParameterName() const;

	bool VerifyNameChange(FName InNewName, FText& OutErrorMessage);

	void NameChanged(FName InNewName);

private:
	TAttribute<FText> ParameterText;
	FOnVerifyTextChanged OnVerifyNameTextChangedDelegate;
	FOnTextCommitted OnNameTextCommittedDelegate;

	mutable FText DisplayedParameterTextCache;
	mutable FName ParameterNameCache;
	TSharedPtr<SNiagaraParameterName> ParameterName;
};