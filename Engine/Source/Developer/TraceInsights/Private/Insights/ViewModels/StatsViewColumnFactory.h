// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Insights/ViewModels/StatsViewColumn.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FStatsViewColumns
{
	// Column identifiers
	static const FName NameColumnID;
	static const FName MetaGroupNameColumnID;
	static const FName TypeColumnID;
	static const FName CountColumnID;
	static const FName SumColumnID;
	static const FName MaxColumnID;
	static const FName UpperQuartileColumnID;
	static const FName AverageColumnID;
	static const FName MedianColumnID;
	static const FName LowerQuartileColumnID;
	static const FName MinColumnID;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FStatsViewColumnFactory
{
private:
	/** Default constructor. */
	FStatsViewColumnFactory();

	/** Destructor. */
	~FStatsViewColumnFactory();

public:
	/** Contains basic information about columns used in the Stats Counters view widget. Names should be localized. */
	TArray<FStatsViewColumn*> Collection;

	/** Mapping between column IDs and FStatsViewColumn pointers. */
	TMap<FName, const FStatsViewColumn*> ColumnIdToPtrMapping;

	/** Singleton */
	static const FStatsViewColumnFactory& Get();
};

////////////////////////////////////////////////////////////////////////////////////////////////////
