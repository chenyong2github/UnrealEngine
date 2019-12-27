// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"

/////////////////////////////////////////////////////
// UPanelSlot

UPanelSlot::UPanelSlot(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITOR
bool UPanelSlot::IsDesignTime() const
{
	return Parent->IsDesignTime();
}
#endif

void UPanelSlot::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	// ReleaseSlateResources for Content unless the content is a UUserWidget as they are responsible for releasing their own content.
	if (Content && !Content->IsA<UUserWidget>())
	{
		Content->ReleaseSlateResources(bReleaseChildren);
	}
}
