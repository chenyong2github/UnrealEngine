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
		int32 PatchId = 0;
 		int32 LayerId = -1; 
		uint32 ColorId = 0;
		uint32 MaterialId = 0;

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

		uint32 GetColorId()
		{
			return ColorId;
		}

		void SetMaterialId(const uint32& InMaterialId)
		{
			MaterialId = InMaterialId;
		}

		uint32 GetMaterialId()
		{
			return MaterialId;
		}

		void SetPatchId(int32 InPatchId)
		{
			PatchId = InPatchId;
		}

		int32 GetPatchId()
		{
			return PatchId;
		}
	};
}


