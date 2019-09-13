// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Framework/Commands/UIAction.h"

class SWidget;
class FDetailWidgetRow;

class DATATABLEEDITOR_API FDataTableRowUtils
{
public:
	static void AddSearchForReferencesContextMenu(FDetailWidgetRow& RowNameDetailWidget, FExecuteAction SearchForReferencesAction);

private:

};

