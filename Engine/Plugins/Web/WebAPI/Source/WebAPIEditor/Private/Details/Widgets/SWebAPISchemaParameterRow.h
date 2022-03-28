// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SWebAPISchemaTreeTableRow.h"
#include "Details/ViewModels/WebAPIParameterViewModel.h"

class SWebAPISchemaParameterRow
	: public SWebAPISchemaTreeTableRow<FWebAPIParameterViewModel>
{
public:
	SLATE_BEGIN_ARGS(SWebAPISchemaParameterRow)
	{ }
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, const TSharedRef<FWebAPIParameterViewModel>& InViewModel, const TSharedRef<STableViewBase>& InOwnerTableView);
};
