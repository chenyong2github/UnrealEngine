// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeoReferencingSystem.h"

#include "DrawDebugHelpers.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "proj.h"

#if WITH_EDITOR
#include "LevelEditor.h"
#endif

#define ECEF_EPSG_FSTRING FString(TEXT("EPSG:4978"))

DECLARE_LOG_CATEGORY_EXTERN(LogGeoReferencing, Log, All);
DEFINE_LOG_CATEGORY(LogGeoReferencing);

AGeoReferencingSystem* AGeoReferencingSystem::GetGeoReferencingSystem(UObject* WorldContextObject)
{
	AGeoReferencingSystem* Actor = nullptr;

	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		TArray<AActor*> Actors;
		UGameplayStatics::GetAllActorsOfClass(World, AGeoReferencingSystem::StaticClass(), Actors);
		int NbActors = Actors.Num();
		if (NbActors == 0)
		{
			UE_LOG(LogGeoReferencing, Error, TEXT("GeoReferencingSystem actor not found. Please add one to your world to configure your geo referencing system."));
		}
		else if (NbActors > 1)
		{
			UE_LOG(LogGeoReferencing, Error, TEXT("Multiple GeoReferencingSystem actors found. Only one actor should be used to configure your geo referencing system"));
		}
		else
		{
			Actor = Cast<AGeoReferencingSystem>(Actors[0]);
		}
	}

	return Actor;
}

class AGeoReferencingSystem::FGeoReferencingSystemInternals
{
public:
	FGeoReferencingSystemInternals()
		: ProjContext(nullptr)
		, ProjProjectedToGeographic(nullptr)
		, ProjProjectedToECEF(nullptr)
		, ProjGeographicToECEF(nullptr)
	{
	}

	// Private PROJ Utilities
	void InitPROJLibrary();
	void DeInitPROJLibrary();
	PJ* GetPROJProjection(FString SourceCRS, FString DestinationCRS);
	bool GetEllipsoid(FString CRSString, FEllipsoid& Ellipsoid);
	FMatrix4d GetWorldFrameToECEFFrame(const FEllipsoid& Ellipsoid, const FCartesianCoordinates& ECEFLocation);

	static FMatrix ConvertMatrix4d(FMatrix4d Matrix);

	PJ_CONTEXT* ProjContext;
	PJ* ProjProjectedToGeographic;
	PJ* ProjProjectedToECEF;
	PJ* ProjGeographicToECEF;
	FEllipsoid ProjectedEllipsoid;
	FEllipsoid GeographicEllipsoid;

	// Transformation caches 
	// Flat Planet
	FVector3d WorldOriginLocationProjected; // Offset between the UE world and the Projected CRS Origin. (Expressed in ProjectedCRS units).

	// Round Planet
	FMatrix4d WorldFrameToECEFFrame; // Matrix to transform a vector from EU to ECEF CRS
	FMatrix4d ECEFFrameToWorldFrame; // Matrix to transform a vector from ECEF to UE CRS - Inverse of the previous one kept in cache. 

	FMatrix4d WorldFrameToUEFrame;
	FMatrix4d UEFrameToWorldFrame;
};

/////// INIT / DEINIT

void AGeoReferencingSystem::PostLoad()
{
	Super::PostLoad();

	Initialize();
}

void AGeoReferencingSystem::PostActorCreated()
{
	Super::PostActorCreated();

	Initialize();
}

void AGeoReferencingSystem::Initialize()
{
	Impl = MakePimpl<FGeoReferencingSystemInternals>();

	Impl->InitPROJLibrary();

	Impl->WorldFrameToUEFrame = FMatrix4d(
		FVector4d(1.0, 0.0, 0.0, 0.0),
		FVector4d(0.0, -1.0, 0.0, 0.0),
		FVector4d(0.0, 0.0, 1.0, 0.0),
		FVector4d(0.0, 0.0, 0.0, 1.0), true);
	Impl->UEFrameToWorldFrame = Impl->WorldFrameToUEFrame.Inverse();

	ApplySettings();
}

void AGeoReferencingSystem::BeginDestroy()
{
	Super::BeginDestroy();

	if (Impl)
	{
		Impl->DeInitPROJLibrary();
	}
}

void AGeoReferencingSystem::EngineToProjected(const FVector& EngineCoordinates, FCartesianCoordinates& ProjectedCoordinates)
{
	switch (PlanetShape)
	{
	case EPlanetShape::RoundPlanet:
	{
		// In RoundPlanet, we have to go through the ECEF transform as an intermediate step. 
		FCartesianCoordinates ECEFCoordinates;
		EngineToECEF(EngineCoordinates, ECEFCoordinates);
		ECEFToProjected(ECEFCoordinates, ProjectedCoordinates);
	}
	break;

	case EPlanetShape::FlatPlanet:
	default:
	{
		// in FlatPlanet, the transform is simply a translation
		// Before any conversion, consider the internal UE rebasing
		FIntVector UERebasingOffset = GetWorld()->OriginLocation;
		FVector3d UERebasedCoordinates(
			EngineCoordinates.X + UERebasingOffset.X,
			EngineCoordinates.Y + UERebasingOffset.Y,
			EngineCoordinates.Z + UERebasingOffset.Z);

		// Convert UE units to meters, invert the Y coordinate because of left-handed UE Frame
		FVector3d UEWorldCoordinates = UERebasedCoordinates * FVector3d(0.01, -0.01, 0.01);

		// Add the defined origin offset
		FVector3d Projected = UEWorldCoordinates + Impl->WorldOriginLocationProjected;
		ProjectedCoordinates = FCartesianCoordinates(Projected);
	}
	break;
	}
}

void AGeoReferencingSystem::ProjectedToEngine(const FCartesianCoordinates& ProjectedCoordinates, FVector& EngineCoordinates)
{
	switch (PlanetShape)
	{
	case EPlanetShape::RoundPlanet:
	{
		// In RoundPlanet, we have to go through the ECEF transform as an intermediate step. 
		FCartesianCoordinates ECEFCoordinates;
		ProjectedToECEF(ProjectedCoordinates, ECEFCoordinates);
		ECEFToEngine(ECEFCoordinates, EngineCoordinates);
	}
	break;

	case EPlanetShape::FlatPlanet:
	default:
	{
		// in FlatPlanet, the transform is simply a translation
		// Remove the Origin location, and convert to UE Units, while inverting the Z Axis
		FVector3d Projected(ProjectedCoordinates.X, ProjectedCoordinates.Y, ProjectedCoordinates.Z);
		FVector3d UEWorldCoordinates = (Projected - Impl->WorldOriginLocationProjected);

		// Convert UE units to meters, invert the Y coordinate because of left-handed UE Frame
		FVector3d UERebasedCoordinates = UEWorldCoordinates * FVector3d(100.0, -100.0, 100.0);

		// Consider the UE internal rebasing
		FIntVector UERebasingOffset = GetWorld()->OriginLocation;
		FVector3d EngineCoordinates3d = UERebasedCoordinates - FVector3d(UERebasingOffset.X, UERebasingOffset.Y, UERebasingOffset.Z);

		EngineCoordinates = FVector(EngineCoordinates3d.X, EngineCoordinates3d.Y, EngineCoordinates3d.Z);
	}
	break;
	}
}

void AGeoReferencingSystem::EngineToECEF(const FVector& EngineCoordinates, FCartesianCoordinates& ECEFCoordinates)
{
	switch (PlanetShape)
	{
	case EPlanetShape::RoundPlanet:
	{
		// Before any conversion, consider the internal UE rebasing
		FIntVector UERebasingOffset = GetWorld()->OriginLocation;
		FVector3d UERebasedCoordinates(
			EngineCoordinates.X + UERebasingOffset.X,
			EngineCoordinates.Y + UERebasingOffset.Y,
			EngineCoordinates.Z + UERebasingOffset.Z);

		// Convert UE units to meters, invert the Y coordinate because of left-handed UE Frame
		FVector3d UEWorldCoordinates = UERebasedCoordinates * FVector3d(0.01, -0.01, 0.01);

		if (bOriginAtPlanetCenter)
		{
			// Easy case, UE is ECEF... And we did the rebasing, so return the Global coordinates
			ECEFCoordinates = UEWorldCoordinates; // TOCHECK Types
		}
		else
		{
			FVector4d ECEFCoordinates4d = Impl->WorldFrameToECEFFrame * FVector4d(UEWorldCoordinates.X, UEWorldCoordinates.Y, UEWorldCoordinates.Z, 1.0);
			ECEFCoordinates = FCartesianCoordinates(ECEFCoordinates4d);
		}
	}
	break;

	case EPlanetShape::FlatPlanet:
	default:
	{
		// In FlatPlanet, we have to go through the Projected transform as an intermediate step. 
		FCartesianCoordinates ProjectedCoordinates;
		EngineToProjected(EngineCoordinates, ProjectedCoordinates);
		ProjectedToECEF(ProjectedCoordinates, ECEFCoordinates);
	}
	break;
	}
}

void AGeoReferencingSystem::ECEFToEngine(const FCartesianCoordinates& ECEFCoordinates, FVector& EngineCoordinates)
{
	switch (PlanetShape)
	{
	case EPlanetShape::RoundPlanet:
	{
		FVector3d UEWorldCoordinates;
		if (bOriginAtPlanetCenter)
		{
			// Easy case, UE is ECEF... And we did the rebasing, so return the Global coordinates
			UEWorldCoordinates = FVector3d(ECEFCoordinates.X, ECEFCoordinates.Y, ECEFCoordinates.Z);
		}
		else
		{
			FVector4d UEWorldCoordinates4d = Impl->ECEFFrameToWorldFrame * FVector4d(ECEFCoordinates.X, ECEFCoordinates.Y, ECEFCoordinates.Z, 1.0);
			UEWorldCoordinates.X = UEWorldCoordinates4d.X;
			UEWorldCoordinates.Y = UEWorldCoordinates4d.Y;
			UEWorldCoordinates.Z = UEWorldCoordinates4d.Z;
		}

		// Convert UE units to meters, invert the Y coordinate because of left-handed UE Frame
		FVector3d UERebasedCoordinates = UEWorldCoordinates * FVector3d(100.0, -100.0, 100.0);

		// Consider the UE internal rebasing
		FIntVector UERebasingOffset = GetWorld()->OriginLocation;
		FVector3d EngineCoordinates3d = UERebasedCoordinates - FVector3d(UERebasingOffset.X, UERebasingOffset.Y, UERebasingOffset.Z);

		EngineCoordinates = FVector(EngineCoordinates3d.X, EngineCoordinates3d.Y, EngineCoordinates3d.Z);
	}
	break;

	case EPlanetShape::FlatPlanet:
	default:
	{
		// In FlatPlanet, we have to go through the Projected transform as an intermediate step. 
		FCartesianCoordinates ProjectedCoordinates;
		ECEFToProjected(ECEFCoordinates, ProjectedCoordinates);
		ProjectedToEngine(ProjectedCoordinates, EngineCoordinates);
	}
	break;
	}
}

void AGeoReferencingSystem::ProjectedToGeographic(const FCartesianCoordinates& ProjectedCoordinates, FGeographicCoordinates& GeographicCoordinates)
{
	PJ_COORD input, output;
	input = proj_coord(ProjectedCoordinates.X, ProjectedCoordinates.Y, ProjectedCoordinates.Z, 0);

	output = proj_trans(Impl->ProjProjectedToGeographic, PJ_FWD, input);
	GeographicCoordinates.Latitude = output.lpz.phi;
	GeographicCoordinates.Longitude = output.lpz.lam;
	GeographicCoordinates.Altitude = output.lpz.z;
}

void AGeoReferencingSystem::GeographicToProjected(const FGeographicCoordinates& GeographicCoordinates, FCartesianCoordinates& ProjectedCoordinates)
{
	PJ_COORD input, output;
	input = proj_coord(GeographicCoordinates.Longitude, GeographicCoordinates.Latitude, GeographicCoordinates.Altitude, 0);

	output = proj_trans(Impl->ProjProjectedToGeographic, PJ_INV, input);
	ProjectedCoordinates.X = output.xyz.x;
	ProjectedCoordinates.Y = output.xyz.y;
	ProjectedCoordinates.Z = output.xyz.z;
}

void AGeoReferencingSystem::ProjectedToECEF(const FCartesianCoordinates& ProjectedCoordinates, FCartesianCoordinates& ECEFCoordinates)
{
	PJ_COORD input, output;
	input = proj_coord(ProjectedCoordinates.X, ProjectedCoordinates.Y, ProjectedCoordinates.Z, 0);

	output = proj_trans(Impl->ProjProjectedToECEF, PJ_FWD, input);
	ECEFCoordinates.X = output.xyz.x;
	ECEFCoordinates.Y = output.xyz.y;
	ECEFCoordinates.Z = output.xyz.z;
}

void AGeoReferencingSystem::ECEFToProjected(const FCartesianCoordinates& ECEFCoordinates, FCartesianCoordinates& ProjectedCoordinates)
{
	PJ_COORD input, output;
	input = proj_coord(ECEFCoordinates.X, ECEFCoordinates.Y, ECEFCoordinates.Z, 0);

	output = proj_trans(Impl->ProjProjectedToECEF, PJ_INV, input);
	ProjectedCoordinates.X = output.xyz.x;
	ProjectedCoordinates.Y = output.xyz.y;
	ProjectedCoordinates.Z = output.xyz.z;
}

void AGeoReferencingSystem::GeographicToECEF(const FGeographicCoordinates& GeographicCoordinates, FCartesianCoordinates& ECEFCoordinates)
{
	PJ_COORD input, output;
	input = proj_coord(GeographicCoordinates.Longitude, GeographicCoordinates.Latitude, GeographicCoordinates.Altitude, 0);

	output = proj_trans(Impl->ProjGeographicToECEF, PJ_FWD, input);
	ECEFCoordinates.X = output.xyz.x;
	ECEFCoordinates.Y = output.xyz.y;
	ECEFCoordinates.Z = output.xyz.z;
}

void AGeoReferencingSystem::ECEFToGeographic(const FCartesianCoordinates& ECEFCoordinates, FGeographicCoordinates& GeographicCoordinates)
{
	PJ_COORD input, output;
	input = proj_coord(ECEFCoordinates.X, ECEFCoordinates.Y, ECEFCoordinates.Z, 0);

	output = proj_trans(Impl->ProjGeographicToECEF, PJ_INV, input);
	GeographicCoordinates.Latitude = output.lpz.phi;
	GeographicCoordinates.Longitude = output.lpz.lam;
	GeographicCoordinates.Altitude = output.lpz.z;
}

// ENU & Transforms

void AGeoReferencingSystem::GetENUVectorsAtEngineLocation(const FVector& EngineCoordinates, FVector& East, FVector& North, FVector& Up)
{
	FCartesianCoordinates ECEFLocation;
	EngineToECEF(EngineCoordinates, ECEFLocation);
	GetENUVectorsAtECEFLocation(ECEFLocation, East, North, Up);
}

void AGeoReferencingSystem::GetENUVectorsAtProjectedLocation(const FCartesianCoordinates& ProjectedCoordinates, FVector& East, FVector& North, FVector& Up)
{
	FCartesianCoordinates ECEFLocation;
	ProjectedToECEF(ProjectedCoordinates, ECEFLocation);
	GetENUVectorsAtECEFLocation(ECEFLocation, East, North, Up);
}

void AGeoReferencingSystem::GetENUVectorsAtGeographicLocation(const FGeographicCoordinates& GeographicCoordinates, FVector& East, FVector& North, FVector& Up)
{
	FCartesianCoordinates ECEFLocation;
	GeographicToECEF(GeographicCoordinates, ECEFLocation);
	GetENUVectorsAtECEFLocation(ECEFLocation, East, North, Up);
}

void AGeoReferencingSystem::GetENUVectorsAtECEFLocation(const FCartesianCoordinates& ECEFCoordinates, FVector& East, FVector& North, FVector& Up)
{
	// Compute Tangent matrix at ECEF location
	FEllipsoid& Ellipsoid = Impl->GeographicEllipsoid;

	if (bOriginLocationInProjectedCRS)
	{
		Ellipsoid = Impl->ProjectedEllipsoid;
	}
	FMatrix4d WorldFrameToECEFFrameAtLocation;
	WorldFrameToECEFFrameAtLocation = Impl->GetWorldFrameToECEFFrame(Ellipsoid, ECEFCoordinates);

	switch (PlanetShape)
	{
	case EPlanetShape::RoundPlanet:
	{
		FMatrix4d UEtoECEF = Impl->UEFrameToWorldFrame * Impl->ECEFFrameToWorldFrame * WorldFrameToECEFFrameAtLocation; //TOOD?

		East = FVector(UEtoECEF.Row0.X, UEtoECEF.Row1.X, UEtoECEF.Row2.X);
		North = FVector(UEtoECEF.Row0.Y, UEtoECEF.Row1.Y, UEtoECEF.Row2.Y);
		Up = FVector(UEtoECEF.Row0.Z, UEtoECEF.Row1.Z, UEtoECEF.Row2.Z);
	}
	break;

	case EPlanetShape::FlatPlanet:
	default:
		// PROJ don't provide anything to project direction vectors. Let's do it by hand...
		FVector3d EasternPoint = ECEFCoordinates.ToVector3d() + WorldFrameToECEFFrameAtLocation * FVector3d(1.0, 0.0, 0.0);
		FVector3d NorthernPoint = ECEFCoordinates.ToVector3d() + WorldFrameToECEFFrameAtLocation * FVector3d(0.0, 1.0, 0.0);

		FCartesianCoordinates ProjectedOrigin, ProjectedEastern, ProjectedNorthern;
		ECEFToProjected(ECEFCoordinates, ProjectedOrigin);
		ECEFToProjected(EasternPoint, ProjectedEastern);
		ECEFToProjected(NorthernPoint, ProjectedNorthern);

		FVector3d East3d(ProjectedEastern.X - ProjectedOrigin.X, ProjectedEastern.Y - ProjectedOrigin.Y, ProjectedEastern.Z - ProjectedOrigin.Z);
		FVector3d North3d(ProjectedNorthern.X - ProjectedOrigin.X, ProjectedNorthern.Y - ProjectedOrigin.Y, ProjectedNorthern.Z - ProjectedOrigin.Z);

		East3d.Normalize();
		North3d.Normalize();

		East = FVector(East3d.X, -East3d.Y, East3d.Z);
		North = FVector(North3d.X, -North3d.Y, North3d.Z);
		Up = FVector::CrossProduct(North, East);
		break;
	}
}

void AGeoReferencingSystem::GetECEFENUVectorsAtECEFLocation(const FCartesianCoordinates& ECEFCoordinates, FVector& ECEFEast, FVector& ECEFNorth, FVector& ECEFUp)
{
	// Compute Tangent matrix at ECEF location
	FEllipsoid& Ellipsoid = Impl->GeographicEllipsoid;
	
	if (bOriginLocationInProjectedCRS)
	{
		Ellipsoid = Impl->ProjectedEllipsoid;
	}
	
	FMatrix4d WorldFrameToECEFFrameAtLocation;
	WorldFrameToECEFFrameAtLocation = Impl->GetWorldFrameToECEFFrame(Ellipsoid, ECEFCoordinates);

	ECEFEast = FVector(WorldFrameToECEFFrameAtLocation.Row0.X, WorldFrameToECEFFrameAtLocation.Row1.X, WorldFrameToECEFFrameAtLocation.Row2.X);
	ECEFNorth = FVector(WorldFrameToECEFFrameAtLocation.Row0.Y, WorldFrameToECEFFrameAtLocation.Row1.Y, WorldFrameToECEFFrameAtLocation.Row2.Y);
	ECEFUp = FVector(WorldFrameToECEFFrameAtLocation.Row0.Z, WorldFrameToECEFFrameAtLocation.Row1.Z, WorldFrameToECEFFrameAtLocation.Row2.Z);
}

FTransform AGeoReferencingSystem::GetTangentTransformAtEngineLocation(const FVector& EngineCoordinates)
{
	FCartesianCoordinates ECEFLocation;
	EngineToECEF(EngineCoordinates, ECEFLocation);
	return GetTangentTransformAtECEFLocation(ECEFLocation);
}

FTransform AGeoReferencingSystem::GetTangentTransformAtProjectedLocation(const FCartesianCoordinates& ProjectedCoordinates)
{
	FCartesianCoordinates ECEFLocation;
	ProjectedToECEF(ProjectedCoordinates, ECEFLocation);
	return GetTangentTransformAtECEFLocation(ECEFLocation);
}

FTransform AGeoReferencingSystem::GetTangentTransformAtGeographicLocation(const FGeographicCoordinates& GeographicCoordinates)
{
	FCartesianCoordinates ECEFLocation;
	GeographicToECEF(GeographicCoordinates, ECEFLocation);
	return GetTangentTransformAtECEFLocation(ECEFLocation);
}

FTransform AGeoReferencingSystem::GetTangentTransformAtECEFLocation(const FCartesianCoordinates& ECEFCoordinates)
{
	if (PlanetShape == EPlanetShape::RoundPlanet)
	{
		// Compute Tangent matrix at ECEF location
		FEllipsoid& Ellipsoid = Impl->GeographicEllipsoid;
		if (bOriginLocationInProjectedCRS)
		{
			Ellipsoid = Impl->ProjectedEllipsoid;
		}
		FMatrix4d WorldFrameToECEFFrameAtLocation;
		WorldFrameToECEFFrameAtLocation = Impl->GetWorldFrameToECEFFrame(Ellipsoid, ECEFCoordinates);


		FMatrix4d UEtoECEF = Impl->UEFrameToWorldFrame * Impl->ECEFFrameToWorldFrame * WorldFrameToECEFFrameAtLocation * Impl->UEFrameToWorldFrame; //TOOD?

		FVector East = FVector(UEtoECEF.Row0.X, UEtoECEF.Row1.X, UEtoECEF.Row2.X);
		FVector North = FVector(UEtoECEF.Row0.Y, UEtoECEF.Row1.Y, UEtoECEF.Row2.Y);
		FVector Up = FVector(UEtoECEF.Row0.Z, UEtoECEF.Row1.Z, UEtoECEF.Row2.Z);
		FVector EngineLocation;
		ECEFToEngine(ECEFCoordinates, EngineLocation);

		FMatrix ResultMatrix = FMatrix::Identity;
		ResultMatrix.SetAxes(&East, &North, &Up, &EngineLocation);
		return FTransform(ResultMatrix);
	}

	return FTransform::Identity;
}

FTransform AGeoReferencingSystem::GetPlanetCenterTransform()
{
	// Compute Origin location in ECEF. 
	if (PlanetShape == EPlanetShape::RoundPlanet)
	{
		if (bOriginAtPlanetCenter)
		{
			return FTransform::Identity;
		}
		else
		{
			FMatrix4d TransformMatrix = Impl->UEFrameToWorldFrame * Impl->ECEFFrameToWorldFrame * Impl->UEFrameToWorldFrame;
			// Don't go to transform yet, we must stay in double to apply the rebasing offset. 

			// Get Origin
			FVector3d Origin3d(TransformMatrix.Row0.W, TransformMatrix.Row1.W, TransformMatrix.Row2.W);
			// Convert UE units to meters, invert the Y coordinate because of left-handed UE Frame
			Origin3d = Origin3d * FVector3d(100.0, -100.0, 100.0);


			// Consider the UE internal rebasing
			FIntVector UERebasingOffset = GetWorld()->OriginLocation;
			FVector3d UERebasedCoordinates = Origin3d - FVector3d(UERebasingOffset.X, UERebasingOffset.Y, UERebasingOffset.Z);
			FVector Origin = FVector(UERebasedCoordinates.X, UERebasedCoordinates.Y, UERebasedCoordinates.Z);


			FVector East, North, Up;
			East = FVector(TransformMatrix.Row0.X, TransformMatrix.Row1.X, TransformMatrix.Row2.X);
			North = FVector(TransformMatrix.Row0.Y, TransformMatrix.Row1.Y, TransformMatrix.Row2.Y);
			Up = FVector(TransformMatrix.Row0.Z, TransformMatrix.Row1.Z, TransformMatrix.Row2.Z);


			FMatrix ResultMatrix = FMatrix::Identity;
			ResultMatrix.SetAxes(&East, &North, &Up, &Origin);
			return FTransform(ResultMatrix);
		}

	}
	else
	{
		// Makes not sense in Flat planet mode... 
		return FTransform::Identity;
	}


}

// Public PROJ Utilities

bool AGeoReferencingSystem::IsCRSStringValid(FString CRSString, FString& Error)
{
	if (Impl->ProjContext == nullptr)
	{
		Error = FString("Proj Context has not been initialized");
		return false;
	}

	// Try to create a CRS from this string
	FTCHARToUTF8 Convert(*CRSString);
	const ANSICHAR* UtfString = Convert.Get();
	PJ* CRS = proj_create(Impl->ProjContext, UtfString);

	if (CRS == nullptr)
	{
		int ErrorNumber = proj_context_errno(Impl->ProjContext);
		Error = FString(proj_errno_string(ErrorNumber));
		return false;
	}

	proj_destroy(CRS);
	return true;
}

bool AGeoReferencingSystem::FGeoReferencingSystemInternals::GetEllipsoid(FString CRSString, FEllipsoid& Ellipsoid)
{
	FTCHARToUTF8 ConvertCRSString(*CRSString);
	const ANSICHAR* CRS = ConvertCRSString.Get();
	bool bSuccess = true;

	PJ* CRSPJ = proj_create(ProjContext, CRS);
	if (CRSPJ != nullptr)
	{
		PJ* EllipsoidPJ = proj_get_ellipsoid(ProjContext, CRSPJ);
		if (EllipsoidPJ != nullptr)
		{
			double SemiMajorMetre; // semi-major axis in meter
			double SemiMinorMetre; // semi-minor axis in meter
			double InvFlattening;  // inverse flattening.
			int IsSemiMinorComputed; // if the semi-minor value was computed. If FALSE, its value comes from the definition

			if (proj_ellipsoid_get_parameters(ProjContext, EllipsoidPJ, &SemiMajorMetre, &SemiMinorMetre, &IsSemiMinorComputed, &InvFlattening))
			{
				Ellipsoid = FEllipsoid(SemiMajorMetre, SemiMajorMetre, SemiMinorMetre);
			}
			else
			{
				int ErrorNumber = proj_context_errno(ProjContext);
				FString ProjError = FString(proj_errno_string(ErrorNumber));
				UE_LOG(LogGeoReferencing, Error, TEXT("AGeoReferencingSystem::GetEllipsoid failed in proj_ellipsoid_get_parameters : %s "), *ProjError);
				bSuccess = false;
			}

			proj_destroy(EllipsoidPJ);
		}
		else
		{
			int ErrorNumber = proj_context_errno(ProjContext);
			FString ProjError = FString(proj_errno_string(ErrorNumber));
			UE_LOG(LogGeoReferencing, Error, TEXT("AGeoReferencingSystem::GetEllipsoid failed in proj_get_ellipsoid : %s "), *ProjError);
			bSuccess = false;
		}

		proj_destroy(CRSPJ);
	}
	else
	{
		int ErrorNumber = proj_context_errno(ProjContext);
		FString ProjError = FString(proj_errno_string(ErrorNumber));
		UE_LOG(LogGeoReferencing, Error, TEXT("AGeoReferencingSystem::GetEllipsoid failed in proj_create : %s "), *ProjError);
		UE_LOG(LogGeoReferencing, Display, TEXT("CRSString was : %s "), *CRSString);
		bSuccess = false;
	}

	return bSuccess;
}

void AGeoReferencingSystem::ApplySettings()
{
	// Apply Projection settings

	// Projected -> Geographic
	if (Impl->ProjProjectedToGeographic != nullptr)
	{
		proj_destroy(Impl->ProjProjectedToGeographic);
	}
	Impl->ProjProjectedToGeographic = Impl->GetPROJProjection(ProjectedCRS, GeographicCRS);

	// Projected -> Geocentric
	if (Impl->ProjProjectedToECEF != nullptr)
	{
		proj_destroy(Impl->ProjProjectedToECEF);
	}
	Impl->ProjProjectedToECEF = Impl->GetPROJProjection(ProjectedCRS, ECEF_EPSG_FSTRING);

	// Geographic -> Geocentric
	if (Impl->ProjGeographicToECEF != nullptr)
	{
		proj_destroy(Impl->ProjGeographicToECEF);
	}
	Impl->ProjGeographicToECEF = Impl->GetPROJProjection(GeographicCRS, ECEF_EPSG_FSTRING);

	bool bProjectedEllipsoidSuccess = Impl->GetEllipsoid(ProjectedCRS, Impl->ProjectedEllipsoid);
	bool bGeographicEllipsoidSuccess = Impl->GetEllipsoid(GeographicCRS, Impl->GeographicEllipsoid);

	bool bSuccess = Impl->ProjProjectedToGeographic != nullptr && Impl->ProjProjectedToECEF != nullptr && Impl->ProjGeographicToECEF != nullptr && bProjectedEllipsoidSuccess && bGeographicEllipsoidSuccess;

#if WITH_EDITOR
	if (!bSuccess)
	{
		// Show an error notification
		const FText NotificationErrorText = NSLOCTEXT("GeoReferencing", "GeoReferencingCRSError", "Error in one CRS definition string - Check log");
		FNotificationInfo Info(NotificationErrorText);
		Info.ExpireDuration = 2.0f;
		Info.bUseSuccessFailIcons = true;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}
#endif

	// Apply Origin settings

	switch (PlanetShape)
	{
	case EPlanetShape::RoundPlanet:
		// Matrice UE to ECEF :
		if (bOriginAtPlanetCenter)
		{
			// Theoritically, we should never use this Identity matrices since the transformations already handle that using a shorter code path, but let's keep consistency  
			Impl->ECEFFrameToWorldFrame = FMatrix4d::Identity();
			Impl->WorldFrameToECEFFrame = FMatrix4d::Identity();
		}
		else
		{
			// We need to compute the ENU Vectors at the origin point. For that, the origin has to be expressed first in ECEF. 
			// Express origin in ECEF, and get the ENU vectors
			FCartesianCoordinates ECEFOrigin;
			Impl->WorldFrameToECEFFrame = FMatrix4d::Identity();
			if (bOriginLocationInProjectedCRS)
			{
				FCartesianCoordinates ProjectedOrigin(
					OriginProjectedCoordinatesEasting,
					OriginProjectedCoordinatesNorthing,
					OriginProjectedCoordinatesUp);
				ProjectedToECEF(ProjectedOrigin, ECEFOrigin);

				Impl->WorldFrameToECEFFrame = Impl->GetWorldFrameToECEFFrame(Impl->ProjectedEllipsoid, ECEFOrigin);
			}
			else
			{
				GeographicToECEF(FGeographicCoordinates(OriginLongitude, OriginLatitude, OriginAltitude), ECEFOrigin);
				Impl->WorldFrameToECEFFrame = Impl->GetWorldFrameToECEFFrame(Impl->GeographicEllipsoid, ECEFOrigin);
			}
			Impl->ECEFFrameToWorldFrame = Impl->WorldFrameToECEFFrame.Inverse();
		}
		break;

	case EPlanetShape::FlatPlanet:
	default:
		if (bOriginLocationInProjectedCRS)
		{
			// World origin is expressed using Projected coordinates. Take them as is (in double)
			Impl->WorldOriginLocationProjected = FVector3d(
				OriginProjectedCoordinatesEasting,
				OriginProjectedCoordinatesNorthing,
				OriginProjectedCoordinatesUp);
		}
		else
		{
			// World origin is expressed using Geographic coordinates. Convert them to the projected CRS to have the offset
			FCartesianCoordinates OriginProjected;
			GeographicToProjected(FGeographicCoordinates(OriginLongitude, OriginLatitude, OriginAltitude), OriginProjected);
			Impl->WorldOriginLocationProjected = FVector3d(OriginProjected.X, OriginProjected.Y, OriginProjected.Z);
		}
		break;
	}
	return;
}

void AGeoReferencingSystem::FGeoReferencingSystemInternals::InitPROJLibrary()
{
	// Initialize proj context

	// Get the base directory of this plugin
	FString BaseDir;
	BaseDir = IPluginManager::Get().FindPlugin("GeoReferencing")->GetBaseDir();

	// Add on the relative location of the third party dll and load it
	FString ProjDataPathPath;
	ProjDataPathPath = FPaths::Combine(*BaseDir, TEXT("Resources/PROJ"));

	UE_LOG(LogGeoReferencing, Display, TEXT("Initializing Proj context using Data in '%s'"), *ProjDataPathPath);
	ProjContext = proj_context_create();
	if (ProjContext == nullptr)
	{
		UE_LOG(LogGeoReferencing, Error, TEXT("proj_context_create() failed - Check DLL dependencies"));
	}
	FTCHARToUTF8 Convert(*ProjDataPathPath);
	const ANSICHAR* Temp = Convert.Get();
	proj_context_set_search_paths(ProjContext, 1, &Temp);
}

void AGeoReferencingSystem::FGeoReferencingSystemInternals::DeInitPROJLibrary()
{
	// Destroy projections
	if (ProjProjectedToGeographic != nullptr)
	{
		proj_destroy(ProjProjectedToGeographic);
		ProjProjectedToGeographic = nullptr;
	}
	if (ProjProjectedToECEF != nullptr)
	{
		proj_destroy(ProjProjectedToECEF);
		ProjProjectedToECEF = nullptr;
	}
	if (ProjGeographicToECEF != nullptr)
	{
		proj_destroy(ProjGeographicToECEF);
		ProjGeographicToECEF = nullptr;
	}

	// Destroy proj context
	if (ProjContext != nullptr)
	{
		proj_context_destroy(ProjContext); /* may be omitted in the single threaded case */
		ProjContext = nullptr;
	}
}

PJ* AGeoReferencingSystem::FGeoReferencingSystemInternals::GetPROJProjection(FString SourceCRS, FString DestinationCRS)
{
	FTCHARToUTF8 ConvertSource(*SourceCRS);
	FTCHARToUTF8 ConvertDestination(*DestinationCRS);
	const ANSICHAR* Source = ConvertSource.Get();
	const ANSICHAR* Destination = ConvertDestination.Get();

	PJ* TempPJ = proj_create_crs_to_crs(ProjContext, Source, Destination, nullptr);
	if (TempPJ == nullptr)
	{
		int ErrorNumber = proj_context_errno(ProjContext);
		FString ProjError = FString(proj_errno_string(ErrorNumber));
		UE_LOG(LogGeoReferencing, Error, TEXT("AGeoReferencingSystem::BuildProjection failed in proj_create_crs_to_crs : %s "), *ProjError);

		UE_LOG(LogGeoReferencing, Display, TEXT("SourceCRS was : %s "), *SourceCRS);
		UE_LOG(LogGeoReferencing, Display, TEXT("DestinationCRS was : %s "), *DestinationCRS);

		return nullptr;
	}

	/* This will ensure that the order of coordinates for the input CRS */
	/* will be longitude, latitude, whereas EPSG:4326 mandates latitude, longitude */
	PJ* P_for_GIS = proj_normalize_for_visualization(ProjContext, TempPJ);
	if (P_for_GIS == nullptr)
	{
		int ErrorNumber = proj_context_errno(ProjContext);
		FString ProjError = FString(proj_errno_string(ErrorNumber));
		UE_LOG(LogGeoReferencing, Error, TEXT("AGeoReferencingSystem::BuildProjection failed in proj_normalize_for_visualization : %s "), *ProjError);
	}

	proj_destroy(TempPJ);
	return P_for_GIS;
}

FMatrix4d AGeoReferencingSystem::FGeoReferencingSystemInternals::GetWorldFrameToECEFFrame(const FEllipsoid& Ellipsoid, const FCartesianCoordinates& ECEFLocation)
{
	// See ECEF standard : https://commons.wikimedia.org/wiki/File:ECEF_ENU_Longitude_Latitude_right-hand-rule.svg
	if (FMath::Abs(ECEFLocation.X) < FMathd::Epsilon &&
		FMath::Abs(ECEFLocation.Y) < FMathd::Epsilon)
	{
		// Special Case - On earth axis... 
		if (FMath::Abs(ECEFLocation.Z) < FMathd::Epsilon)
		{
			// At origin - Should not happen, but consider it's the same as north pole
			return FMatrix4d(
				FVector4d(0.0, 1.0, 0.0, 0.0),    // East = Y
				FVector4d(-1.0, 0.0, 0.0, 0.0), // North = -X
				FVector4d(0.0, 0.0, 1.0, 0.0),    // Up = Z
				FVector4d(ECEFLocation.X, ECEFLocation.Y, ECEFLocation.Z, 1.0), // Location
				false); // Expressed in columns
		}
		else
		{
			// At South or North pole - Axis are set to be continuous with other points
			int Sign = FMathd::SignNonZero(ECEFLocation.Z);
			return FMatrix4d(
				FVector4d(0.0, 1.0, 0.0, 0.0),    // East = Y
				FVector4d(-1.0 * Sign, 0.0, 0.0, 0.0), // North = -X
				FVector4d(0.0, 0.0, 1.0 * Sign, 0.0),    // Up = Z
				FVector4d(ECEFLocation.X, ECEFLocation.Y, ECEFLocation.Z, 1.0), // Location
				false); // Expressed in columns
		}
	}
	else
	{
		FVector3d Up = Ellipsoid.GeodeticSurfaceNormal(ECEFLocation);
		FVector3d East = FVector3d(-ECEFLocation.Y, ECEFLocation.X, 0.0).Normalized();
		FVector3d North = Up.Cross(East);

		return FMatrix4d(
			FVector4d(East.X, East.Y, East.Z, 0.0),        // East
			FVector4d(North.X, North.Y, North.Z, 0.0),    // North
			FVector4d(Up.X, Up.Y, Up.Z, 0.0),            // Up
			FVector4d(ECEFLocation.X, ECEFLocation.Y, ECEFLocation.Z, 1.0), // Location
			false); // Expressed in columns
	}
}

FMatrix AGeoReferencingSystem::FGeoReferencingSystemInternals::ConvertMatrix4d(FMatrix4d Matrix)
{
	return FMatrix(
		FVector(Matrix.Row0.X, Matrix.Row1.X, Matrix.Row2.X), // 1st Column
		FVector(Matrix.Row0.Y, Matrix.Row1.Y, Matrix.Row2.Y), // 2nd Column
		FVector(Matrix.Row0.Z, Matrix.Row1.Z, Matrix.Row2.Z), // 3rd Column
		FVector(Matrix.Row0.W, Matrix.Row1.W, Matrix.Row2.W)  // 4th Column - Translation
	);
}

#if WITH_EDITOR
void AGeoReferencingSystem::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	//Get the name of the property that was changed  
	FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(AGeoReferencingSystem, ProjectedCRS) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AGeoReferencingSystem, GeographicCRS) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AGeoReferencingSystem, bOriginAtPlanetCenter) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AGeoReferencingSystem, bOriginLocationInProjectedCRS) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AGeoReferencingSystem, OriginLatitude) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AGeoReferencingSystem, OriginLongitude) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AGeoReferencingSystem, OriginAltitude) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AGeoReferencingSystem, OriginProjectedCoordinatesEasting) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AGeoReferencingSystem, OriginProjectedCoordinatesNorthing) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AGeoReferencingSystem, OriginProjectedCoordinatesUp) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AGeoReferencingSystem, PlanetShape))
	{
		ApplySettings();
	}

	// Call the base class version  
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif