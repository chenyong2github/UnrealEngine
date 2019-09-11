// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ToolMenuBase.generated.h"

class FMultiBox;

USTRUCT()
struct FCustomizedToolMenuEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FName Name;
};

USTRUCT()
struct FCustomizedToolMenuSection
{
	GENERATED_BODY()
		
	UPROPERTY()
	FName Name;

	UPROPERTY()
	TArray<FCustomizedToolMenuEntry> Entries;
};

USTRUCT()
struct SLATE_API FCustomizedToolMenu
{
	GENERATED_BODY()

	UPROPERTY()
	FName Name;

	UPROPERTY()
	TArray<FCustomizedToolMenuSection> Sections;

	UPROPERTY()
	TArray<FName> HiddenSections;

	UPROPERTY()
	TArray<FName> HiddenEntries;

	bool IsSectionHidden(const FName InSectionName) const;
	bool IsEntryHidden(const FName InSectionName) const;

	FCustomizedToolMenuSection* FindSection(const FName InSectionName);
	const FCustomizedToolMenuSection* FindSection(const FName InSectionName) const;

	FCustomizedToolMenuEntry* FindEntry(const FName InEntryName, FName* OutSectionName = nullptr);
	const FCustomizedToolMenuEntry* FindEntry(const FName InEntryName, FName* OutSectionName = nullptr) const;

	/* Updates re-positioning of sections and entries
	 * Does not need to update hidden state as MultiBlocks/Widgets do not store hidden state of each entry and section themselves
	 */
	void UpdateFromMultiBox(const TSharedRef<const FMultiBox>& InMultiBox);
};

UCLASS(Abstract)
class SLATE_API UToolMenuBase : public UObject
{
	GENERATED_BODY()

public:

	virtual bool IsEditing() const { return false; }
	virtual FName GetSectionName(const FName InEntryName) const { return NAME_None; }

	virtual FCustomizedToolMenu* FindMenuCustomization() const { return nullptr; }
	virtual FCustomizedToolMenu* AddMenuCustomization() const { return nullptr; }
};
