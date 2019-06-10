// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MPCDITypes.h"
#include "MPCDIBlendTexture.h"
#include "MPCDIWarpTexture.h"

#include "RenderResource.h"


namespace mpcdi
{
	struct Region;
}

namespace MPCDI
{
	class FDebugMeshExporter;

	struct FMPCDIRegion
	{
		FString ID;

		float X;
		float Y;
		float W;
		float H;
		
		uint32_t ResX;
		uint32_t ResY;

		// Position/Scale of the viewport in the output framebuffer - This is temp until we implement the divorce
		// Between input and output framebuffers
		//float OutX, OutY;
		//float OutW, OutH;

		FMPCDIBlendTexture AlphaMap;
		FMPCDIBlendTexture BetaMap;
		FMPCDIWarpTexture  WarpMap;

		bool isRuntimeData : 1;

		FMPCDIRegion()
			: isRuntimeData(false)
		{ }

		~FMPCDIRegion()
		{ }

		FMPCDIRegion(const wchar_t* Name, int InW, int InH)
			: ID(Name)
			, X(0), Y(0)
			, W(1), H(1)
			, ResX(InW), ResY(InH)
			, isRuntimeData(true)
		{ }

		void Load(mpcdi::Region* InMPCIDRegionData, const EMPCDIProfileType ProfileType);

		inline FMatrix GetRegionMatrix() const
		{
			FMatrix RegionMatrix = FMatrix::Identity;
			RegionMatrix.M[0][0] = W;
			RegionMatrix.M[1][1] = H;
			RegionMatrix.M[3][0] = X;
			RegionMatrix.M[3][1] = Y;
			return RegionMatrix;
		}
	};
}
