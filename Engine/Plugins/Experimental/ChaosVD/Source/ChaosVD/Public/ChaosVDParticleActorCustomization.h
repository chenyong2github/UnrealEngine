// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "IDetailCustomization.h"

/** Custom details panel for the ChaosVD Particle Actor */
class FChaosVDParticleActorCustomization : public IDetailCustomization
{
public:
	
	inline static FName ChaosVDCategoryName = FName("Chaos Visual Debugger Data");

	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
