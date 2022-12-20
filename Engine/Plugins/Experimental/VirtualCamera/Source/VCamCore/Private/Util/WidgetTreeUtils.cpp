// Copyright Epic Games, Inc. All Rights Reserved.

#include "Util/WidgetTreeUtils.h"

#include "Blueprint/WidgetTree.h"
#include "UI/VCamWidget.h"

namespace UE::VCamCore
{
	void ForEachWidgetToConsiderForVCam(UUserWidget& Widget, TFunctionRef<void(UWidget* Widget)> Callback)
	{
		if (!Widget.WidgetTree)
		{
			return;
		}

		Callback(&Widget);
					
		TQueue<UWidgetTree*> SearchQueue;
		SearchQueue.Enqueue(Widget.WidgetTree);
		UWidgetTree* CurrentTree;
		while (SearchQueue.Dequeue(CurrentTree))
		{
			CurrentTree->ForEachWidget([&SearchQueue, Callback](UWidget* Widget)
			{
				Callback(Widget);
				if (UUserWidget* UserWidget = Cast<UUserWidget>(Widget); UserWidget && UserWidget->WidgetTree)
				{
					SearchQueue.Enqueue(UserWidget->WidgetTree);
				}
			});
		}
	}
}
