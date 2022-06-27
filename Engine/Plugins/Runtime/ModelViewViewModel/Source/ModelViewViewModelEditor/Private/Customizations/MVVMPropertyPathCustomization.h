// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPropertyTypeCustomization.h"
#include "MVVMBlueprintView.h"
#include "PropertyHandle.h"
#include "Types/MVVMFieldVariant.h"

class SMVVMSourceSelector;
class SMVVMFieldSelector;

namespace UE::MVVM
{
	struct FBindingSource;

	class FPropertyPathCustomization : public IPropertyTypeCustomization
	{
	public:
		FPropertyPathCustomization(UWidgetBlueprint* WidgetBlueprint);
		~FPropertyPathCustomization();

		virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
		virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override {}

		static TSharedRef<IPropertyTypeCustomization> MakeInstance(UWidgetBlueprint* InWidgetBlueprint)
		{
			check(InWidgetBlueprint != nullptr);

			return MakeShared<FPropertyPathCustomization>(InWidgetBlueprint);
		}

	private:
		void OnSourceSelectionChanged(FBindingSource Selected);
		void OnPropertySelectionChanged(FMVVMBlueprintPropertyPath Selected);

		void OnOtherPropertyChanged();

		EMVVMBindingMode GetCurrentBindingMode() const;
		TArray<UE::MVVM::FBindingSource> OnGetSources() const;
		UE::MVVM::FBindingSource OnGetSelectedSource() const;

		TArray<FMVVMBlueprintPropertyPath> OnGetFields() const;
		FMVVMBlueprintPropertyPath OnGetSelectedField() const;

		void HandleBlueprintChanged(UBlueprint* Blueprint);

	private:
		TSharedPtr<IPropertyHandle> PropertyHandle;
		UWidgetBlueprint* WidgetBlueprint = nullptr;

		TSharedPtr<SMVVMSourceSelector> SourceSelector;
		TSharedPtr<SMVVMFieldSelector> FieldSelector;

		bool bIsWidget = false;
		bool bPropertySelectionChanging = false;

		FDelegateHandle OnBlueprintChangedHandle;
	};
}