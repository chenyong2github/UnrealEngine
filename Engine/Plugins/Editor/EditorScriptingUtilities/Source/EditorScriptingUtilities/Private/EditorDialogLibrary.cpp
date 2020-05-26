// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorDialogLibrary.h"
#include "Misc/MessageDialog.h"

TEnumAsByte<EAppReturnType::Type> UEditorDialogLibrary::ShowMessage(const FText& Title, const FText& Message, TEnumAsByte<EAppMsgType::Type> MessageType, TEnumAsByte<EAppReturnType::Type> DefaultValue)
{
	return FMessageDialog::Open(MessageType, DefaultValue, Message, &Title);
}
