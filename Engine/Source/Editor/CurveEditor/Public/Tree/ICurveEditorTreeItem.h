// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Containers/Array.h"
#include "Templates/UniquePtr.h"

struct FCurveEditorTreeItemID;

class FName;
class SWidget;
class FCurveModel;
class FCurveEditor;

struct CURVEEDITOR_API ICurveEditorTreeItem
{
	virtual ~ICurveEditorTreeItem() {}
	virtual TSharedPtr<SWidget> GenerateCurveEditorTreeWidget(const FName& InColumnName, TWeakPtr<FCurveEditor> InCurveEditor, FCurveEditorTreeItemID InTreeItemID) = 0;

	virtual void CreateCurveModels(TArray<TUniquePtr<FCurveModel>>& OutCurveModels) = 0;

	struct FColumnNames
	{
		FName Label;
		FName PinHeader;

		CURVEEDITOR_API FColumnNames();
	};

	static const FColumnNames ColumnNames;
};