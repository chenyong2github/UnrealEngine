// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "NiagaraBoundsCalculator.h"
#include "NiagaraDataSet.h"

template<bool bUsedWithSprites, bool bUsedWithMeshes, bool bUsedWithRibbons>
class FNiagaraBoundsCalculatorHelper : public FNiagaraBoundsCalculator
{
public:
	FNiagaraBoundsCalculatorHelper() = default;
	FNiagaraBoundsCalculatorHelper(const FVector& InMeshExtents)
		: MeshExtents(InMeshExtents)
	{}

	virtual void InitAccessors(FNiagaraDataSet& DataSet) override final
	{
		static const FName PositionName(TEXT("Position"));
		static const FName SpriteSizeName(TEXT("SpriteSize"));
		static const FName ScaleName(TEXT("Scale"));
		static const FName RibbonWidthName(TEXT("RibbonWidth"));

		if (DataSet.HasVariable(PositionName))
		{
			PositionAccessor = FNiagaraDataSetAccessor<FNiagaraDataConversions<FVector>>(DataSet, PositionName);
		}
		else
		{
			PositionAccessor = FNiagaraDataSetAccessor<FNiagaraDataConversions<FVector>>();
		}

		if (bUsedWithSprites && DataSet.HasVariable(SpriteSizeName))
		{
			SpriteSizeAccessor = FNiagaraDataSetAccessor<FNiagaraDataConversions<FVector2D>>(DataSet, SpriteSizeName);
		}
		else
		{
			SpriteSizeAccessor = FNiagaraDataSetAccessor<FNiagaraDataConversions<FVector2D>>();
		}

		if (bUsedWithMeshes && DataSet.HasVariable(ScaleName))
		{
			ScaleAccessor = FNiagaraDataSetAccessor<FNiagaraDataConversions<FVector>>(DataSet, ScaleName);
		}
		else
		{
			ScaleAccessor = FNiagaraDataSetAccessor<FNiagaraDataConversions<FVector>>();
		}

		if (bUsedWithRibbons && DataSet.HasVariable(RibbonWidthName))
		{
			RibbonWidthAccessor = FNiagaraDataSetAccessor<FNiagaraDataConversions<float>>(DataSet, RibbonWidthName);
		}
		else
		{
			RibbonWidthAccessor = FNiagaraDataSetAccessor<FNiagaraDataConversions<float>>();
		}
	}

	virtual void RefreshAccessors() override final
	{
		PositionAccessor.InitForAccess();

		if (bUsedWithSprites)
		{
			SpriteSizeAccessor.InitForAccess();
		}
		if (bUsedWithMeshes)
		{
			ScaleAccessor.InitForAccess();
		}
		if (bUsedWithRibbons)
		{
			RibbonWidthAccessor.InitForAccess();
		}
	}

	virtual FBox CalculateBounds(const int32 NumInstances) const override final
	{
		if (!NumInstances || !PositionAccessor.IsValid())
		{
			return FBox(ForceInit);
		}

		constexpr float kDefaultSize = 50.0f;


		FNiagaraMinMaxStruct MinMaxXs = PositionAccessor.GetMinMaxElement(0);
		FNiagaraMinMaxStruct MinMaxYs = PositionAccessor.GetMinMaxElement(1);
		FNiagaraMinMaxStruct MinMaxZs = PositionAccessor.GetMinMaxElement(2);

		const FVector PositionMax = FVector(MinMaxXs.Max, MinMaxYs.Max, MinMaxZs.Max);
		const FVector PositionMin = FVector(MinMaxXs.Min, MinMaxYs.Min, MinMaxZs.Min);

		float MaxSize = KINDA_SMALL_NUMBER;
		if (bUsedWithMeshes)
		{
			FVector MaxScale = FVector(kDefaultSize, kDefaultSize, kDefaultSize);
			if (ScaleAccessor.IsValid())
			{
				MaxScale = FVector
				(
					ScaleAccessor.GetMaxElement(0),
					ScaleAccessor.GetMaxElement(1),
					ScaleAccessor.GetMaxElement(2)
				);
			}

			const FVector ScaledExtents = MeshExtents * (MaxScale.IsNearlyZero() ? FVector::OneVector : MaxScale);
			MaxSize = FMath::Max(MaxSize, ScaledExtents.GetMax());
		}

		if (bUsedWithSprites)
		{
			float MaxSpriteSize = kDefaultSize;

			if (SpriteSizeAccessor.IsValid())
			{
				MaxSpriteSize = FMath::Max(SpriteSizeAccessor.GetMaxElement(0), SpriteSizeAccessor.GetMaxElement(1));
			}
			MaxSize = FMath::Max(MaxSize, FMath::IsNearlyZero(MaxSpriteSize) ? 1.0f : MaxSpriteSize);
		}

		if (bUsedWithRibbons)
		{
			float MaxRibbonWidth = kDefaultSize;
			if (RibbonWidthAccessor.IsValid())
			{
				MaxRibbonWidth = RibbonWidthAccessor.GetMaxElement(0);
			}

			MaxSize = FMath::Max(MaxSize, FMath::IsNearlyZero(MaxRibbonWidth) ? 1.0f : MaxRibbonWidth);
		}

		return FBox(PositionMin, PositionMax).ExpandBy(MaxSize);
	}

	FNiagaraDataSetAccessor<FNiagaraDataConversions<FVector>> PositionAccessor;
	FNiagaraDataSetAccessor<FNiagaraDataConversions<FVector2D>> SpriteSizeAccessor;
	FNiagaraDataSetAccessor<FNiagaraDataConversions<FVector>> ScaleAccessor;
	FNiagaraDataSetAccessor<FNiagaraDataConversions<float>> RibbonWidthAccessor;
	const FVector MeshExtents = FVector::OneVector;
};
