// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Misc/EnumClassFlags.h"
#include "Containers/UnrealString.h"

class FCurveEditorTree;
struct FCurveEditorTreeItem;
struct FCurveEditorTreeItemID;
struct FCurveEditorFilterStates;
template<typename> class TArrayView;
enum class ECurveEditorTreeFilterState : uint8;

enum class ECurveEditorTreeFilterType : uint32
{
	/** Filter is a FCurveEditorTreeTextFilter instance */
	Text,

	CUSTOM_START,


	First = Text,
};



struct CURVEEDITOR_API FCurveEditorTreeFilter
{
	explicit FCurveEditorTreeFilter(ECurveEditorTreeFilterType InFilterType)
		: FilterType(InFilterType)
	{}

	virtual ~FCurveEditorTreeFilter() {}

	/**
	 * @return The type of this filter as registered by RegisterFilterType
	 */
	ECurveEditorTreeFilterType GetType() const
	{
		return FilterType;
	}

public:

	/**
	 * Register a new filter type that is passed to ICurveEditorTreeItem::Filter
	 *
	 * @param InTreeItem The tree item to test
	 * @return true if the item matches the filter, false otherwise
	 */
	static ECurveEditorTreeFilterType RegisterFilterType();

protected:

	/** The static type of this filter as retrieved by RegisterFilterType */
	ECurveEditorTreeFilterType FilterType;
};

/**
 * Built-in text filter of type ECurveEditorTreeFilterType::Text. Filter terms are applied as a case-insensitive boolean OR substring match.
 */
struct FCurveEditorTreeTextFilter : FCurveEditorTreeFilter
{
	/** Array of case-insensitive terms to find within tree items. */
	TArray<FString> FilterTerms;

	FCurveEditorTreeTextFilter()
		: FCurveEditorTreeFilter(ECurveEditorTreeFilterType::Text)
	{}

	/**
	 * Check whether the supplied string matches any of the terms
	 */
	bool Match(const TCHAR* InString) const
	{
		for (const FString& Term : FilterTerms)
		{
			if (FCString::Stristr(InString, *Term) != nullptr)
			{
				return true;
			}
		}
		return false;
	}
};