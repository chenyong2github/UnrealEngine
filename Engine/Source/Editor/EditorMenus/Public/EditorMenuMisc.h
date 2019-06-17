// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "EditorMenuMisc.generated.h"

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
	GENERATED_USTRUCT_BODY()

	FEditorMenuInsert() : Position(EEditorMenuInsertType::Default) {}
	FEditorMenuInsert(FName InName, EEditorMenuInsertType InPosition) : Name(InName), Position(InPosition) {}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	FName Name;

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
