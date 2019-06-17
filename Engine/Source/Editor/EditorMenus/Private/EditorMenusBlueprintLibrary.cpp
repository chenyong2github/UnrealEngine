// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "EditorMenusBlueprintLibrary.h"

FEditorMenuOwner UEditorMenuEntryExtensions::MakeEditorMenuOwner(FName Name)
{
	return FEditorMenuOwner(Name);
}

void UEditorMenuEntryExtensions::BreakEditorMenuOwner(const FEditorMenuOwner& InValue, FName& Name)
{
	Name = InValue.TryGetName();
}

UObject* UEditorMenuContextExtensions::FindByClass(const FEditorMenuContext& Context, UClass* InClass)
{
	return Context.FindByClass(InClass);
}

void UEditorMenuEntryExtensions::SetLabel(UPARAM(ref) FEditorMenuEntry& Target, const FText& Label)
{
	Target.Label = Label;
}

FText UEditorMenuEntryExtensions::GetLabel(const FEditorMenuEntry& Target)
{
	return Target.Label.Get();
}

void UEditorMenuEntryExtensions::SetToolTip(UPARAM(ref) FEditorMenuEntry& Target, const FText& ToolTip)
{
	Target.ToolTip = ToolTip;
}

FText UEditorMenuEntryExtensions::GetToolTip(const FEditorMenuEntry& Target)
{
	return Target.ToolTip.Get();
}

void UEditorMenuEntryExtensions::SetIcon(UPARAM(ref) FEditorMenuEntry& Target, const FName StyleSetName, const FName StyleName, const FName SmallStyleName)
{
	if (SmallStyleName == NAME_None)
	{
		if (StyleSetName == NAME_None && StyleName == NAME_None)
		{
			Target.Icon = FSlateIcon();
		}
			
		Target.Icon = FSlateIcon(StyleSetName, StyleName);
	}

	Target.Icon = FSlateIcon(StyleSetName, StyleName, SmallStyleName);
}

void UEditorMenuEntryExtensions::SetStringCommand(UPARAM(ref) FEditorMenuEntry& Target, const FName TypeName, const FString& String)
{
	Target.ResetActions();
	Target.StringCommand.TypeName = TypeName;
	Target.StringCommand.String = String;
}

FEditorMenuEntry UEditorMenuEntryExtensions::InitMenuEntry(const FName InOwner, const FName InName, const FText& InLabel, const FText& InToolTip, const FEditorMenuStringCommand& StringCommand)
{
	FEditorMenuEntry Entry(InOwner, InName, EMultiBlockType::MenuEntry);
	Entry.Label = InLabel;
	Entry.ToolTip = InToolTip;
	Entry.StringCommand = StringCommand;
	return Entry;
}

void UEditorMenuSectionExtensions::SetLabel(UPARAM(ref) FEditorMenuSection& Section, const FText& Label)
{
	Section.Label = Label;
}

FText UEditorMenuSectionExtensions::GetLabel(const FEditorMenuSection& Section)
{
	return Section.Label.Get();
}

void UEditorMenuSectionExtensions::AddEntry(UPARAM(ref) FEditorMenuSection& Section, const FEditorMenuEntry& Args)
{
	Section.AddEntry(Args);
}

void UEditorMenuSectionExtensions::AddEntryObject(UPARAM(ref) FEditorMenuSection& Section, UEditorMenuEntryScript* InObject)
{
	Section.AddEntryObject(InObject);
}
