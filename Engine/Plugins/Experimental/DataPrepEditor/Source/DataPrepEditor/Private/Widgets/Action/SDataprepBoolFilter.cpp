// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Widgets/Action/SDataprepBoolFilter.h"

#include "SelectionSystem/DataprepBoolFilter.h"
#include "Widgets/Action/SDataprepFetcherSelector.h"

#include "Widgets/Layout/SBox.h"

void SDataprepBoolFilter::Construct(const FArguments& InArgs, UDataprepBoolFilter& InFilter)
{
	ChildSlot
	[
		SNew(SBox)
		.MinDesiredWidth( 400.f )
		.Padding( 5.f )
		[
			SNew( SDataprepFetcherSelector, InFilter )
		]
	];
}
