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

		PositionAccessor = FNiagaraDataSetAccessor<FVector>(DataSet, FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), PositionName));

		if (bUsedWithSprites)
		{
			SpriteSizeAccessor = FNiagaraDataSetAccessor<FVector2D>(DataSet, FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), SpriteSizeName));
		}
		else
		{
			SpriteSizeAccessor = FNiagaraDataSetAccessor<FVector2D>();
		}

		if (bUsedWithMeshes)
		{
			ScaleAccessor = FNiagaraDataSetAccessor<FVector>(DataSet, FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), ScaleName));
		}
		else
		{
			ScaleAccessor = FNiagaraDataSetAccessor<FVector>();
		}

		if (bUsedWithRibbons)
		{
			RibbonWidthAccessor = FNiagaraDataSetAccessor<float>(DataSet, FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), RibbonWidthName));
		}
		else
		{
			RibbonWidthAccessor = FNiagaraDataSetAccessor<float>();
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

		const FVector PositionMax = FVector(
			GetFloatMax(MakeArrayView(PositionAccessor.GetX(), NumInstances)),
			GetFloatMax(MakeArrayView(PositionAccessor.GetY(), NumInstances)),
			GetFloatMax(MakeArrayView(PositionAccessor.GetZ(), NumInstances)));

		const FVector PositionMin = FVector(
			GetFloatMin(MakeArrayView(PositionAccessor.GetX(), NumInstances)),
			GetFloatMin(MakeArrayView(PositionAccessor.GetY(), NumInstances)),
			GetFloatMin(MakeArrayView(PositionAccessor.GetZ(), NumInstances)));

		float MaxSize = KINDA_SMALL_NUMBER;
		if (bUsedWithMeshes)
		{
			FVector MaxScale = FVector(kDefaultSize, kDefaultSize, kDefaultSize);
			if (ScaleAccessor.IsValid())
			{
				MaxScale = FVector(
					GetFloatMax(MakeArrayView(ScaleAccessor.GetX(), NumInstances)),
					GetFloatMax(MakeArrayView(ScaleAccessor.GetY(), NumInstances)),
					GetFloatMax(MakeArrayView(ScaleAccessor.GetZ(), NumInstances)));
			}

			const FVector ScaledExtents = MeshExtents * (MaxScale.IsNearlyZero() ? FVector::OneVector : MaxScale);
			MaxSize = FMath::Max(MaxSize, ScaledExtents.GetMax());
		}

		if (bUsedWithSprites)
		{
			float MaxSpriteSize = kDefaultSize;

			if (SpriteSizeAccessor.IsValid())
			{
				MaxSpriteSize = FMath::Max(
					GetFloatMax(MakeArrayView(SpriteSizeAccessor.GetX(), NumInstances)),
					GetFloatMax(MakeArrayView(SpriteSizeAccessor.GetY(), NumInstances)));
			}
			MaxSize = FMath::Max(MaxSize, FMath::IsNearlyZero(MaxSpriteSize) ? 1.0f : MaxSpriteSize);
		}

		if (bUsedWithRibbons)
		{
			float MaxRibbonWidth = kDefaultSize;
			if (RibbonWidthAccessor.IsValid())
			{
				MaxRibbonWidth = GetFloatMax(MakeArrayView(RibbonWidthAccessor.GetX(), NumInstances));
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
