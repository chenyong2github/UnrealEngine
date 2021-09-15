// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomNodeBuilder.h"
#include "Engine/EngineTypes.h"
#include "Framework/SlateDelegates.h"
#include "Styling/SlateColor.h"

class IDetailCategoryBuilder;
class IPropertyHandle;
enum class ECheckBoxState : uint8;

// Helper class to create a Mobility customization for the specified Property in the specified CategoryBuilder.
class DETAILCUSTOMIZATIONS_API FMobilityCustomization : public IDetailCustomNodeBuilder, public TSharedFromThis<FMobilityCustomization>
{
public:
	enum
	{
		StaticMobilityBitMask = (1u << EComponentMobility::Static),
		StationaryMobilityBitMask = (1u << EComponentMobility::Stationary),
		MovableMobilityBitMask = (1u << EComponentMobility::Movable),
	};

	FMobilityCustomization(TSharedPtr<IPropertyHandle> InMobilityHandle, uint8 InRestrictedMobilityBits, bool InForLight);

	virtual void GenerateHeaderRowContent(FDetailWidgetRow& WidgetRow) override;
	virtual FName GetName() const override;
	virtual TSharedPtr<IPropertyHandle> GetPropertyHandle() const override { return MobilityHandle; }

private:

	TSharedRef<class SWidget> OnGenerateWidget(FName InMobilityName) const;
	void OnMobilityChanged(FName InMobilityName, ESelectInfo::Type);
	FText GetMobilityToolTip() const;
	EComponentMobility::Type GetActiveMobility() const;
	FText GetActiveMobilityText() const;
	FText GetActiveMobilityToolTip() const;

	TArray<FName> AllowedOptions;
	TSharedPtr<IPropertyHandle> MobilityHandle;
	bool bForLight;
	uint8 RestrictedMobilityBits;
};
