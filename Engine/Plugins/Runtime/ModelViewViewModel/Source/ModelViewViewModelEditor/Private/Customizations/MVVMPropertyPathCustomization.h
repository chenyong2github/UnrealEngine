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

	enum class EPropertyPathType
	{
		Widget,
		ViewModel
	};

	class FPropertyPathCustomizationBase : public IPropertyTypeCustomization
	{
	protected:
		FPropertyPathCustomizationBase(UWidgetBlueprint* WidgetBlueprint, EPropertyPathType PathType);
		~FPropertyPathCustomizationBase();

	public:
		virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
		virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override {}

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
		EPropertyPathType PathType;
		UWidgetBlueprint* WidgetBlueprint = nullptr;

		TSharedPtr<SMVVMSourceSelector> SourceSelector;
		TSharedPtr<SMVVMFieldSelector> FieldSelector;

		TArray<TSharedPtr<IFieldPathHelper>> PathHelpers;
		TArray<TSharedPtr<IFieldPathHelper>> OtherHelpers;

		bool bPropertySelectionChanging = false;

		FDelegateHandle OnBlueprintChangedHandle;
	};

	class FWidgetPropertyPathCustomization : public FPropertyPathCustomizationBase
	{
	public:
		static TSharedRef<IPropertyTypeCustomization> MakeInstance(UWidgetBlueprint* InWidgetBlueprint)
		{
			check(InWidgetBlueprint != nullptr);
			TSharedRef<FWidgetPropertyPathCustomization> Customization = MakeShared<FWidgetPropertyPathCustomization>(InWidgetBlueprint);
			return Customization;
		}
	
		FWidgetPropertyPathCustomization(UWidgetBlueprint* WidgetBlueprint) : 
			FPropertyPathCustomizationBase(WidgetBlueprint, EPropertyPathType::Widget)
		{
		}
	};

	class FViewModelPropertyPathCustomization : public FPropertyPathCustomizationBase
	{
	public:
		static TSharedRef<IPropertyTypeCustomization> MakeInstance(UWidgetBlueprint* InWidgetBlueprint)
		{
			check(InWidgetBlueprint != nullptr);
			TSharedRef<FViewModelPropertyPathCustomization> Customization = MakeShared<FViewModelPropertyPathCustomization>(InWidgetBlueprint);
			return Customization;
		}

		FViewModelPropertyPathCustomization(UWidgetBlueprint* WidgetBlueprint) :
			FPropertyPathCustomizationBase(WidgetBlueprint, EPropertyPathType::ViewModel)
		{
		}
	};
}