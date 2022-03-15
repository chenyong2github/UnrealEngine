// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SWebAPISchemaTreeTableRow.h"

class FWebAPIEnumViewModel;

class SWebAPISchemaEnumRow
	: public SWebAPISchemaTreeTableRow<FWebAPIEnumViewModel>
{
public:
	SLATE_BEGIN_ARGS(SWebAPISchemaEnumRow)
	{ }
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, const TSharedRef<FWebAPIEnumViewModel>& InViewModel, const TSharedRef<STableViewBase>& InOwnerTableView);
};
