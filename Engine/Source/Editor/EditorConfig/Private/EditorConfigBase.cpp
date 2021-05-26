// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorConfigBase.h"
#include "EditorConfigSubsystem.h"
#include "Editor.h"

bool UEditorConfigBase::LoadEditorConfig()
{
	UEditorConfigSubsystem* Subsystem = GEditor->GetEditorSubsystem<UEditorConfigSubsystem>();
	return Subsystem->LoadConfigObject(this->GetClass(), this);
}

bool UEditorConfigBase::SaveEditorConfig() const
{
	UEditorConfigSubsystem* Subsystem = GEditor->GetEditorSubsystem<UEditorConfigSubsystem>();
	return Subsystem->SaveConfigObject(this->GetClass(), this);
}