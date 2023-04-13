// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtr.h"

#include "WidgetChild.generated.h"

class UWidget;

/**
 * Represent a Widget present in the Tree Widget of the UserWidget
 */
USTRUCT()
struct UMG_API FWidgetChild
{
	GENERATED_BODY();
public:
	FWidgetChild();
	FWidgetChild(const class UUserWidget* Outer, FName InChildName);

	FName GetFName() const
	{
		return WidgetName;
	};

	UWidget* GetWidget() const
	{ 
		return WidgetPtr.Get(); 
	};

	/** Resolves the Widget ptr using it's name. */
	UWidget* Resolve(const class UWidgetTree* WidgetTree);

private:
	/** This either the widget to focus, OR the name of the function to call. */
	UPROPERTY(EditAnywhere, Category = "Interaction")
	FName WidgetName;

	UPROPERTY(Transient)
	TWeakObjectPtr<UWidget> WidgetPtr;
};
