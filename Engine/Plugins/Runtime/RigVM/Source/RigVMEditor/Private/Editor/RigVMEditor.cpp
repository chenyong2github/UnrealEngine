// Copyright Epic Games, Inc. All Rights Reserved.

#include "Editor/RigVMEditor.h"
#include "Editor/RigVMDetailsViewWrapperObject.h"

#define LOCTEXT_NAMESPACE "RigVMEditor"

FRigVMEditor::FRigVMEditor()
	: Host(nullptr)
{
}

URigVMBlueprint* FRigVMEditor::GetRigVMBlueprint() const
{
	return Cast<URigVMBlueprint>(GetBlueprintObj());
}

URigVMHost* FRigVMEditor::GetRigVMHost() const
{
	return Host;
}

UClass* FRigVMEditor::GetDetailWrapperClass() const
{
	return URigVMDetailsViewWrapperObject::StaticClass();
}

void FRigVMEditor::HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject)
{
}

void FRigVMEditor::SetHost(URigVMHost* InHost)
{
	Host = InHost;
	if(Host)
	{
		OnPreviewHostUpdated().Broadcast(this);
	}
}

#undef LOCTEXT_NAMESPACE 
