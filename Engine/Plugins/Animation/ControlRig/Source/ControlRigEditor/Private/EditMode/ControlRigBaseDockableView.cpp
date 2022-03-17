// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigBaseDockableView.h"
#include "AssetData.h"
#include "EditorStyleSet.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "EditorStyleSet.h"
#include "Styling/CoreStyle.h"
#include "ScopedTransaction.h"
#include "ControlRig.h"
#include "UnrealEdGlobals.h"
#include "ControlRigEditMode.h"
#include "EditorModeManager.h"
#include "ISequencer.h"

void FControlRigBaseDockableView::SetEditMode(FControlRigEditMode& InEditMode)
{

	CurrentControlRig = nullptr;
	ModeTools = InEditMode.GetModeManager();
	if (FControlRigEditMode* EditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName)))
	{
		EditMode->OnControlRigAddedOrRemoved().AddRaw(this, &FControlRigBaseDockableView::HandleControlAdded);
		HandleControlAdded(GetControlRig(), true);
	}
}

FControlRigBaseDockableView::FControlRigBaseDockableView()
{
	FCoreUObjectDelegates::OnObjectsReplaced.AddRaw(this, &FControlRigBaseDockableView::OnObjectsReplaced);
}

FControlRigBaseDockableView::~FControlRigBaseDockableView()
{
	if (FControlRigEditMode* EditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName)))
	{
		EditMode->OnControlRigAddedOrRemoved().RemoveAll(this);
		TArrayView<TWeakObjectPtr<UControlRig>> Controls = EditMode->GetControlRigs();
		for (TWeakObjectPtr<UControlRig>& ControlRigPtr : Controls)
		{
			if (UControlRig* ControlRig = ControlRigPtr.Get())
			{
				ControlRig->ControlSelected().RemoveAll(this);
			}
		}
	}
	else
	{
		if (CurrentControlRig.IsValid())
		{
			(CurrentControlRig.Get())->ControlSelected().RemoveAll(this);
		}
	}
	CurrentControlRig = nullptr;

	FCoreUObjectDelegates::OnObjectsReplaced.RemoveAll(this);

}

UControlRig* FControlRigBaseDockableView::GetControlRig() 
{
	UControlRig* NewControlRig = nullptr;
	if (FControlRigEditMode* EditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName)))
	{
		//just get first one, should only be one for control rig editor
		TArrayView<TWeakObjectPtr<UControlRig>> Controls = EditMode->GetControlRigs();
		for (TWeakObjectPtr<UControlRig>& ControlRigPtr : Controls)
		{
			if (UControlRig* ControlRig = ControlRigPtr.Get())
			{
				NewControlRig = ControlRig;
				break;
			}
		}
	}
	bool bNewControlRig = NewControlRig != CurrentControlRig;
	if (bNewControlRig)
	{
		if (CurrentControlRig.IsValid())
		{
			(CurrentControlRig.Get())->ControlSelected().RemoveAll(this);

		}
		CurrentControlRig = NewControlRig;
		NewControlRigSet(NewControlRig);
	}
	return NewControlRig;
}


void FControlRigBaseDockableView::OnObjectsReplaced(const TMap<UObject*, UObject*>& OldToNewInstanceMap)
{
	if(CurrentControlRig.IsValid())
	{ 
		UObject* OldObject = CurrentControlRig.Get();
		UObject* NewObject = OldToNewInstanceMap.FindRef(OldObject);
		if (NewObject)
		{
			if (UControlRig* ControlRig = Cast<UControlRig>(NewObject))
			{
				NewControlRigSet(ControlRig);
			}
		}
	}
}

void FControlRigBaseDockableView::NewControlRigSet(UControlRig* ControlRig)
{
	CurrentControlRig = ControlRig;
}

void FControlRigBaseDockableView::HandleControlAdded(UControlRig* ControlRig, bool bIsAdded)
{
	if (ControlRig)
	{
		if (bIsAdded)
		{
			(ControlRig)->ControlSelected().RemoveAll(this);
			(ControlRig)->ControlSelected().AddRaw(this, &FControlRigBaseDockableView::HandleControlSelected);
			CurrentControlRig = ControlRig;
		}
		else
		{
			(ControlRig)->ControlSelected().RemoveAll(this);
		}
	}
}

void FControlRigBaseDockableView::HandleControlSelected(UControlRig* Subject, FRigControlElement* InControl, bool bSelected)
{

}

ISequencer* FControlRigBaseDockableView::GetSequencer() const
{
	if (FControlRigEditMode* EditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName)))
	{
		TWeakPtr<ISequencer> Sequencer = EditMode->GetWeakSequencer();
		return Sequencer.Pin().Get();
	}
	return nullptr;
}
