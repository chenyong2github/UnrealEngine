// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/Types.h"
#include "CADKernel/Math/MathConst.h"

namespace CADKernel
{
	class CADKERNEL_API FMetadataDictionary
	{

	private:

		FString Name;
		int32 HostId = 0;
		uint32 ColorId = 0;
		uint32 MaterialId = 0;
		int32 LayerId = -1; 

	public:

		FMetadataDictionary() = default;

		void SetHostId(const int32 InHostId)
		{
			HostId = InHostId;
		}

		void SetLayer(const int32 InLayerId)
		{
			LayerId = InLayerId;
		}

		void SetName(const FString& InName)
		{
			Name = InName;
		}

		void SetColorId(const uint32& InColorId)
		{
			ColorId = InColorId;
		}

		void SetMaterialId(const uint32& InMaterialId)
		{
			MaterialId = InMaterialId;
		}
	};
}


