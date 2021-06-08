// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/EntityGeom.h"

namespace CADKernel
{
	class FModelMesh;
	class FTopologicalFace;

	class CADKERNEL_API FTopologicalEntity : public FEntityGeom
	{
	public:

		virtual int32 FaceCount() const
		{
			return 0;
		}

		virtual void GetFaces(TArray<TSharedPtr<FTopologicalFace>>& OutFaces) {}

		/**
		 * Each face of model is set by its orientation. This allow to make oriented mesh and to keep the face orientation in topological function.
		 * Marker2 of spread face is set. It must be reset after the process
		 */
		virtual void SpreadBodyOrientation() {}

		virtual void Serialize(FCADKernelArchive& Ar) override
		{
			FEntityGeom::Serialize(Ar);
		}
		
#ifdef CADKERNEL_DEV
		virtual FInfoEntity& GetInfo(FInfoEntity&) const override;
#endif

		const bool IsApplyCriteria() const
		{
			return ((States & EHaveStates::IsApplyCriteria) == EHaveStates::IsApplyCriteria);
		}

		virtual void SetApplyCriteria() const
		{
			States |= EHaveStates::IsApplyCriteria;
		}
		
		virtual void ResetApplyCriteria()
		{
			States &= ~EHaveStates::IsApplyCriteria;
		}

		bool IsMeshed() const
		{
			return ((States & EHaveStates::IsMeshed) == EHaveStates::IsMeshed);
		}
		
		virtual void SetMeshed()
		{
			States |= EHaveStates::IsMeshed;
		}
		
		virtual void ResetMeshed()
		{
			States &= ~EHaveStates::IsMeshed;
		}
	};

} // namespace CADKernel

