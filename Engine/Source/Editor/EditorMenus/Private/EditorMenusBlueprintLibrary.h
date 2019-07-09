// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "EditorMenuDelegates.h"
#include "EditorMenuEntry.h"
#include "EditorMenuSection.h"
#include "Misc/Attribute.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "EditorMenusBlueprintLibrary.generated.h"

UCLASS()
class EDITORMENUS_API UEditorMenuContextExtensions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "Editor UI", meta = (ScriptMethod))
	static UObject* FindByClass(const FEditorMenuContext& Context, UClass* InClass);
};

UCLASS()
class EDITORMENUS_API UEditorMenuEntryExtensions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintPure, Category = "Editor UI", meta = (Keywords = "construct build", NativeMakeFunc))
	static FEditorMenuOwner MakeEditorMenuOwner(FName Name);

	UFUNCTION(BlueprintPure, Category = "Editor UI", meta = (NativeBreakFunc))
	static void BreakEditorMenuOwner(const FEditorMenuOwner& InValue, FName& Name);

	UFUNCTION(BlueprintCallable, Category = "Editor UI", meta = (ScriptMethod))
	static void SetLabel(UPARAM(ref) FEditorMenuEntry& Target, const FText& Label);

	UFUNCTION(BlueprintCallable, Category = "Editor UI", meta = (ScriptMethod))
	static FText GetLabel(const FEditorMenuEntry& Target);

	UFUNCTION(BlueprintCallable, Category = "Editor UI", meta = (ScriptMethod))
	static void SetToolTip(UPARAM(ref) FEditorMenuEntry& Target, const FText& ToolTip);

	UFUNCTION(BlueprintCallable, Category = "Editor UI", meta = (ScriptMethod))
	static FText GetToolTip(const FEditorMenuEntry& Target);

	UFUNCTION(BlueprintCallable, Category = "Editor UI", meta = (ScriptMethod))
	static void SetIcon(UPARAM(ref) FEditorMenuEntry& Target, const FName StyleSetName, const FName StyleName = NAME_None, const FName SmallStyleName = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "Editor UI", meta = (ScriptMethod))
	static void SetStringCommand(UPARAM(ref) FEditorMenuEntry& Target, const EEditorMenuStringCommandType Type, const FString& String, const FName CustomType = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "Editor UI")
	static FEditorMenuEntry InitMenuEntry(const FName InOwner, const FName InName, const FText& InLabel, const FText& InToolTip, const FEditorMenuStringCommand& StringCommand);
};

UCLASS()
class EDITORMENUS_API UEditorMenuSectionExtensions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "Editor UI", meta = (ScriptMethod))
	static void SetLabel(UPARAM(ref) FEditorMenuSection& Section, const FText& Label);

	UFUNCTION(BlueprintCallable, Category = "Editor UI", meta = (ScriptMethod))
	static FText GetLabel(const FEditorMenuSection& Section);

	UFUNCTION(BlueprintCallable, Category = "Editor UI", meta = (ScriptMethod))
	static void AddEntry(UPARAM(ref) FEditorMenuSection& Section, const FEditorMenuEntry& Args);

	UFUNCTION(BlueprintCallable, Category = "Editor UI", meta = (ScriptMethod))
	static void AddEntryObject(UPARAM(ref) FEditorMenuSection& Section, UEditorMenuEntryScript* InObject);
};
