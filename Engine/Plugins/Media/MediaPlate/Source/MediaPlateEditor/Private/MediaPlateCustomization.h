// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Editor.h"
#include "IDetailCustomization.h"
#include "Input/Reply.h"

class AMediaPlate;
class IDetailLayoutBuilder;

/**
 * Implements a details view customization for the AMediaPlate class.
 */
class FMediaPlateCustomization
	: public IDetailCustomization
{
public:

	//~ IDetailCustomization interface

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	/**
	 * Creates an instance of this class.
	 *
	 * @return The new instance.
	 */
	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShareable(new FMediaPlateCustomization());
	}

private:

	/** List of the actors we are editing. */
	TArray<TWeakObjectPtr<AMediaPlate>> ActorsList;

	/**
	 * Called when the open media plate button is pressed.
	 */
	FReply OnOpenMediaPlate();

};

