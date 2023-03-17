// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/ManagedArrayAccessor.h"


namespace GeometryCollection::Facades
{
	/**
	*
	*/
	class CHAOS_API FTetrahedralMetrics
	{
	public:
		// Groups

		// Attributes
		static const FName SignedVolumeAttributeName;
		static const FName AspectRatioAttributeName;

		FTetrahedralMetrics(FManagedArrayCollection& InCollection);
		FTetrahedralMetrics(const FManagedArrayCollection& InCollection);
		virtual ~FTetrahedralMetrics();

		void DefineSchema();
		bool IsConst() const { return SignedVolumeAttribute.IsConst(); }
		bool IsValid() const;

		const TManagedArrayAccessor<float>& GetSignedVolumeRO() const { return SignedVolumeAttribute; }
		TManagedArrayAccessor<float>& GetSignedVolume() { check(!IsConst()); return SignedVolumeAttribute; }

		const TManagedArrayAccessor<float>& GetAspectRatioRO() const { return AspectRatioAttribute; }
		TManagedArrayAccessor<float>& GetAspectRatio() { check(!IsConst()); return AspectRatioAttribute; }

	private:
		TManagedArrayAccessor<float> SignedVolumeAttribute;
		TManagedArrayAccessor<float> AspectRatioAttribute;
	};

} // namespace GeometryCollection::Facades