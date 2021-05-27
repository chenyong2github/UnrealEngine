// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorConfigBase.h"
#include "EditorConfigSubsystem.h"
#include "Editor.h"

bool UEditorConfigBase::LoadEditorConfig()
{
	if (GEditor)
	{
		UEditorConfigSubsystem* Subsystem = GEditor->GetEditorSubsystem<UEditorConfigSubsystem>();
		if (Subsystem)
		{
			return Subsystem->LoadConfigObject(this->GetClass(), this);
		}
	}
	return false;
}

bool UEditorConfigBase::SaveEditorConfig() const
{
	if (GEditor)
	{
		UEditorConfigSubsystem* Subsystem = GEditor->GetEditorSubsystem<UEditorConfigSubsystem>();
		if (Subsystem)
		{
			return Subsystem->SaveConfigObject(this->GetClass(), this);
		}
	}
	return false;
}