// Copyright Epic Games, Inc. All Rights Reserved.

#include "Types/UIFWidgetTreeOwner.h"
#include "Types/UIFWidgetTree.h"


FUIFrameworkWidgetTree& IUIFrameworkWidgetTreeOwner::GetWidgetTree()
{
	PURE_VIRTUAL(IUIFrameworkWidgetTreeOwner::GetWidgetTree, static FUIFrameworkWidgetTree Tmp; return Tmp;)
}