// Copyright Epic Games, Inc. All Rights Reserved.
#include "GeometryCollection/DerivedDataGeometryCollectionCooker.h"
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
	//touch guid when new data needs to be serialized. Please leave comment saying what you did
	//TEXT("4");	//original versions before using guids.
	//return TEXT("8724C70A140146B5A2F4CF0C16083041");	//Switching to GUIDs so that people don't poison DDC when testing things locally
	//return TEXT("C422BF0A539C455A90C6A8660BA0170F");	//Previous version was only saving to DDC. Changing GUID so we know which entries are potentially missing from assets
	//return TEXT("951075CFF57C40D88E15756393EECE70");	//Simplicials are now stored in the DDC
	//return TEXT("5AA67AB5FE4040629FD47946FE3CA3FD");	//Implicits are now stored in the DDC
	//return TEXT("421E954DED254B95B20125FBAB507746");	//Mass and inertia are clamped together to ensure both are reasonable. At least one simplicial sample point always exists
	//return TEXT("969470E420CD4F8E83356B8BC4007799");	//Properly report error when non-contiguous objects are stored in a geometry collection
	//return TEXT("35629AF302984E83935233220788CDCC");	//Added size specific data for geometry collection
	//return TEXT("C84C3A40E80B42FC865922587047255D");	//Fix non-coincident triangle verts being incorrectly sorted after coincident vertices
	//return TEXT("9EC74FE6494D443DA12189BADF953A33");	//Fix cluster inertia tensor
	//return TEXT("5A59EE91C27641DF8A083998EA32E560");	//Remove mesh normals when computing levelset
	//return TEXT("08C81448E92D4B5EB9444C0E854A5BE4");	//Cull out slightly bigger triangles early in the pipeline. This is needed because of later transforms which add error
	//return TEXT("E1615D65A2D645F3B9D119CC78D9240F");	//change default level set resolution from 5-10 to 10-10 to get better rasterization results
	//return TEXT("C89DD39311BC49898736B7A49A1F51AF");	//Inertia and mass must have a minimum box of 10cm^3
	//return TEXT("235FA4CD6A794305A4638D559DD1D359");	//Levelset normal updated to use surface normals
	//return TEXT("43D27BBFDF96434BB116498A8375003E");	//Levelset error metrics
	//return TEXT("142F76A9F1524A9F935B28C7FB74690E");	//Build simplicials
	//return TEXT("132EC70C044646E48A725A67114DAE06");	//CleanCollisionParticles upgrade
	//return TEXT("5EFA18C9C87E4350A34FD8F8100DDEA2");	//Bulk serialize changes
	//return TEXT("1FD9874624304A0EB6C635DC2B4BE237");  //Add hole filling as pre-process before level set generation
	//return TEXT("0182FCF14245402B8A902298417ECAA3");	//Increase resolution for cluster parents
	//return TEXT("2F4F3A038A7B41F28CA86799DD8D4300");	//No tiny faces or non-manifold edges in the hole fills + fix materials
	//return TEXT("B5F1831D60884F9DAC2C2CBCF2ECFA12");	//simplicials
	//return TEXT("851938C6E7EA44C4B2DA0C8E600693A4");	//simplicials and inertia
	return TEXT("E237FFBE23A54668A07C3D14AB68A7B4");	//box and sphere

}

FString FDerivedDataGeometryCollectionCooker::GetPluginSpecificCacheKeySuffix() const
{

	return FString::Printf(TEXT("%s_%s_%d"), *GeometryCollection.GetIdGuid().ToString(), *GeometryCollection.GetStateGuid().ToString(), FDestructionObjectVersion::Type::LatestVersion);
}


#endif	//WITH_EDITOR
