// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Styling/SlateTypes.h"

class SGraphActionMenu;

class SNiagaraLibraryOnlyToggleHeader : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnLibraryOnlyChanged, bool);

public:
	SLATE_BEGIN_ARGS(SNiagaraLibraryOnlyToggleHeader)
		: _LibraryOnly(true)
		, _HeaderLabelText(NSLOCTEXT("LibraryOnlyToggle", "DefaultHeader", "Select Item"))
		{}
		SLATE_ATTRIBUTE(bool, LibraryOnly)
		SLATE_EVENT(FOnLibraryOnlyChanged, LibraryOnlyChanged)
		SLATE_ARGUMENT(FText, HeaderLabelText)
	SLATE_END_ARGS();

	NIAGARAEDITOR_API void Construct(const FArguments& InArgs);

	NIAGARAEDITOR_API void SetActionMenu(TSharedRef<SGraphActionMenu> InActionMenu);

private:
	void OnCheckStateChanged(ECheckBoxState InCheckState);

	ECheckBoxState GetCheckState() const;

private:
	TAttribute<bool> LibraryOnly;
	FOnLibraryOnlyChanged LibraryOnlyChanged;
	TWeakPtr<SGraphActionMenu> ActionMenuWeak;
};