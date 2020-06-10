// Copyright Epic Games, Inc. All Rights Reserved.

#include "Subsystems/EditorDelegateSubsystem.h"
#include "Editor.h"

UEditorDelegateSubsystem::UEditorDelegateSubsystem()
	: UEditorSubsystem()
{

}

void UEditorDelegateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	FEditorDelegates::OnEditCutActorsBegin.AddUObject(this, &UEditorDelegateSubsystem::BroadcastEditCutActorsBegin);
	FEditorDelegates::OnEditCutActorsEnd.AddUObject(this, &UEditorDelegateSubsystem::BroadcastEditCutActorsEnd);
	
	FEditorDelegates::OnEditCopyActorsBegin.AddUObject(this, &UEditorDelegateSubsystem::BroadcastEditCopyActorsBegin);
	FEditorDelegates::OnEditCopyActorsEnd.AddUObject(this, &UEditorDelegateSubsystem::BroadcastEditCopyActorsEnd);

	FEditorDelegates::OnEditPasteActorsBegin.AddUObject(this, &UEditorDelegateSubsystem::BroadcastEditPasteActorsBegin);
	FEditorDelegates::OnEditPasteActorsEnd.AddUObject(this, &UEditorDelegateSubsystem::BroadcastEditPasteActorsEnd);

	FEditorDelegates::OnDuplicateActorsBegin.AddUObject(this, &UEditorDelegateSubsystem::BroadcastDuplicateActorsBegin);
	FEditorDelegates::OnDuplicateActorsEnd.AddUObject(this, &UEditorDelegateSubsystem::BroadcastDuplicateActorsEnd);

	FEditorDelegates::OnDeleteActorsBegin.AddUObject(this, &UEditorDelegateSubsystem::BroadcastDeleteActorsBegin);
	FEditorDelegates::OnDeleteActorsEnd.AddUObject(this, &UEditorDelegateSubsystem::BroadcastDeleteActorsEnd);
}

void UEditorDelegateSubsystem::Deinitialize()
{
	FEditorDelegates::OnEditCutActorsBegin.RemoveAll(this);
	FEditorDelegates::OnEditCutActorsEnd.RemoveAll(this);
	FEditorDelegates::OnEditCopyActorsBegin.RemoveAll(this);
	FEditorDelegates::OnEditCopyActorsEnd.RemoveAll(this);
	FEditorDelegates::OnEditPasteActorsBegin.RemoveAll(this);
	FEditorDelegates::OnEditPasteActorsEnd.RemoveAll(this);
	FEditorDelegates::OnDuplicateActorsBegin.RemoveAll(this);
	FEditorDelegates::OnDuplicateActorsEnd.RemoveAll(this);
	FEditorDelegates::OnDeleteActorsBegin.RemoveAll(this);
	FEditorDelegates::OnDeleteActorsEnd.RemoveAll(this);

}

/** To fire before an Actor is Cut */
void UEditorDelegateSubsystem::BroadcastEditCutActorsBegin()
{
	OnEditCutActorsBegin.Broadcast();
}

/** To fire after an Actor is Cut */
void UEditorDelegateSubsystem::BroadcastEditCutActorsEnd()
{
	OnEditCutActorsEnd.Broadcast();
}

/** To fire before an Actor is Copied */
void UEditorDelegateSubsystem::BroadcastEditCopyActorsBegin()
{
	OnEditCopyActorsBegin.Broadcast();
}

/** To fire after an Actor is Copied */
void UEditorDelegateSubsystem::BroadcastEditCopyActorsEnd()
{
	OnEditCopyActorsEnd.Broadcast();
}

/** To fire before an Actor is Pasted */
void UEditorDelegateSubsystem::BroadcastEditPasteActorsBegin()
{
	OnEditPasteActorsBegin.Broadcast();
}

/** To fire after an Actor is Pasted */
void UEditorDelegateSubsystem::BroadcastEditPasteActorsEnd()
{
	OnEditPasteActorsEnd.Broadcast();
}

/** To fire before an Actor is duplicated */
void UEditorDelegateSubsystem::BroadcastDuplicateActorsBegin()
{
	OnDuplicateActorsBegin.Broadcast();
}

/** To fire after an Actor is duplicated */
void UEditorDelegateSubsystem::BroadcastDuplicateActorsEnd()
{
	OnDuplicateActorsEnd.Broadcast();
}

/** To fire before an Actor is Deleted */
void UEditorDelegateSubsystem::BroadcastDeleteActorsBegin()
{
	OnDeleteActorsBegin.Broadcast();
}

/** To fire after an Actor is Deleted */
void UEditorDelegateSubsystem::BroadcastDeleteActorsEnd()
{
	OnDeleteActorsEnd.Broadcast();
}
