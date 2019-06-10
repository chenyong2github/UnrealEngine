// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MPCDIData.h"
#include "MPCDIRegion.h"
#include "MPCDILog.h"

#include "Stats/Stats.h"
#include "Engine/Engine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

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

FMPCDIData::FMPCDIData()
	: Profile(EMPCDIProfileType::Invalid)
	, UseColorAdjustment(false)
{
}

FMPCDIData::~FMPCDIData()
{
	CleanupMPCDIData();
}

void FMPCDIData::CleanupMPCDIData()
{
	for (auto& ItBuffer : Buffers)
	{
		for (auto& ItRegion : ItBuffer->Regions)
		{
			BeginReleaseResource(&ItRegion->WarpMap);
			BeginReleaseResource(&ItRegion->AlphaMap);
			BeginReleaseResource(&ItRegion->BetaMap);
		}
	}

	FlushRenderingCommands();

	for (auto& ItBuffer : Buffers)
	{
		delete ItBuffer;
	}
}

bool FMPCDIData::Load(const FString& MPCIDIFile, bool useCubeMap /* = false */, int cubeMapResolution/* = 2048 */, bool ColorAdjustment /* = false */)
{
	FilePath = MPCIDIFile;

	//CubeMapResolution = cubeMapResolution;
	UseColorAdjustment = ColorAdjustment;

	bool success = Load();
	//UseCubeMap = useCubeMap && Profile == EMPCDIProfileType::mpcdi_A3D;

	return success;
}


bool FMPCDIData::Load()
{
	bool success = false;

	mpcdi::Profile *profile = mpcdi::Profile::CreateProfile();
	mpcdi::Reader *reader = mpcdi::Reader::CreateReader();
	std::string version = reader->GetSupportedVersions();

	mpcdi::MPCDI_Error mpcdi_err = reader->Read(TCHAR_TO_ANSI(*FilePath), profile);
	delete reader;

	UE_LOG(LogMPCDI, Log, TEXT("Loading MPCDI file %s."), *FilePath);

	if (MPCDI_FAILED(mpcdi_err))
	{
		UE_LOG(LogMPCDI, Error, TEXT("Error %d reading MPCDI file"), int32(mpcdi_err));
		success = false;
	}
	else
	{
		// Compute profile
		switch (profile->GetProfileType())
		{
		case mpcdi::ProfileType2d:
			Profile = EMPCDIProfileType::mpcdi_2D;
			break;
		case mpcdi::ProfileType3d:
			Profile = EMPCDIProfileType::mpcdi_3D;
			break;
		case mpcdi::ProfileTypea3:
			Profile = EMPCDIProfileType::mpcdi_A3D;
			break;
		case mpcdi::ProfileTypesl:
			Profile = EMPCDIProfileType::mpcdi_SL;
			break;
		};

		// Store version
		Version = version.c_str();
		UE_LOG(LogMPCDI, Verbose, TEXT("Version: %s"), *Version);

		TextureMatrix = FMatrix::Identity;
		if (Profile == EMPCDIProfileType::mpcdi_2D || Profile == EMPCDIProfileType::mpcdi_3D || Profile == EMPCDIProfileType::mpcdi_SL)
		{
			// Fetching from a 2D source
			TextureMatrix.M[0][0] = 1.f;
			TextureMatrix.M[1][1] = -1.f;
			TextureMatrix.M[3][0] = 0.f;
			TextureMatrix.M[3][1] = 1.f;
		}
		else if (Profile == EMPCDIProfileType::mpcdi_A3D)
		{
			// Fetching from a 3D source
			TextureMatrix.M[0][0] = 0.5f;
			TextureMatrix.M[1][1] = -0.5f;
			TextureMatrix.M[3][0] = 0.5f;
			TextureMatrix.M[3][1] = 0.5f;
		}

		// Fill buffer information
		int bufferIndex = 0;
		for (mpcdi::Display::BufferIterator itBuffer = profile->GetDisplay()->GetBufferBegin(); itBuffer != profile->GetDisplay()->GetBufferEnd(); ++itBuffer)
		{
			Buffers.Add(new FMPCDIBuffer());
			FMPCDIBuffer &buffer = *(Buffers.Last());

			mpcdi::Buffer *mpcdiBuffer = itBuffer->second;

			buffer.ID = FString(mpcdiBuffer->GetId().c_str());
			buffer.AABBox = FBox(FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX));

			UE_LOG(LogMPCDI, Verbose, TEXT("Buffer %d - %s"), bufferIndex, ANSI_TO_TCHAR(mpcdiBuffer->GetId().c_str()));

			// Fill-in region information
			int regionIndex = 0;

			//RegionAllocator::Reset();
			for (mpcdi::Buffer::RegionIterator it = mpcdiBuffer->GetRegionBegin(); it != mpcdiBuffer->GetRegionEnd(); ++it)
			{
				mpcdi::Region *mpcdiRegion = it->second;
				if (mpcdiRegion)
				{
					MPCDI::FMPCDIRegion* NewRegionPtr = new MPCDI::FMPCDIRegion();
					NewRegionPtr->Load(mpcdiRegion, GetProfileType());
					NewRegionPtr->WarpMap.AppendAABB(buffer.AABBox);
					buffer.Regions.Add(NewRegionPtr);
				}
				++regionIndex;
			}// end region loop

			++bufferIndex;

		}//end buffer loop

		success = true;
	}

	delete profile;

	return success;
}

void FMPCDIData::AddReferencedObjects(FReferenceCollector& Collector)
{

}

bool FMPCDIData::IsCubeMapEnabled() const
{
	return false;
}

/*
bool FMPCDIData::ComputeFrustum_SL(const IMPCDI::FRegionLocator& RegionLocator, IMPCDI::FFrustum &OutFrustum, float WorldScale, float ZNear, float ZFar) const
{
	const FMPCDIData::FMPCDIBuffer *MPCDIBuffer = Buffers[RegionLocator.BufferIndex];

	OutFrustum.WorldScale = WorldScale;

	FMatrix local2world = MPCDIBuffer->Camera2World;
	FMatrix world2local = local2world.Inverse();

	// Those matrices were copied from LocalPlayer.cpp.
	// They change the coordinate system from the Unreal "Game" coordinate system to the Unreal "Render" coordinate system
	static const FMatrix Game2Render(FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));

	static const FMatrix Render2Game = Game2Render.Inverse();

	OutFrustum.Origin = MPCDIBuffer->Origin;

	// Compute view matrix
	OutFrustum.ViewMatrix = Game2Render;

	FMatrix OriginViewMatrix = Render2Game * world2local*Game2Render;
	//OutFrustum.ProjectionMatrix = originViewMatrix * MPCDIBuffer->ProjectionMatrix;
	OutFrustum.OriginViewMatrix = OriginViewMatrix;
	OutFrustum.ProjectionMatrix = MPCDIBuffer->ProjectionMatrix;

	OutFrustum.UVMatrix = FMatrix::Identity;
	return true;
}
*/

bool FMPCDIData::ComputeFrustum(const IMPCDI::FRegionLocator& RegionLocator, IMPCDI::FFrustum& Frustum, float WorldScale, float ZNear, float ZFar) const
{
	if (RegionLocator.BufferIndex >= Buffers.Num())
	{
		//@todo: handle error
		return false;
	}

	const FMPCDIData::FMPCDIBuffer *MPCDIBuffer = Buffers[RegionLocator.BufferIndex];
	if (MPCDIBuffer)
	{
		switch (GetProfileType())
		{
		case EMPCDIProfileType::mpcdi_A3D:
			return GetRegion(RegionLocator)->WarpMap.GetFrustum_A3D(Frustum, WorldScale, ZNear, ZFar);
			break;
#if 0
		//@todo Unsupported now
		case EMPCDIProfileType::mpcdi_SL:
		{
			return ComputeFrustum_SL(RegionLocator, Frustum, WorldScale, ZNear, ZFar);
			break;
		}
		break;
#endif
		default:
			//@todo logs not supported yet
			break;
		}
	}

	return false;
}
