// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ControlRigPickerWidget.h"
#include "ControlRigEditMode.h"

UControlRigPickerWidget::UControlRigPickerWidget(const FObjectInitializer& ObjectInitializer)
	: UUserWidget(ObjectInitializer)
	, EditMode(nullptr)
{
}

void UControlRigPickerWidget::SelectControl(const FString& ControlPropertyPath, bool bSelected)
{
	if(EditMode)
	{
		EditMode->SetRigElementSelection(ERigElementType::Control, FName(*ControlPropertyPath, FNAME_Find), bSelected);
	}
}

bool UControlRigPickerWidget::IsControlSelected(const FString& ControlPropertyPath)
{
	if(EditMode)
	{
		return EditMode->SelectedRigElements.Contains(FRigElementKey(FName(*ControlPropertyPath, FNAME_Find), ERigElementType::Control));
	}

	return false;
}

void UControlRigPickerWidget::EnableControl(const FString& ControlPropertyPath, bool bEnabled)
{
	if(EditMode)
	{
		ensure(false);
		//EditMode->SetControlEnabled(ControlPropertyPath, bEnabled);
	}
}

bool UControlRigPickerWidget::IsControlEnabled(const FString& ControlPropertyPath)
{
	if(EditMode)
	{
		ensure(false);
		return true; //EditMode->IsControlEnabled(ControlPropertyPath);
	}

	return false;
}