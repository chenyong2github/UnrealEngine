// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Layout/Visibility.h"
#include "Types/SlateEnums.h"
#include "ScopedTransaction.h"

#include "SplineMetadataDetailsFactory.h"

#include "VPSplineMetadataDetails.generated.h"

UCLASS()
class UVPSplineMetadataDetailsFactory : public USplineMetadataDetailsFactoryBase
{
	GENERATED_BODY()

	public:
	virtual ~UVPSplineMetadataDetailsFactory() {}
	virtual TSharedPtr<ISplineMetadataDetails> Create() override;
	virtual UClass* GetMetadataClass() const override;
};

class FVPSplineMetadataDetails : public ISplineMetadataDetails, public TSharedFromThis<FVPSplineMetadataDetails>
{
public:
	virtual ~FVPSplineMetadataDetails() {}
	virtual FName GetName() const override { return FName("VPSplineMetadataDetails"); }
	virtual FText GetDisplayName() const override;
	virtual void Update(USplineComponent* InSplineComponent, const TSet<int32>& InSelectedKeys) override;
	virtual void GenerateChildContent(IDetailGroup& InGroup) override;

	TOptional<float> NormalizedPositionValue;
	TOptional<float> FocalLengthValue;
	TOptional<float> ApertureValue;
	TOptional<float> FocusDistanceValue;

	USplineComponent* SplineComp = nullptr;
	TSet<int32> SelectedKeys;

private:
	void OnBeginSliderMovement();
	void OnEndSliderMovement(float NewValue);

	EVisibility IsEnabled() const { return (SelectedKeys.Num() > 0) ? EVisibility::Visible : EVisibility::Collapsed; }
	bool IsOnePointSelected() const { return SelectedKeys.Num() == 1; }


	TOptional<float> GetNormalizedPosition() const { return NormalizedPositionValue; }
	void OnSetNormalizedPosition(float NewValue, ETextCommit::Type CommitInfo);

	TOptional<float> GetFocalLength() const { return FocalLengthValue; }
	void OnSetFocalLength(float NewValue, ETextCommit::Type CommitInfo);

	TOptional<float> GetAperture() const { return ApertureValue; }
	void OnSetAperture(float NewValue, ETextCommit::Type CommitInfo);

	TOptional<float> GetFocusDistance() const { return FocusDistanceValue; }
	void OnSetFocusDistance(float NewValue, ETextCommit::Type CommitInfo);

	class UVPSplineMetadata* GetMetadata() const;
private:
	TUniquePtr<FScopedTransaction> EditSliderValueTransaction;
};
