// Copyright Epic Games, Inc. All Rights Reserved.

#include "Types/UIFParentWidget.h"
#include "GameFramework/PlayerController.h"
#include "UIFWidget.h"
#include "UIFPlayerComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(UIFParentWidget)

/**
 *
 */
FUIFrameworkParentWidget::FUIFrameworkParentWidget(UUIFrameworkWidget* InWidget)
	: Parent(InWidget)
	, bIsParentAWidget(true)
{
}


FUIFrameworkParentWidget::FUIFrameworkParentWidget(UUIFrameworkPlayerComponent* InPlayer)
	: Parent(InPlayer)
	, bIsParentAWidget(false)
{
}


UUIFrameworkWidget* FUIFrameworkParentWidget::AsWidget() const
{
	return CastChecked<UUIFrameworkWidget>(Parent);
}


UUIFrameworkPlayerComponent* FUIFrameworkParentWidget::AsPlayerComponent() const
{
	return CastChecked<UUIFrameworkPlayerComponent>(Parent);
}
