// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/CADKernelArchive.h"
#include "CADKernel/Core/Entity.h"

#include "CADKernel/Math/MatrixH.h"

class FString;

namespace CADKernel
{
	class CADKERNEL_API FEntityGeom : public FEntity
	{
	friend class FCoreTechBridge;

	protected:
#ifdef CORETECHBRIDGE_DEBUG
		FIdent CtKioId = 0;
#endif
	public:
		FEntityGeom()
			: FEntity()
		{
		}


		virtual TSharedPtr<FEntityGeom> ApplyMatrix(const FMatrixH& InMatrix) const
		{
			return TSharedPtr<FEntityGeom>();
		}

		virtual void Display(const FString& Name) const
		{
		}

		virtual void Serialize(FCADKernelArchive& Ar) override
		{
			FEntity::Serialize(Ar);
#ifndef CORETECHBRIDGE_DEBUG
			FIdent CtKioId = 0;
#endif
			Ar << CtKioId;
		}

#ifdef CORETECHBRIDGE_DEBUG
		FIdent GetKioId()
		{
			return CtKioId;
		}
#endif

#ifdef CADKERNEL_DEV
		virtual FInfoEntity& GetInfo(FInfoEntity& Info) const override;
#endif
	};
}

