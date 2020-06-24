// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "NiagaraBoundsCalculator.h"
#include "NiagaraDataSet.h"
#include "NiagaraDataSetAccessor.h"

template<bool bUsedWithSprites, bool bUsedWithMeshes, bool bUsedWithRibbons>
class FNiagaraBoundsCalculatorHelper : public FNiagaraBoundsCalculator
{
public:
	FNiagaraBoundsCalculatorHelper() = default;
	FNiagaraBoundsCalculatorHelper(const FVector& InMeshExtents)
		: MeshExtents(InMeshExtents)
	{}

	virtual void InitAccessors(const FNiagaraDataSetCompiledData& CompiledData) override final
	{
		static const FName PositionName(TEXT("Position"));
		static const FName SpriteSizeName(TEXT("SpriteSize"));
		static const FName ScaleName(TEXT("Scale"));
		static const FName RibbonWidthName(TEXT("RibbonWidth"));

		PositionAccessor.Init(CompiledData, PositionName);
		if (bUsedWithSprites)
		{
			SpriteSizeAccessor.Init(CompiledData, SpriteSizeName);
		}
		if (bUsedWithMeshes)
		{
			ScaleAccessor.Init(CompiledData, ScaleName);
		}
		if (bUsedWithRibbons)
		{
			RibbonWidthAccessor.Init(CompiledData, RibbonWidthName);
		}
	}

	virtual FBox CalculateBounds(const FNiagaraDataSet& DataSet, const int32 NumInstances) const override final
	{
		if (!NumInstances || !PositionAccessor.IsValid())
		{
			return FBox(ForceInit);
		}

		constexpr float kDefaultSize = 50.0f;

		FVector PositionMax;
		FVector PositionMin;
		PositionAccessor.GetReader(DataSet).GetMinMax(PositionMin, PositionMax);

		float MaxSize = KINDA_SMALL_NUMBER;
		if (bUsedWithMeshes)
		{
			FVector MaxScale = FVector(kDefaultSize, kDefaultSize, kDefaultSize);
			if (ScaleAccessor.IsValid())
			{
				MaxScale = ScaleAccessor.GetReader(DataSet).GetMax();
			}

			const FVector ScaledExtents = MeshExtents * (MaxScale.IsNearlyZero() ? FVector::OneVector : MaxScale);
			MaxSize = FMath::Max(MaxSize, ScaledExtents.GetMax());
		}

		if (bUsedWithSprites)
		{
			float MaxSpriteSize = kDefaultSize;

			if (SpriteSizeAccessor.IsValid())
			{
				const FVector2D MaxSpriteSize2D = SpriteSizeAccessor.GetReader(DataSet).GetMax();
				MaxSpriteSize = FMath::Max(MaxSpriteSize2D.X, MaxSpriteSize2D.Y);
			}
			MaxSize = FMath::Max(MaxSize, FMath::IsNearlyZero(MaxSpriteSize) ? 1.0f : MaxSpriteSize);
		}

		if (bUsedWithRibbons)
		{
			float MaxRibbonWidth = kDefaultSize;
			if (RibbonWidthAccessor.IsValid())
			{
				MaxRibbonWidth = RibbonWidthAccessor.GetReader(DataSet).GetMax();
			}

			MaxSize = FMath::Max(MaxSize, FMath::IsNearlyZero(MaxRibbonWidth) ? 1.0f : MaxRibbonWidth);
		}

		return FBox(PositionMin, PositionMax).ExpandBy(MaxSize);
	}

	FNiagaraDataSetAccessor<FVector> PositionAccessor;
	FNiagaraDataSetAccessor<FVector2D> SpriteSizeAccessor;
	FNiagaraDataSetAccessor<FVector> ScaleAccessor;
	FNiagaraDataSetAccessor<float> RibbonWidthAccessor;

	const FVector MeshExtents = FVector::OneVector;
};
