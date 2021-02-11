// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EdGraph/EdGraphSchema.h"
#include "UObject/ObjectMacros.h"
#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"
#include "PropertyHandle.h"

#include "NiagaraFlipbookSettingsDetails.generated.h"

class IDetailLayoutBuilder;

USTRUCT()
struct FNiagaraFlipbookTextureSourceAction : public FEdGraphSchemaAction
{
	GENERATED_BODY()

	// Simple type info
	static FName StaticGetTypeId() { static FName Type("FNiagaraFlipbookTextureSourceAction"); return Type; }
	virtual FName GetTypeId() const override { return StaticGetTypeId(); }

	FNiagaraFlipbookTextureSourceAction()
		: FEdGraphSchemaAction()
	{}

	FNiagaraFlipbookTextureSourceAction(FName InBindingName, FText InNodeCategory, FText InMenuDesc, FText InToolTip, const int32 InGrouping, FText InKeywords)
		: FEdGraphSchemaAction(MoveTemp(InNodeCategory), MoveTemp(InMenuDesc), MoveTemp(InToolTip), InGrouping, MoveTemp(InKeywords))
		, BindingName(InBindingName)
	{}

	//~ Begin FEdGraphSchemaAction Interface
	virtual UEdGraphNode* PerformAction(class UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) override
	{
		return nullptr;
	}
	//~ End FEdGraphSchemaAction Interface

	FName BindingName;
};

class FNiagaraFlipbookTextureSourceDetails : public IPropertyTypeCustomization
{
public:
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override {}

	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShared<FNiagaraFlipbookTextureSourceDetails>();
	}

private:
	FText GetText() const;
	TSharedRef<SWidget> OnGetMenuContent() const;
	void CollectAllActions(FGraphActionListBuilderBase& OutAllActions) const;
	TSharedRef<SWidget> OnCreateWidgetForAction(struct FCreateWidgetForActionData* const InCreateData) const;
	void OnActionSelected(const TArray<TSharedPtr<FEdGraphSchemaAction>>& SelectedActions, ESelectInfo::Type InSelectionType) const;

private:
	TSharedPtr<IPropertyHandle> PropertyHandle;
};

///** Details customization for flipbook texture settings. */
//class FNiagaraFlipbookTextureSettingsDetails : public IPropertyTypeCustomization
//{
//public:
//	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
//	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
//
//	static TSharedRef<IDetailCustomization> MakeInstance();
//};
//
///** Details customization for flipbook settings. */
//class FNiagaraFlipbookSettingsDetails : public IDetailCustomization
//{
//public:
//	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
//	static TSharedRef<IDetailCustomization> MakeInstance();
//};
