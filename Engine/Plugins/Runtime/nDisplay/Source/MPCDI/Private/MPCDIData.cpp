// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MPCDIData.h"
#include "MPCDIRegion.h"
#include "MPCDILog.h"

#include "Stats/Stats.h"
#include "Engine/Engine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

#include "MPCDIHelpers.h"

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


bool FMPCDIData::FMPCDIBuffer::Initialize(const FString& BufferName)
{
	ID = BufferName;
	AABBox = FBox(FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX));
	return true;
}
void FMPCDIData::FMPCDIBuffer::AddRegion(MPCDI::FMPCDIRegion* MPCDIRegionPtr)
{
	if (MPCDIRegionPtr)
	{
		MPCDIRegionPtr->WarpMap.AppendAABB(AABBox);
		Regions.Add(MPCDIRegionPtr);
	}
}


FMPCDIData::FMPCDIData()
	: Profile(IMPCDI::EMPCDIProfileType::Invalid)	
	, bForceExtFilesReload(false)
{
}

FMPCDIData::~FMPCDIData()
{
	CleanupMPCDIData();
}

void FMPCDIData::ReloadAll()
{
	FScopeLock lock(&ExtFilesReloadCS);
	bForceExtFilesReload = true;
}

void FMPCDIData::ReloadChangedExternalFiles_RenderThread()
{
	FScopeLock lock(&ExtFilesReloadCS);

	// Reload Ext pfm+png files
	for (auto& It : Buffers)
	{
		for (auto& Region : It->Regions)
		{
			Region->ReloadExternalFiles_RenderThread(bForceExtFilesReload);
		}
	}

	bForceExtFilesReload = false;
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

bool FMPCDIData::LoadFromFile(const FString& MPCIDIFile)
{
	bool success = Load(MPCIDIFile);
	return success;
}

bool FMPCDIData::Initialize(const FString& MPCIDIFile, IMPCDI::EMPCDIProfileType MPCDIProfileType)
{
	LocalMPCIDIFile = MPCIDIFile;
	Profile = MPCDIProfileType;

	TextureMatrix = FMatrix::Identity;
	if (Profile == IMPCDI::EMPCDIProfileType::mpcdi_2D || Profile == IMPCDI::EMPCDIProfileType::mpcdi_3D || Profile == IMPCDI::EMPCDIProfileType::mpcdi_SL)
	{
		// Fetching from a 2D source
		TextureMatrix.M[0][0] = 1.f;
		TextureMatrix.M[1][1] = -1.f;
		TextureMatrix.M[3][0] = 0.f;
		TextureMatrix.M[3][1] = 1.f;
	}
	else if (Profile == IMPCDI::EMPCDIProfileType::mpcdi_A3D)
	{
		// Fetching from a 3D source
		TextureMatrix.M[0][0] = 0.5f;
		TextureMatrix.M[1][1] = -0.5f;
		TextureMatrix.M[3][0] = 0.5f;
		TextureMatrix.M[3][1] = 0.5f;
	}

	return MPCDIProfileType != IMPCDI::EMPCDIProfileType::Invalid;
}

bool FMPCDIData::AddRegion(const FString& BufferName, const FString& RegionName, IMPCDI::FRegionLocator& OutRegionLocator)
{
	if (!FindRegion(BufferName, RegionName, OutRegionLocator))
	{
		if (OutRegionLocator.BufferIndex < 0)
		{
			//Create new buffer
			FMPCDIBuffer* buf = new FMPCDIData::FMPCDIBuffer();
			buf->Initialize(BufferName);
			Buffers.Add(buf);
			FindRegion(BufferName, RegionName, OutRegionLocator);
		}

		if (OutRegionLocator.RegionIndex < 0)
		{
			if (OutRegionLocator.BufferIndex < Buffers.Num())
			{
				FMPCDIBuffer* Buffer = Buffers[OutRegionLocator.BufferIndex];
				if (Buffer)
				{
					MPCDI::FMPCDIRegion* NewRegionPtr = new MPCDI::FMPCDIRegion(*RegionName, 1920, 1080);
					Buffer->AddRegion(NewRegionPtr);
					return FindRegion(BufferName, RegionName, OutRegionLocator);
				}
			}
		}
	}

	//Region already exist
	return true;
}

bool FMPCDIData::Load(const FString& MPCIDIFile)
{

	FString MPCIDIFileFullPath = DisplayClusterHelpers::config::GetFullPath(MPCIDIFile);
	if (!FPaths::FileExists(MPCIDIFileFullPath))
	{
		//! Handle error: mpcdi file not found
		return false;
	}

	bool success = false;

	mpcdi::Profile *profile = mpcdi::Profile::CreateProfile();
	mpcdi::Reader *reader = mpcdi::Reader::CreateReader();
	std::string version = reader->GetSupportedVersions();

	mpcdi::MPCDI_Error mpcdi_err = reader->Read(TCHAR_TO_ANSI(*MPCIDIFileFullPath), profile);
	delete reader;

	UE_LOG(LogMPCDI, Log, TEXT("Loading MPCDI file %s."), *MPCIDIFileFullPath);

	if (MPCDI_FAILED(mpcdi_err))
	{
		UE_LOG(LogMPCDI, Error, TEXT("Error %d reading MPCDI file"), int32(mpcdi_err));
		success = false;
	}
	else
	{
		// Compute profile
		IMPCDI::EMPCDIProfileType MPCDIProfileType = IMPCDI::EMPCDIProfileType::Invalid;
		switch (profile->GetProfileType())
		{
		case mpcdi::ProfileType2d:
			MPCDIProfileType = IMPCDI::EMPCDIProfileType::mpcdi_2D;
			break;
		case mpcdi::ProfileType3d:
			MPCDIProfileType = IMPCDI::EMPCDIProfileType::mpcdi_3D;
			break;
		case mpcdi::ProfileTypea3:
			MPCDIProfileType = IMPCDI::EMPCDIProfileType::mpcdi_A3D;
			break;
		case mpcdi::ProfileTypesl:
			MPCDIProfileType = IMPCDI::EMPCDIProfileType::mpcdi_SL;
			break;
		};

		if (!Initialize(MPCIDIFile, MPCDIProfileType))
		{
			return false;
		}

		// Store version
		Version = version.c_str();
		UE_LOG(LogMPCDI, Verbose, TEXT("Version: %s"), *Version);

		

		// Fill buffer information		
		for (mpcdi::Display::BufferIterator itBuffer = profile->GetDisplay()->GetBufferBegin(); itBuffer != profile->GetDisplay()->GetBufferEnd(); ++itBuffer)
		{
			Buffers.Add(new FMPCDIBuffer());
			FMPCDIBuffer &buffer = *(Buffers.Last());
			mpcdi::Buffer *mpcdiBuffer = itBuffer->second;
			buffer.Initialize(FString(mpcdiBuffer->GetId().c_str()));

			// Fill-in region information
			for (mpcdi::Buffer::RegionIterator it = mpcdiBuffer->GetRegionBegin(); it != mpcdiBuffer->GetRegionEnd(); ++it)
			{
				mpcdi::Region *mpcdiRegion = it->second;
				if (mpcdiRegion)
				{
					MPCDI::FMPCDIRegion* NewRegionPtr = new MPCDI::FMPCDIRegion();
					if (NewRegionPtr->Load(mpcdiRegion, GetProfileType()))
					{
						buffer.AddRegion(NewRegionPtr);
					}
					else
					{
						//@todo handle error
						UE_LOG(LogMPCDI, Error, TEXT("Can't load mpcdi region %s"), ANSI_TO_TCHAR(mpcdiRegion->GetId().c_str()));
					}					
				}
			}// end region loop
		}//end buffer loop
		success = true;
	}

	delete profile;
	return success;
}

void FMPCDIData::AddReferencedObjects(FReferenceCollector& Collector)
{
}

#if 0
//@todo Unsupported now
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
#endif

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
		case IMPCDI::EMPCDIProfileType::mpcdi_A3D:
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
