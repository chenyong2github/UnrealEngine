// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprint/WidgetChild.h"

#include "CoreMinimal.h"
#include "Blueprint/WidgetTree.h"


FWidgetChild::FWidgetChild()
{}

FWidgetChild::FWidgetChild(const class UUserWidget* Outer, FName InChildName)
	:ChildName(InChildName)
{
	if (!ChildName.IsNone() && Outer)
	{
		ChildWidgetPtr = Outer->WidgetTree->FindWidget(ChildName);
	}
}

bool FWidgetChild::IsValid() const 
{
	return ChildName.IsNone() || ChildWidgetPtr != nullptr; 
}

bool FWidgetChild::Resolve(const UWidgetTree* WidgetTree)
{
	if (!ChildName.IsNone() && WidgetTree)
	{
		ChildWidgetPtr = WidgetTree->FindWidget(ChildName);
		return ChildWidgetPtr != nullptr;
	}
	
	ChildWidgetPtr = nullptr;
	return false;
}
