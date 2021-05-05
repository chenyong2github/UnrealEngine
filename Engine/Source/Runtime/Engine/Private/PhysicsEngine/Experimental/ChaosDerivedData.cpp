// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosDerivedData.h"

#if WITH_CHAOS

#include "CoreMinimal.h"
#include "Chaos/Core.h"
#include "Chaos/CollisionConvexMesh.h"
#include "Chaos/ChaosArchive.h"
#include "ChaosDerivedDataUtil.h"
#include "Chaos/Particles.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/Convex.h"

#include "Serialization/Archive.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Serialization/MemoryWriter.h"
#include "ChaosCooking.h"

class FChaosDerivedDataCookerRefHolder : public FGCObject
{
public:
	FChaosDerivedDataCookerRefHolder(FChaosDerivedDataCooker* InCooker)
		: Cooker(InCooker)
	{
	}

	// FGCObject Interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		if (Cooker && Cooker->Setup)
		{
			Collector.AddReferencedObject(Cooker->Setup);
		}
	}
	virtual FString GetReferencerName() const override
	{
		return TEXT("ChaosDerivedDataCooker");
	}
	// End FGCObject Interface

private:
	FChaosDerivedDataCooker* Cooker;
};

const TCHAR* FChaosDerivedDataCooker::GetPluginName() const
{
	return TEXT("ChaosGeometryData");
}

const TCHAR* FChaosDerivedDataCooker::GetVersionString() const
{
	// As changing our DDC version will most likely affect any external callers that rely on Chaos types
	// for their own DDC or serialized data - change Chaos::ChaosVersionString in Chaos/Core.h to bump our
	// Chaos data version. Callers can also rely on that version in their builders and avoid bad serialization
	// when basic Chaos data changes
	return *Chaos::ChaosVersionString;
}

FString FChaosDerivedDataCooker::GetDebugContextString() const
{
	if (Setup)
	{
		UObject* Outer = Setup->GetOuter();
		if (Outer)
		{
			return Outer->GetFullName();
		}
	}

	return FDerivedDataPluginInterface::GetDebugContextString();
}

FString FChaosDerivedDataCooker::GetPluginSpecificCacheKeySuffix() const
{
	FString SetupGeometryKey(TEXT("INVALID"));

	if(Setup)
	{
		Setup->GetGeometryDDCKey(SetupGeometryKey);
	}

	return FString::Printf(TEXT("%s_%s_REAL%d"),
		*RequestedFormat.ToString(),
		*SetupGeometryKey,
		(int)sizeof(Chaos::FReal));
}

bool FChaosDerivedDataCooker::IsBuildThreadsafe() const
{
	// #BG Investigate Parallel Build
	return false;
}

bool FChaosDerivedDataCooker::Build(TArray<uint8>& OutData)
{
	bool bSucceeded = false;

	if(Setup)
	{
		FMemoryWriter MemWriterAr(OutData);
		Chaos::FChaosArchive Ar(MemWriterAr);

		int32 PrecisionSize = (int32)sizeof(BuildPrecision);

		Ar << PrecisionSize;

		Chaos::FCookHelper Cooker(Setup);
		Cooker.Cook();

		Ar << Cooker.SimpleImplicits << Cooker.ComplexImplicits << Cooker.UVInfo << Cooker.FaceRemap;

		bSucceeded = true;
	}

	return bSucceeded;
}

FChaosDerivedDataCooker::FChaosDerivedDataCooker(UBodySetup* InSetup, FName InFormat, bool bInUseRefHolder)
	: Setup(InSetup)
	, RequestedFormat(InFormat)
{
	if (bInUseRefHolder)
	{
		RefHolder = MakeUnique<FChaosDerivedDataCookerRefHolder>(this);
	}
}


#endif
 
