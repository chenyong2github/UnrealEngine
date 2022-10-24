// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/GeometryCollectionProximityUtility.h"

class FGeometryCollection;

class CHAOS_API FGeometryCollectionProximityPropertiesInterface : public FManagedArrayInterface
{
public :
	typedef FManagedArrayInterface Super;
	using FManagedArrayInterface::ManagedCollection;

	// Proximity Properties Group Name
	static const FName ProximityPropertiesGroup;
	
	// Attribute: Method to determine proximity
	static const FName ProximityDetectionMethod;
	// Attribute: For convex hull proximity, what separation distance can still be considered as 'proximity'
	static const FName ProximityDistanceThreshold;
	// Attribute: Whether to use the computed proximity graph as a connection graph
	static const FName ProximityAsConnectionGraph;


	struct FProximityProperties
	{
		EProximityMethod Method;
		float DistanceThreshold = 1.0f;
		bool bUseAsConnectionGraph = false;
	};

	FGeometryCollectionProximityPropertiesInterface(FGeometryCollection* InGeometryCollection);

	void InitializeInterface() override;

	void CleanInterfaceForCook() override;
	
	void RemoveInterfaceAttributes() override;

	FProximityProperties GetProximityProperties() const;
	void SetProximityProperties(const FProximityProperties&);
};

