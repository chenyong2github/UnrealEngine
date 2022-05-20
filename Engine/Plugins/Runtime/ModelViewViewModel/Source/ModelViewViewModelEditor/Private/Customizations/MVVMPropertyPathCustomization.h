// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPropertyTypeCustomization.h"
#include "MVVMBlueprintView.h"
#include "MVVMPropertyPathHelpers.h"
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
		void OnPropertySelectionChanged(FMVVMConstFieldVariant Selected);

		void OnOtherPropertyChanged();

		TArray<IFieldPathHelper*> GetRawPathHelpers() const;
		TArray<IFieldPathHelper*> GetRawOtherHelpers() const;
		
		EMVVMBindingMode GetCurrentBindingMode() const;

		void HandleBlueprintChanged(UBlueprint* Blueprint);

	private:
		TSharedPtr<IPropertyHandle> PropertyHandle;
		UWidgetBlueprint* WidgetBlueprint = nullptr;

		TSharedPtr<SMVVMSourceSelector> SourceSelector;
		TSharedPtr<SMVVMFieldSelector> FieldSelector;

		TArray<TSharedPtr<IFieldPathHelper>> PathHelpers;
		TArray<TSharedPtr<IFieldPathHelper>> OtherHelpers;

		bool bPropertySelectionChanging = false;

		FDelegateHandle OnBlueprintChangedHandle;
	};
}