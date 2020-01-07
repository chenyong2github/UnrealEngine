// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Templates/SharedPointer.h"
#include "Templates/SubclassOf.h"

class FCurveEditor;
class IDetailsView;
class UCurveEditorFilterBase;
class SWindow;
class FTabManager;

class CURVEEDITOR_API SCurveEditorFilterPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCurveEditorFilterPanel)
	{}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<FCurveEditor> InCurveEditor);
	void SetFilterClass(UClass* InClass);

public:
	/** Call this to request opening a window containing this panel. */
	static void OpenDialog(const TSharedRef<FTabManager>& TabManager, TSharedRef<FCurveEditor> InHostCurveEditor, TSubclassOf<UCurveEditorFilterBase> DefaultFilterClass);
	
	/** Closes the dialog if there is one open. */
	static void CloseDialog();
protected:
	FReply OnApplyClicked();
	bool CanApplyFilter() const;
	FText GetCurrentFilterText() const;
private:
	/** Weak pointer to the curve editor which created this filter. */
	TWeakPtr<FCurveEditor> WeakCurveEditor;

	/** The Details View in our UI that we update each time they choose a class. */
	TSharedPtr<IDetailsView> DetailView;

	/** Singleton for the pop-up window. */
	static TWeakPtr<SWindow> ExistingFilterWindow;
};