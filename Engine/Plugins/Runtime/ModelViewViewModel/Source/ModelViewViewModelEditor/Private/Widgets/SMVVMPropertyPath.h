// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "MVVMBlueprintView.h"
#include "Widgets/SMVVMFieldIcon.h"
#include "MVVMPropertyPathHelpers.h"
#include "Types/MVVMFieldVariant.h"

class SMVVMPropertyPathBase : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMVVMPropertyPathBase)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const UWidgetBlueprint* WidgetBlueprint);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	virtual UE::MVVM::IFieldPathHelper& GetPathHelper() = 0;

private:
	const UWidgetBlueprint* WidgetBlueprint = nullptr;
	UE::MVVM::FMVVMConstFieldVariant SelectedField;
	TOptional<UE::MVVM::FBindingSource> SelectedSource;
};

class SMVVMWidgetPropertyPath : public SMVVMPropertyPathBase
{
public:
	SLATE_BEGIN_ARGS(SMVVMWidgetPropertyPath) {}
		SLATE_ARGUMENT(FMVVMBlueprintPropertyPath*, WidgetPath)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const UWidgetBlueprint* WidgetBlueprint);

private:
	virtual UE::MVVM::IFieldPathHelper& GetPathHelper() override
	{ 
		return PathHelper;
	}

private:
	UE::MVVM::FWidgetFieldPathHelper PathHelper;
};

class SMVVMViewModelPropertyPath : public SMVVMPropertyPathBase
{
public:
	SLATE_BEGIN_ARGS(SMVVMViewModelPropertyPath) {}
		SLATE_ARGUMENT(FMVVMBlueprintPropertyPath*, ViewModelPath)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const UWidgetBlueprint* WidgetBlueprint);

private:
	virtual UE::MVVM::IFieldPathHelper& GetPathHelper() override
	{
		return PathHelper;
	}

private:
	UE::MVVM::FViewModelFieldPathHelper PathHelper;
};
