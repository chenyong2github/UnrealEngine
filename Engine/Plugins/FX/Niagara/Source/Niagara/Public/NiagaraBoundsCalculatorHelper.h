// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "NiagaraBoundsCalculator.h"
#include "NiagaraDataSet.h"

template<bool bUsedWithSprites, bool bUsedWithMeshes, bool bUsedWithRibbons>
class FNiagaraBoundsCalculatorHelper : public FNiagaraBoundsCalculator
{
public:
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

	virtual FBox CalculateBounds(const int32 NumInstances, bool& bOutContainsNaN) override final
	{
		PositionAccessor.InitForAccess();
		if (PositionAccessor.IsValid() == false)
			return FBox(ForceInit);

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

		constexpr float kDefaultSize = 50.0f;
		FVector MaxScale(ScaleAccessor.IsValid() ? FVector::ZeroVector : FVector(kDefaultSize, kDefaultSize, kDefaultSize));
		float MaxSpriteSize(SpriteSizeAccessor.IsValid() ? 0.0f : kDefaultSize);
		float MaxRibbonWidth(RibbonWidthAccessor.IsValid() ? 0.0f : kDefaultSize);

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
		bOutContainsNaN = false;
#endif

		FBox Ret;
		Ret.Init();

		for (int32 InstIdx = 0; InstIdx < NumInstances && PositionAccessor.IsValid(); ++InstIdx)
		{
			FVector Position;
			PositionAccessor.Get(InstIdx, Position);

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
			// Some graphs have a tendency to divide by zero. This ContainsNaN has been added prophylactically
			// to keep us safe during GDC. It should be removed as soon as we feel safe that scripts are appropriately warned.
			if (Position.ContainsNaN())
			{
				bOutContainsNaN = true;
				continue;
			}
#endif

			Ret += Position;

			if (bUsedWithMeshes && ScaleAccessor.IsValid())
			{
				MaxScale = MaxScale.ComponentMax(ScaleAccessor.Get(InstIdx));
			}

			if (bUsedWithSprites && SpriteSizeAccessor.IsValid())
			{
				const FVector2D InstanceSpriteSize = SpriteSizeAccessor.Get(InstIdx);
				MaxSpriteSize = FMath::Max3(MaxSpriteSize, InstanceSpriteSize.X, InstanceSpriteSize.Y);
			}

			if (bUsedWithRibbons && RibbonWidthAccessor.IsValid())
			{
				MaxRibbonWidth = FMath::Max(MaxRibbonWidth, RibbonWidthAccessor.Get(InstIdx));
			}
		}

		float MaxSize = 0.0001f;
		if (bUsedWithSprites)
		{
			const float SpriteSize = FMath::IsNearlyZero(MaxSpriteSize) ? 1.0f : MaxSpriteSize * 0.5f;
			MaxSize = FMath::Max(MaxSize, SpriteSize);
		}

		if (bUsedWithMeshes)
		{
			const FVector MeshSize = (MaxScale.IsNearlyZero() ? FVector::OneVector : MaxScale) * MeshExtents;
			MaxSize = FMath::Max(MaxSize, MeshSize.GetMax());
		}

		if (bUsedWithRibbons)
		{
			const float RibbonSize = FMath::IsNearlyZero(MaxRibbonWidth) ? 1.0f : MaxRibbonWidth * 0.5f;
			MaxSize = FMath::Max(MaxSize, RibbonSize);
		}

		Ret = Ret.ExpandBy(MaxSize);

		return Ret;
	}

	FNiagaraDataSetAccessor<FVector> PositionAccessor;
	FNiagaraDataSetAccessor<FVector2D> SpriteSizeAccessor;
	FNiagaraDataSetAccessor<FVector> ScaleAccessor;
	FNiagaraDataSetAccessor<float> RibbonWidthAccessor;
	FVector MeshExtents = FVector::OneVector;
};
