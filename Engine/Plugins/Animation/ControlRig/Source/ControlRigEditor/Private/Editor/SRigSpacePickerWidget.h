// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rigs/RigHierarchy.h"
#include "SRigHierarchy.h"
#include "Widgets/Layout/SBox.h"
#include "Rigs/RigSpaceHierarchy.h"


/** Widget allowing picking of a space source for space switching */
class SRigSpacePickerWidget : public SCompoundWidget
{
public:

	struct FResult
	{
		FResult()
			: Reply(FReply::Unhandled())
			// , Actor(nullptr)
			// , SceneComponent(nullptr)
			, Key()
		{
		}
		
		FReply Reply;
		//AActor* Actor;
		//USceneComponent* SceneComponent;
		FRigElementKey Key;
	};

	SLATE_BEGIN_ARGS(SRigSpacePickerWidget) {}
		SLATE_ARGUMENT(URigHierarchy*, Hierarchy)
		SLATE_ARGUMENT(FRigElementKey, SelectedControl)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	~SRigSpacePickerWidget();

	FResult InvokeDialog();
	void CloseDialog(bool bWasPicked=false);
	FReply CancelClicked();
	virtual FReply OnKeyDown( const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent ) override;

	virtual bool SupportsKeyboardFocus() const override
	{
		return true;
	}

private:

	void AddSpacePickerButton(const FSlateBrush* InBush, const FText& InTitle, FOnClicked OnClickedDelegate);

	FReply HandleLocalSpaceClicked();
	FReply HandleWorldSpaceClicked();
	FReply HandleElementSpaceClicked(FRigElementKey InKey);

	URigHierarchy* Hierarchy;
	FRigElementKey ControlKey;
	TSharedPtr<SVerticalBox> ListBox;
	TWeakPtr<SWindow> PickerWindow;
	FRigElementKey PickedKey;
};