// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/GeometryParticles.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/ImplicitObjectUnion.h"

namespace Chaos
{
	template <typename T, int d>
	void UpdateShapesArrayFromGeometry(TShapesArray<T, d>& ShapesArray, const TImplicitObject<T, d>* Geometry)
	{
		if(Geometry)
		{
			if(const auto* Union = Geometry->template GetObject<TImplicitObjectUnion<T, d>>())
			{
				ShapesArray.SetNum(Union->GetObjects().Num());
				int32 Inner = 0;
				for(const auto& Geom : Union->GetObjects())
				{
					ShapesArray[Inner] = MakeUnique<TPerShapeData<T, d>>();
					ShapesArray[Inner]->Geometry = Geom.Get();
					++Inner;
				}
			}
			else
			{
				ShapesArray.SetNum(1);
				ShapesArray[0] = MakeUnique<TPerShapeData<T, d>>();
				ShapesArray[0]->Geometry = Geometry;
			}
		}
		else
		{
			ShapesArray.Reset();
		}
	}

	template void UpdateShapesArrayFromGeometry(TShapesArray<float, 3>& ShapesArray, const TImplicitObject<float, 3>* Geometry);
}
