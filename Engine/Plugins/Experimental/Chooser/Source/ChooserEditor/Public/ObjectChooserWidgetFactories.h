// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "SClassViewer.h"

namespace UE::ChooserEditor
{
	typedef TFunction<void(const UObject* Object, FText& OutText)> FChooserTextConverter;
	typedef TFunction<TSharedRef<SWidget>(UObject* Object)> FChooserWidgetCreator;

	class CHOOSEREDITOR_API FObjectChooserWidgetFactories
	{
	public:
		static TMap<const UClass*, FChooserTextConverter> ChooserTextConverter;
		static TMap<const UClass*, FChooserWidgetCreator> ChooserWidgetCreators;

		static TSharedPtr<SWidget> CreateWidget(FName DataInterfaceTypeName, UObject* Value, const FOnClassPicked& CreateClassCallback, TSharedPtr<SBorder>* InnerWidget = nullptr);
		
		static void RegisterWidgets();
	};
}
