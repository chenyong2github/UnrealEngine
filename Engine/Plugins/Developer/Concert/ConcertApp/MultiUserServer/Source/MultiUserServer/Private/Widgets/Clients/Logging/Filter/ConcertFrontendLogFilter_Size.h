// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertFrontendLogFilter.h"
#include "Math/UnitConversion.h"

enum class ESizeFilterMode
{
	/** Allow logs bigger than or equal to the specified size */
	BiggerThanOrEqual,
	/** Allow logs smaller than or equal to the specified size */
	LessThanOrEqual
};

/** Filters based on the log's size */
class FConcertLogFilter_Size : public FConcertLogFilter
{
public:
	
	//~ Begin FConcertLogFilter Interface
	virtual bool PassesFilter(const FConcertLogEntry& InItem) const override;
	//~ End FConcertLogFilter Interface

	void AdvanceFilterMode();
	void SetSizeInBytes(uint32 NewSizeInBytes);
	void SetDataUnit(EUnit NewUnit);
	
	ESizeFilterMode GetFilterMode() const { return FilterMode; }
	uint32 GetSizeInBytes() const { return SizeInBytes; }
	EUnit GetDataUnit() const { return DataUnit; }
	TSet<EUnit> GetAllowedUnits() const { return { EUnit::Bytes, EUnit::Kilobytes, EUnit::Megabytes }; }
	
private:

	ESizeFilterMode FilterMode = ESizeFilterMode::BiggerThanOrEqual;
	uint32 SizeInBytes = 0;
	EUnit DataUnit = EUnit::Bytes;
};

class FConcertFrontendLogFilter_Size : public TConcertFrontendLogFilterAggregate<FConcertLogFilter_Size>
{
	using Super = TConcertFrontendLogFilterAggregate<FConcertLogFilter_Size>;
public:
	
	FConcertFrontendLogFilter_Size();

private:

	TSharedRef<SWidget> MakeDataUnitMenu();
	FText GetSizeAndUnitAsText() const;
};
