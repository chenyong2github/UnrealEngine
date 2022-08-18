// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/List/ObjectMixerEditorListRow.h"

#include "Types/SlateEnums.h"

#include "ObjectMixerEditorListMenuContext.generated.h"	

UCLASS()
class OBJECTMIXEREDITOR_API UObjectMixerEditorListMenuContext : public UObject
{

	GENERATED_BODY()
	
public:

	struct FObjectMixerEditorListMenuContextData
	{
		TArray<FObjectMixerEditorListRowPtr> SelectedItems;
		TWeakPtr<class FObjectMixerEditorMainPanel> MainPanelPtr;
	};

	static TSharedPtr<SWidget> CreateContextMenu(const FObjectMixerEditorListMenuContextData InData);
	static TSharedPtr<SWidget> BuildContextMenu(const FObjectMixerEditorListMenuContextData& InData);
	static void RegisterContextMenu();

	FObjectMixerEditorListMenuContextData Data;
	
	static FName DefaultContextBaseMenuName;

private:

	static void OnTextCommitted(const FText& InText, ETextCommit::Type InCommitType, UObjectMixerEditorListMenuContext* Context);
	static void OnTextChanged(const FText& InText, UObjectMixerEditorListMenuContext* Context);
	
	static void OnClickCategoryMenuEntry(const FName Key, UObjectMixerEditorListMenuContext* Context);
	static void AddObjectsToCategory(const FName Key, UObjectMixerEditorListMenuContext* Context);
	static void RemoveObjectsFromCategory(const FName Key, UObjectMixerEditorListMenuContext* Context);
	static bool AreAllObjectsInCategory(const FName Key, UObjectMixerEditorListMenuContext* Context);

	TSharedPtr<class SEditableTextBox> EditableText;
};
