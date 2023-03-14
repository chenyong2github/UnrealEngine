// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "SClassViewer.h"

namespace UE::AnimNext
{
	struct FParamTypeHandle;
}

namespace UE::AnimNext::GraphEditor
{
	class ANIMNEXTGRAPHEDITOR_API FWidgetFactories
	{
	public:
		static TMap<const UClass*, TFunction<void (const UObject* Object, FText& OutText)>> AnimNextInterfaceTextConverter;
		static TMap<const UClass*, TFunction<TSharedRef<SWidget> (UObject* Object)>> AnimNextInterfaceWidgetCreators;

		static TSharedPtr<SWidget> CreateAnimNextInterfaceWidget(FParamTypeHandle TypeHandle, UObject* Value, const FOnClassPicked& CreateClassCallback, TSharedPtr<SBorder>* InnerWidget = nullptr);
		
		static void RegisterWidgets();
	};
}
