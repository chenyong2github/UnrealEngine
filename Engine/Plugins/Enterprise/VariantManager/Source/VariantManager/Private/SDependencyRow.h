// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Variant.h"

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Widgets/Views/STableRow.h"

struct FColumnSizeData;
struct FVariantDependency;

// Adapter so that we can use arrays of these objects on SListViews and still reference the original Dependency
struct FVariantDependencyModel : TSharedFromThis<FVariantDependencyModel>
{
	TWeakObjectPtr<UVariant> ParentVariant;
	FVariantDependency* Dependency;
};
using FVariantDependencyModelPtr = TSharedPtr<FVariantDependencyModel>;

class SDependencyRow : public STableRow<FVariantDependencyModelPtr>
{
public:
	SLATE_BEGIN_ARGS(SDependencyRow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, FColumnSizeData& InDependenciesColumnData, FVariantDependencyModelPtr InDependencyModel, bool bInteractionEnabled);

private:
	void OnSelectedVariantSetChanged(TSharedPtr<FText> NewItem, ESelectInfo::Type SelectType);
	void OnSelectedVariantChanged(TSharedPtr<FText> NewItem, ESelectInfo::Type SelectType);

	FText GetSelectedVariantSetOption() const;
	FText GetSelectedVariantOption() const;

	FText GetDependentVariantSetText() const;
	FText GetDependentVariantText() const;

	void RebuildVariantSetOptions();
	void RebuildVariantOptions();

	FReply OnDeleteRowClicked();
	FReply OnEnableRowToggled();

private:
	TArray<TSharedPtr<FText>> VariantSetOptions;
	TArray<TSharedPtr<FText>> VariantOptions;

	TWeakObjectPtr<UVariant> ParentVariantPtr;
	FVariantDependency* Dependency;
};