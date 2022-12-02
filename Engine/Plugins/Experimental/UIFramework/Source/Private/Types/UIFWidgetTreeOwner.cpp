// Copyright Epic Games, Inc. All Rights Reserved.

#include "Types/UIFWidgetTreeOwner.h"
#include "Types/UIFWidgetTree.h"


FUIFrameworkWidgetTree& IUIFrameworkWidgetTreeOwner::GetWidgetTree()
{
	static FUIFrameworkWidgetTree Tmp;
	PURE_VIRTUAL(IUIFrameworkWidgetTreeOwner::GetWidgetTree, return Tmp;)
}