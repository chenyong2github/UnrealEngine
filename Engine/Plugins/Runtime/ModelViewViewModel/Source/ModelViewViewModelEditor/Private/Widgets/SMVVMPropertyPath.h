// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "MVVMBlueprintView.h"
#include "Widgets/SMVVMFieldIcon.h"
#include "Types/MVVMFieldVariant.h"

class SMVVMPropertyPath : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMVVMPropertyPath)
	{}
		SLATE_ARGUMENT(FMVVMBlueprintPropertyPath*, PropertyPath)
		SLATE_ARGUMENT(const UWidgetBlueprint*, WidgetBlueprint)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FText GetSourceDisplayName() const;
	FText GetFieldDisplayName() const;
	UE::MVVM::FMVVMConstFieldVariant GetLastField() const;

private:
	FMVVMBlueprintPropertyPath* PropertyPath = nullptr;
	TWeakObjectPtr<const UWidgetBlueprint> WidgetBlueprint = nullptr;
};
