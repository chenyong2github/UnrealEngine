// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPropertyTypeCustomization.h"

class UWidgetBlueprint;

namespace UE::MVVM
{
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
		FText GetSourceToDestinationPath() const;
		FText GetDestinationToSourcePath() const;

		void OnTextCommitted(const FText& NewValue, ETextCommit::Type CommitType, bool bSourceToDestination);
		void OnFunctionPathChanged(const UFunction* NewPath, bool bSourceToDestination);

	private:
		UWidgetBlueprint* WidgetBlueprint = nullptr;
		TSharedPtr<IPropertyHandle> DestinationToSourceProperty;
		TSharedPtr<IPropertyHandle> SourceToDestinationProperty;
	};
}