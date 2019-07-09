// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/UIAction.h"

#include "EditorMenuMisc.generated.h"

struct FEditorMenuContext;

UENUM(BlueprintType)
enum class EEditorMenuStringCommandType : uint8
{
	Command,
	Python,
	Custom
};

USTRUCT(BlueprintType)
struct EDITORMENUS_API FEditorMenuStringCommand
{
	GENERATED_BODY()

	// Which command handler to use
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	EEditorMenuStringCommandType Type;

	// Which command handler to use when type is custom
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	FName CustomType;

	// String to pass to command handler
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	FString String;

private:

	friend class UEditorMenuSubsystem;

	bool IsBound() const { return String.Len() > 0; }

	FExecuteAction ToExecuteAction(const FEditorMenuContext& Context) const;

	FName GetTypeName() const;
};

UENUM(BlueprintType)
enum class EEditorMenuInsertType : uint8
{
	Default,
	Before,
	After,
	First
};

USTRUCT(BlueprintType)
struct FEditorMenuInsert
{
	GENERATED_BODY()

	FEditorMenuInsert() : Position(EEditorMenuInsertType::Default) {}
	FEditorMenuInsert(FName InName, EEditorMenuInsertType InPosition) : Name(InName), Position(InPosition) {}

	// Where to insert
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	FName Name;

	// How to insert
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	EEditorMenuInsertType Position;

	FORCEINLINE bool operator==(const FEditorMenuInsert& Other) const
	{
		return Other.Name == Name && Other.Position == Position;
	}

	FORCEINLINE bool operator!=(const FEditorMenuInsert& Other) const
	{
		return Other.Name != Name || Other.Position != Position;
	}

	bool IsDefault() const
	{
		return Position == EEditorMenuInsertType::Default;
	}

	bool IsBeforeOrAfter() const
	{
		return Position == EEditorMenuInsertType::Before || Position == EEditorMenuInsertType::After;
	}
};

