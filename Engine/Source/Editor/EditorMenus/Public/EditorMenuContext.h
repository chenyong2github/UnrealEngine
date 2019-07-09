// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "EditorMenuContext.generated.h"

class FUICommandList;
class FTabManager;
class FExtender;
class FExtensibilityManager;

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

	void AppendCommandList(TSharedPtr<FUICommandList> InCommandList);
	void AddExtender(const TSharedPtr<FExtender>& InExtender);
	TSharedPtr<FExtender> GetAllExtenders();
	void ReplaceExtenders(const TSharedPtr<FExtender>& InExtender);
	void ResetExtenders();

	void AppendObjects(const TArray<UObject*>& InObjects);
	void AddObject(UObject* InObject);

	friend class UEditorMenuSubsystem;
	

private:
	
	UPROPERTY()
	TArray<UObject*> ContextObjects;

	TSharedPtr<FUICommandList> CommandList;

	// Prevent CommandLists from being deleted
	TArray<TSharedPtr<const FUICommandList>> ReferencedCommandLists;
	TSharedPtr<FExtensibilityManager> ExtensibilityManager;
};

