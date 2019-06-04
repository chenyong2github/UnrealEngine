// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Insights/ViewModels/TimersViewColumn.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FTimersViewColumns
{
	//////////////////////////////////////////////////
	// Column identifiers

	static const FName NameColumnID;
	static const FName MetaGroupNameColumnID;
	static const FName TypeColumnID;
	static const FName InstanceCountColumnID;

	// Inclusive Time columns
	static const FName TotalInclusiveTimeColumnID;
	static const FName MaxInclusiveTimeColumnID;
	static const FName UpperQuartileInclusiveTimeColumnID;
	static const FName AverageInclusiveTimeColumnID;
	static const FName MedianInclusiveTimeColumnID;
	static const FName LowerQuartileInclusiveTimeColumnID;
	static const FName MinInclusiveTimeColumnID;

	// Exclusive Time columns
	static const FName TotalExclusiveTimeColumnID;
	static const FName MaxExclusiveTimeColumnID;
	static const FName UpperQuartileExclusiveTimeColumnID;
	static const FName AverageExclusiveTimeColumnID;
	static const FName MedianExclusiveTimeColumnID;
	static const FName LowerQuartileExclusiveTimeColumnID;
	static const FName MinExclusiveTimeColumnID;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FTimersViewColumnFactory
{
private:
	/** Default constructor. */
	FTimersViewColumnFactory();

	/** Destructor. */
	~FTimersViewColumnFactory();

public:
	/** Contains basic information about columns used in the Timers view widget. Names should be localized. */
	TArray<FTimersViewColumn*> Collection;

	/** Mapping between column IDs and FTimersViewColumn pointers. */
	TMap<FName, const FTimersViewColumn*> ColumnIdToPtrMapping;

	/** Singleton */
	static const FTimersViewColumnFactory& Get();
};

////////////////////////////////////////////////////////////////////////////////////////////////////
