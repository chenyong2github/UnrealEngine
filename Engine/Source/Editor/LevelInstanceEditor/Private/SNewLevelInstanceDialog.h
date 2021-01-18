// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Types/SlateEnums.h"
#include "Styling/SlateColor.h"
#include "Input/Reply.h"
#include "Widgets/SWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

enum class ELevelInstanceCreationType : uint8;

enum class ELevelInstancePivotType : uint8;

//////////////////////////////////////////////////////////////////////////
// SNewLevelInstanceDialog

class SNewLevelInstanceDialog : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SNewLevelInstanceDialog) {}
		/** A pointer to the parent window */
		SLATE_ATTRIBUTE(TSharedPtr<SWindow>, ParentWindow)
		SLATE_ATTRIBUTE(TArray<AActor*>, PivotActors)
	SLATE_END_ARGS()

	~SNewLevelInstanceDialog() {}

	/**
	 * Construct this widget.  Called by the SNew() Slate macro.
	 *
	 * @param	InArgs				Declaration used by the SNew() macro to construct this widget
	 * @param	InViewModel			The UI logic not specific to slate
	 */
	void Construct(const FArguments& InArgs);

	static const FVector2D DEFAULT_WINDOW_SIZE;

	bool ClickedOk() const { return bClickedOk; }
	ELevelInstanceCreationType GetCreationType() const { return SelectedCreationType; }
	ELevelInstancePivotType GetPivotType() const { return SelectedPivotType; }
	AActor* GetPivotActor() const { return SelectedPivotActor; }

private:
	int32 GetSelectedCreationType() const;
	void OnSelectedCreationTypeChanged(int32 NewValue, ESelectInfo::Type SelectionType);

	int32 GetSelectedPivotType() const;
	void OnSelectedPivotTypeChanged(int32 NewValue, ESelectInfo::Type SelectionType);
		
	FReply OnOkClicked();
	bool IsOkEnabled() const;

	FReply OnCancelClicked();

	TSharedRef<SWidget> OnGeneratePivotActorWidget(AActor* Actor) const;
	FText GetSelectedPivotActorText() const;
	void OnSelectedPivotActorChanged(AActor* NewValue, ESelectInfo::Type SelectionType);
	bool IsPivotActorSelectionEnabled() const;

	/** Pointer to the parent window, so we know to destroy it when done */
	TWeakPtr<SWindow> ParentWindowPtr;

	/** The type of LevelInstance to create */
	ELevelInstanceCreationType SelectedCreationType;

	/** The type of Pivot */
	ELevelInstancePivotType SelectedPivotType;

	/** Pivot Actor */
	AActor* SelectedPivotActor = nullptr;

	/** Actor List */
	TArray<AActor*> PivotActors;

	/** Dialog Result */
	bool bClickedOk;
};