// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Tree/CurveEditorTreeFilter.h"
#include "Tree/ICurveEditorTreeItem.h"
#include "Tree/CurveEditorTree.h"



ECurveEditorTreeFilterType FCurveEditorTreeFilter::RegisterFilterType()
{
	static ECurveEditorTreeFilterType NextFilterType = ECurveEditorTreeFilterType::CUSTOM_START;
	ensureMsgf(NextFilterType != ECurveEditorTreeFilterType::First, TEXT("Maximum limit for registered curve tree filters (64) reached."));
	if (NextFilterType == ECurveEditorTreeFilterType::First)
	{
		return NextFilterType;
	}

	ECurveEditorTreeFilterType ThisFilterType = NextFilterType;

	// When the custom view ID reaches 0x80000000 the left shift will result in well-defined unsigned integer wraparound, resulting in 0 (None)
	NextFilterType = ECurveEditorTreeFilterType( ((__underlying_type(ECurveEditorTreeFilterType))NextFilterType) + 1 );

	return NextFilterType;
}