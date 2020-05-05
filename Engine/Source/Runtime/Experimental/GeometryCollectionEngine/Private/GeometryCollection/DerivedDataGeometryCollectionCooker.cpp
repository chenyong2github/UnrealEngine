// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/DerivedDataGeometryCollectionCooker.h"
#include "Chaos/Core.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollection.h"
#include "Serialization/MemoryWriter.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "Chaos/ChaosArchive.h"
#include "UObject/DestructionObjectVersion.h"
#include "Chaos/ErrorReporter.h"

#if WITH_EDITOR

FDerivedDataGeometryCollectionCooker::FDerivedDataGeometryCollectionCooker(UGeometryCollection& InGeometryCollection)
	: GeometryCollection(InGeometryCollection)
{
}

FString FDerivedDataGeometryCollectionCooker::GetDebugContextString() const
{
	return GeometryCollection.GetFullName();
}

bool FDerivedDataGeometryCollectionCooker::Build(TArray<uint8>& OutData)
{
	FMemoryWriter Ar(OutData);
	Chaos::FChaosArchive ChaosAr(Ar);
	if (FGeometryCollection* Collection = GeometryCollection.GetGeometryCollection().Get())
	{
		FSharedSimulationParameters SharedParams;
		GeometryCollection.GetSharedSimulationParams(SharedParams);

		Chaos::FErrorReporter ErrorReporter(GeometryCollection.GetName());

		BuildSimulationData(ErrorReporter, *Collection, SharedParams);
		Collection->Serialize(ChaosAr);
		if (false && ErrorReporter.EncounteredAnyErrors())
		{
			bool bAllErrorsHandled = !ErrorReporter.ContainsUnhandledError();
			ErrorReporter.ReportError(*FString::Printf(TEXT("Could not cook content for Collection:%s"), *GeometryCollection.GetPathName()));
			if (bAllErrorsHandled)
			{
				ErrorReporter.HandleLatestError();
			}
			return false;	//Don't save into DDC if any errors found
		}

		return true;
	}

	return false;
}

const TCHAR* FDerivedDataGeometryCollectionCooker::GetVersionString() const
{
	if (OverrideVersion)
	{
		return OverrideVersion;	//force load old ddc if found. Not recommended
	}

	return TEXT("A8A2C0FB45084FCB922FEC1139E11341");
}

FString FDerivedDataGeometryCollectionCooker::GetPluginSpecificCacheKeySuffix() const
{

	return FString::Printf(TEXT("%s_%s_%s_%d"), *Chaos::ChaosVersionString, *GeometryCollection.GetIdGuid().ToString(), *GeometryCollection.GetStateGuid().ToString(), FDestructionObjectVersion::Type::LatestVersion);
}


#endif	//WITH_EDITOR
