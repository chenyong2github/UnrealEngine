// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "CineCameraRigRail.h"

namespace ETextCommit { enum Type : int; }

/**
 * Implements a details view customization for the ACineCameraRigRail
 */
class FCineCameraRigRailDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShared<FCineCameraRigRailDetails>();
	}

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	TOptional<float> CurrentPositionValue = 1.0f;

private:
	TWeakObjectPtr<ACineCameraRigRail> RigRailActorPtr;

	TOptional<float> GetCustomPosition() const;

	TOptional<float> GetCustomPositionSliderMinValue() const;
	TOptional<float> GetCustomPositionSliderMaxValue() const;
	void OnBeginCustomPositionSliderMovement();
	void OnEndCustomPositionSliderMovement(float NewValue);
	void OnCustomPositionCommitted(float NewValue, ETextCommit::Type CommitType);
	void OnCustomPositionChanged(float NewValue);

	bool bCustomPositionSliderStartedTransaction = false;
};
