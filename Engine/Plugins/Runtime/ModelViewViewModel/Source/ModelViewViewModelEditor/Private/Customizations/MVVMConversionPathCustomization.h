// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPropertyTypeCustomization.h"
#include "Layout/Visibility.h"
#include "MVVMPropertyPath.h"
#include "Styling/SlateTypes.h"
#include "Templates/ValueOrError.h"
#include "Types/MVVMBindingMode.h"
#include "Types/MVVMBindingSource.h"
#include "Types/MVVMFieldVariant.h"

struct FMVVMPropertyPathBase;
class FStructOnScope;
class SGraphPin;
class UWidgetBlueprint;

namespace UE::MVVM
{

class SFieldSelector;

class FConversionPathCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance(UWidgetBlueprint* InWidgetBlueprint)
	{
		return MakeShared<FConversionPathCustomization>(InWidgetBlueprint);
	}

	FConversionPathCustomization(UWidgetBlueprint* InWidgetBlueprint);

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	FText GetFunctionPathText(TSharedRef<IPropertyHandle> Property) const;
	void AddRowForProperty(IDetailChildrenBuilder& ChildBuilder, const TSharedPtr<IPropertyHandle>& Property, bool bSourceToDestination);

	void OnTextCommitted(const FText& NewValue, ETextCommit::Type CommitType, TSharedRef<IPropertyHandle> Property, bool bSourceToDestination);
	void OnFunctionPathChanged(const UFunction* NewFunction, TSharedRef<IPropertyHandle> Property, bool bSourceToDestination);
	void RefreshDetailsView() const;

	FMVVMBlueprintPropertyPath OnGetSelectedField(FName ArgumentName, bool bSourceToDestination) const;
	void OnSetProperty(FMVVMBlueprintPropertyPath NewSelection, FName ArgumentName, bool bSourceToDestination);

	ECheckBoxState OnGetIsArgumentBound(FName ArgumentName, bool bSourceToDestination) const;
	void OnCheckedBindArgument(ECheckBoxState CheckState, FName ArgumentName, bool bSourceToDestination);
	EVisibility GetArgumentWidgetVisibility(FName ArgumentName, bool bSourceToDestination, bool bDefaultValue) const;

	FEdGraphPinType GetArgumentPinType(FName ArgumentName, bool bSourceToDestination) const;

	EMVVMBindingMode GetBindingMode() const;

private:
	UWidgetBlueprint* WidgetBlueprint = nullptr;

	TSharedPtr<IPropertyHandle> ParentHandle;
	TSharedPtr<IPropertyHandle> BindingModeHandle;
	TWeakPtr<IPropertyUtilities> WeakPropertyUtilities;

	TArray<TSharedPtr<SGraphPin>> ArgumentPinWidgets; 
	TArray<TSharedPtr<UE::MVVM::SFieldSelector>> ArgumentFieldSelectors;
};

} // namespace UE::MVVM
