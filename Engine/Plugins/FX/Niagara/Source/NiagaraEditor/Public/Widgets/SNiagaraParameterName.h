// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "EditorStyleSet.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Styling/SlateTypes.h"
#include "Framework/SlateDelegates.h"
#include "Styling/SlateWidgetStyleAsset.h"

class SInlineEditableTextBlock;
class SBorder;

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
		SLATE_ATTRIBUTE(FText, HighlightText)
		SLATE_EVENT(FOnVerifyNameChange, OnVerifyNameChange)
		SLATE_EVENT(FOnNameChanged, OnNameChanged)
		SLATE_EVENT(FIsSelected, IsSelected)
		SLATE_EVENT(FPointerEventHandler, OnDoubleClicked)
		SLATE_NAMED_SLOT(FArguments, Decorator)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override;

	void EnterEditingMode();

	void EnterNamespaceModifierEditingMode();

private:
	TSharedRef<SBorder> CreateNamespaceWidget(FText NamespaceDisplayName, FText NamespaceDescription, FLinearColor NamespaceBorderColor, FName NamespaceForegroundStyle);

	void UpdateContent(FName InDisplayedParameterName);

	FName ReconstructNameFromEditText(const FText& InEditText);

	FName ReconstructNameFromNamespaceModifierEditText(const FText& InEditText);

	FText GetNamespaceModifierText();

	bool VerifyNameTextChange(const FText& InNewNameText, FText& OutErrorMessage);

	void NameTextCommitted(const FText& InNewNameText, ETextCommit::Type CommitType);

	bool VerifyNamespaceModifierTextChange(const FText& InNewNameText, FText& OutErrorMessage);

	void NamespaceModifierTextCommitted(const FText& InNewNameText, ETextCommit::Type CommitType);

private:
	const FInlineEditableTextBlockStyle* EditableTextStyle;
	const FTextBlockStyle* ReadOnlyTextStyle;
	TAttribute<FName> ParameterName;
	bool bIsReadOnly;
	TAttribute<FText> HighlightText;
	FOnVerifyNameChange OnVerifyNameChangeDelegate;
	FOnNameChanged OnNameChangedDelegate;
	FPointerEventHandler OnDoubleClickedDelegate;
	FIsSelected IsSelected;
	FName DisplayedParameterName;
	FName DisplayedNamespaceModifier;
	TSharedPtr<SInlineEditableTextBlock> EditableTextBlock;
	TSharedPtr<SBorder> NamespaceModifierBorder;
	TSharedPtr<SWidget> Decorator;
};

class NIAGARAEDITOR_API SNiagaraParameterNameTextBlock : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraParameterNameTextBlock)
		: _EditableTextStyle(&FEditorStyle::Get().GetWidgetStyle<FInlineEditableTextBlockStyle>("InlineEditableTextBlockStyle"))
	{
		_Clipping = EWidgetClipping::OnDemand;
	}
		SLATE_ATTRIBUTE(FText, ParameterText)
		SLATE_STYLE_ARGUMENT(FInlineEditableTextBlockStyle, EditableTextStyle)
		SLATE_ARGUMENT(bool, IsReadOnly)
		SLATE_ATTRIBUTE(FText, HighlightText)
		SLATE_EVENT(FOnVerifyTextChanged, OnVerifyTextChanged)
		SLATE_EVENT(FOnTextCommitted, OnTextCommitted)
		SLATE_EVENT(FIsSelected, IsSelected)
		SLATE_NAMED_SLOT(FArguments, Decorator)
		
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void EnterEditingMode();

	void EnterNamespaceModifierEditingMode();

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