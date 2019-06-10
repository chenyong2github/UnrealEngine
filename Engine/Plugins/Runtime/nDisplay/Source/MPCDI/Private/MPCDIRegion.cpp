// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MPCDIRegion.h"
#include "MPCDIBlendTexture.h"

THIRD_PARTY_INCLUDES_START

#include "mpcdiProfile.h"
#include "mpcdiReader.h"
#include "mpcdiDisplay.h"
#include "mpcdiBuffer.h"
#include "mpcdiRegion.h"
#include "mpcdiAlphaMap.h"
#include "mpcdiBetaMap.h"
#include "mpcdiDistortionMap.h"
#include "mpcdiGeometryWarpFile.h"

THIRD_PARTY_INCLUDES_END

namespace MPCDI
{
	void FMPCDIRegion::Load(mpcdi::Region* mpcdiRegion, const EMPCDIProfileType ProfileType)
	{
		ID = FString(mpcdiRegion->GetId().c_str());

		X = mpcdiRegion->GetX();
		Y = mpcdiRegion->GetY();
		W = mpcdiRegion->GetXsize();
		H = mpcdiRegion->GetYsize();
		ResX = mpcdiRegion->GetXresolution();
		ResY = mpcdiRegion->GetYresolution();

		mpcdi::Frustum* mpcdi_frustum = mpcdiRegion->GetFrustum();


		mpcdi::AlphaMap *alphaMap = mpcdiRegion->GetFileSet()->GetAlphaMap();
		if (alphaMap)
		{
			float AlphaEmbeddedGamma = alphaMap->GetGammaEmbedded();
			AlphaMap.LoadBlendMap(alphaMap, AlphaEmbeddedGamma);
		}

		mpcdi::BetaMap *betaMap = mpcdiRegion->GetFileSet()->GetBetaMap();
		if (betaMap)
		{
			BetaMap.LoadBlendMap(betaMap,1);
		}

		if (ProfileType != EMPCDIProfileType::mpcdi_SL)
		{
			mpcdi::GeometryWarpFile *geometryFile = mpcdiRegion->GetFileSet()->GetGeometryWarpFile();
			if (geometryFile)
			{
				WarpMap.LoadWarpMap(geometryFile, ProfileType);
			}
		}
	}
}
