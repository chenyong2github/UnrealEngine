// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "EditorMenuContext.generated.h"

class FUICommandList;
class FTabManager;
class FExtender;
class FExtensibilityManager;
struct FUIAction;

UCLASS(BlueprintType, Abstract)
class EDITORMENUS_API UEditorMenuContextBase : public UObject
{
	GENERATED_BODY()
};

UCLASS()
class EDITORMENUS_API USlateTabManagerContext : public UEditorMenuContextBase
{
	GENERATED_BODY()
public:

	TWeakPtr<FTabManager> TabManager;
};

USTRUCT(BlueprintType)
struct EDITORMENUS_API FEditorMenuContext
{
	GENERATED_BODY()
public:
	
	FEditorMenuContext();
	FEditorMenuContext(UObject* InContext);
	FEditorMenuContext(TSharedPtr<FUICommandList> InCommandList, TSharedPtr<FExtender> InExtender = TSharedPtr<FExtender>(), UObject* InContext = nullptr);

	template <typename TContextType>
	TContextType* Find() const
	{
		for (UObject* Object : ContextObjects)
		{
			if (TContextType* Result = Cast<TContextType>(Object))
			{
				return Result;
			}
		}

		return nullptr;
	}
	
	UObject* FindByClass(UClass* InClass) const;

	void AppendCommandList(const TSharedRef<FUICommandList>& InCommandList);
	void AppendCommandList(const TSharedPtr<FUICommandList>& InCommandList);
	const FUIAction* GetActionForCommand(TSharedPtr<const FUICommandInfo> Command, TSharedPtr<const FUICommandList>& OutCommandList) const;

	void AddExtender(const TSharedPtr<FExtender>& InExtender);
	TSharedPtr<FExtender> GetAllExtenders();
	void ReplaceExtenders(const TSharedPtr<FExtender>& InExtender);
	void ResetExtenders();

	void AppendObjects(const TArray<UObject*>& InObjects);
	void AddObject(UObject* InObject);

	friend class UEditorMenuSubsystem;
	friend struct FEditorMenuEntry;

private:
	
	UPROPERTY()
	TArray<UObject*> ContextObjects;

	TArray<TSharedPtr<FUICommandList>> CommandLists;

	TSharedPtr<FUICommandList> CommandList;

	TSharedPtr<FExtensibilityManager> ExtensibilityManager;
};

