// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ToolMenuContext.generated.h"

struct FUIAction;
class FUICommandInfo;
class FUICommandList;
class FTabManager;
class FExtender;

UCLASS(BlueprintType, Abstract)
class TOOLMENUS_API UToolMenuContextBase : public UObject
{
	GENERATED_BODY()
};

UCLASS()
class TOOLMENUS_API USlateTabManagerContext : public UToolMenuContextBase
{
	GENERATED_BODY()
public:

	TWeakPtr<FTabManager> TabManager;
};

USTRUCT(BlueprintType)
struct TOOLMENUS_API FToolMenuContext
{
	GENERATED_BODY()
public:

	FToolMenuContext();
	FToolMenuContext(UObject* InContext);
	FToolMenuContext(TSharedPtr<FUICommandList> InCommandList, TSharedPtr<FExtender> InExtender = TSharedPtr<FExtender>(), UObject* InContext = nullptr);

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
	const FUIAction* GetActionForCommand(TSharedPtr<const FUICommandInfo> Command) const;

	void AddExtender(const TSharedPtr<FExtender>& InExtender);
	TSharedPtr<FExtender> GetAllExtenders();
	void ReplaceExtenders(const TSharedPtr<FExtender>& InExtender);
	void ResetExtenders();

	void AppendObjects(const TArray<UObject*>& InObjects);
	void AddObject(UObject* InObject);

	friend class UToolMenus;
	friend struct FToolMenuEntry;

private:

	UPROPERTY()
	TArray<UObject*> ContextObjects;

	TArray<TSharedPtr<FUICommandList>> CommandLists;

	TSharedPtr<FUICommandList> CommandList;

	TArray<TSharedPtr<FExtender>> Extenders;
};

