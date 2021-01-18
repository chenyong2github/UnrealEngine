// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISourceControlChangelist.h"

class FPerforceSourceControlChangelist : public ISourceControlChangelist
{
public:
	FPerforceSourceControlChangelist();

	explicit FPerforceSourceControlChangelist(int32 InChangelistNumber)
		: ChangelistNumber(InChangelistNumber)
	{
	}

	FPerforceSourceControlChangelist(const FPerforceSourceControlChangelist& InOther)
		: ChangelistNumber(InOther.ChangelistNumber)
	{
	}

	bool operator==(const FPerforceSourceControlChangelist& InOther) const
	{
		return ChangelistNumber == InOther.ChangelistNumber;
	}

	bool operator!=(const FPerforceSourceControlChangelist& InOther) const
	{
		return ChangelistNumber != InOther.ChangelistNumber;
	}

	bool IsDefault() const
	{
		return *this == DefaultChangelist;
	}

	friend FORCEINLINE uint32 GetTypeHash(const FPerforceSourceControlChangelist& PerforceChangelist)
	{
		return GetTypeHash(PerforceChangelist.ChangelistNumber);
	}

	FString ToString() const
	{
		return ChangelistNumber != DefaultChangelist.ChangelistNumber ? FString::FromInt(ChangelistNumber) : TEXT("default");
	}

public:
	static const FPerforceSourceControlChangelist DefaultChangelist;
	int32 ChangelistNumber;
};

typedef TSharedRef<class FPerforceSourceControlChangelist, ESPMode::ThreadSafe> FPerforceSourceControlChangelistRef;