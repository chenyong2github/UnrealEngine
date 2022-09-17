// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

#include "Components/SplineComponent.h"

#include "VPSplineMetadata.generated.h"

USTRUCT()
struct FVPSplineCurveDefaults
{
	GENERATED_BODY()

		FVPSplineCurveDefaults()
		: DefaultNormalizedPosition(-1.0f)
		, DefaultFocalLength(35.0f)
		, DefaultAperture(2.8f)
		, DefaultFocusDistance(100000.f)
	{};

	UPROPERTY(EditDefaultsOnly, Category = VPSplineCurveDefaults)
	float DefaultNormalizedPosition;

	UPROPERTY(EditDefaultsOnly, Category = VPSplineCurveDefaults)
	float DefaultFocalLength;

	UPROPERTY(EditDefaultsOnly, Category = VPSplineCurveDefaults)
	float DefaultAperture;

	UPROPERTY(EditDefaultsOnly, Category = VPSplineCurveDefaults)
	float DefaultFocusDistance;

};

UCLASS()
class VPSPLINE_API UVPSplineMetadata : public USplineMetadata
{
	GENERATED_BODY()
	
public:
	/** Insert point before index, lerping metadata between previous and next key values */
	virtual void InsertPoint(int32 Index, float t, bool bClosedLoop) override;
	/** Update point at index by lerping metadata between previous and next key values */
	virtual void UpdatePoint(int32 Index, float t, bool bClosedLoop) override;
	virtual void AddPoint(float InputKey) override;
	virtual void RemovePoint(int32 Index) override;
	virtual void DuplicatePoint(int32 Index) override;
	virtual void CopyPoint(const USplineMetadata* FromSplineMetadata, int32 FromIndex, int32 ToIndex) override;
	virtual void Reset(int32 NumPoints) override;
	virtual void Fixup(int32 NumPoints, USplineComponent* SplineComp) override;

	UPROPERTY(EditAnywhere, Category = "Point")
	FInterpCurveFloat NormalizedPosition;

	UPROPERTY(EditAnywhere, Category = "Camera")
	FInterpCurveFloat FocalLength;

	UPROPERTY(EditAnywhere, Category = "Camera")
	FInterpCurveFloat Aperture;

	UPROPERTY(EditAnywhere, Category = "Camera")
	FInterpCurveFloat FocusDistance;
};
