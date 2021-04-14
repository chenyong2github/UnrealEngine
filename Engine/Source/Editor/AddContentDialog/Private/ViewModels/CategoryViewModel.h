// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FSlateBrush;
enum class EContentSourceCategory:uint8;

/** A view model for displaying a content source category in the UI. */
class FCategoryViewModel
{
public:
	FCategoryViewModel();

	FCategoryViewModel(EContentSourceCategory InCategory);

	/** Gets the display name of the category. */
	FText GetText() const;

	inline bool operator==(const FCategoryViewModel& Other) const
	{
		return Category == Other.Category;
	}

	bool operator<(FCategoryViewModel const& Other) const
	{
		return SortID < Other.SortID;
	}

	uint32 GetTypeHash() const;

private:
	void Initialize();

private:
	EContentSourceCategory Category;
	FText Text;
	int SortID;
};

inline uint32 GetTypeHash(const FCategoryViewModel& CategoryViewModel)
{
	return CategoryViewModel.GetTypeHash();
}
