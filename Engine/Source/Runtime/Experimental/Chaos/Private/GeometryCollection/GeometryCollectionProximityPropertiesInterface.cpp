// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionProximityPropertiesInterface.h"
#include "GeometryCollection/GeometryCollection.h"

const FName FGeometryCollectionProximityPropertiesInterface::ProximityPropertiesGroup = "ProximityProperties";
const FName FGeometryCollectionProximityPropertiesInterface::ProximityDistanceThreshold = "DistanceThreshold";
const FName FGeometryCollectionProximityPropertiesInterface::ProximityDetectionMethod = "DetectionMethod";
const FName FGeometryCollectionProximityPropertiesInterface::ProximityAsConnectionGraph = "AsConnectionGraph";

FGeometryCollectionProximityPropertiesInterface::FGeometryCollectionProximityPropertiesInterface(FGeometryCollection* InGeometryCollection)
	: FManagedArrayInterface(InGeometryCollection)
{}

void 
FGeometryCollectionProximityPropertiesInterface::InitializeInterface()
{
	if (!ManagedCollection->HasGroup(ProximityPropertiesGroup))
	{
		ManagedCollection->AddGroup(ProximityPropertiesGroup);
	}

	if (!ManagedCollection->HasAttribute(ProximityDetectionMethod, ProximityPropertiesGroup))
	{
		ManagedCollection->AddAttribute<int32>(ProximityDetectionMethod, ProximityPropertiesGroup);
	}

	if (!ManagedCollection->HasAttribute(ProximityDistanceThreshold, ProximityPropertiesGroup))
	{
		ManagedCollection->AddAttribute<float>(ProximityDistanceThreshold, ProximityPropertiesGroup);
	}

	if (!ManagedCollection->HasAttribute(ProximityAsConnectionGraph, ProximityPropertiesGroup))
	{
		ManagedCollection->AddAttribute<bool>(ProximityAsConnectionGraph, ProximityPropertiesGroup);
	}
}

void 
FGeometryCollectionProximityPropertiesInterface::CleanInterfaceForCook()
{
	RemoveInterfaceAttributes();
}

void 
FGeometryCollectionProximityPropertiesInterface::RemoveInterfaceAttributes()
{
	ManagedCollection->RemoveGroup(ProximityPropertiesGroup);
}

FGeometryCollectionProximityPropertiesInterface::FProximityProperties
FGeometryCollectionProximityPropertiesInterface::GetProximityProperties() const
{
	FProximityProperties Properties;
	const bool bHasProximityProperties = ManagedCollection->NumElements(ProximityPropertiesGroup) > 0;
	if (bHasProximityProperties)
	{
		const TManagedArray<bool>& AsConnectionGraph = ManagedCollection->GetAttribute<bool>(ProximityAsConnectionGraph, ProximityPropertiesGroup);
		const TManagedArray<int32>& DetectionMethod = ManagedCollection->GetAttribute<int32>(ProximityDetectionMethod, ProximityPropertiesGroup);
		const TManagedArray<float>& DistanceThreshold = ManagedCollection->GetAttribute<float>(ProximityDistanceThreshold, ProximityPropertiesGroup);

		constexpr int32 DefaultIndex = 0;
		Properties.bUseAsConnectionGraph = AsConnectionGraph[DefaultIndex];
		Properties.DistanceThreshold = DistanceThreshold[DefaultIndex];
		Properties.Method = (EProximityMethod)DetectionMethod[DefaultIndex];
	}
	return Properties;
}

void
FGeometryCollectionProximityPropertiesInterface::SetProximityProperties(const FProximityProperties& InProximityAttributes)
{
	FProximityProperties Property;

	int32 AttributeIndex = 0;
	const bool bHasProximityProperties = ManagedCollection->NumElements(ProximityPropertiesGroup) > 0;
	if (!bHasProximityProperties)
	{
		if (!ensure(ManagedCollection->HasGroup(ProximityPropertiesGroup)))
		{
			InitializeInterface();
		}
		AttributeIndex = ManagedCollection->AddElements(1, ProximityPropertiesGroup);
	}

	TManagedArray<bool>& AsConnectionGraph = ManagedCollection->ModifyAttribute<bool>(ProximityAsConnectionGraph, ProximityPropertiesGroup);
	TManagedArray<int32>& Method = ManagedCollection->ModifyAttribute<int32>(ProximityDetectionMethod, ProximityPropertiesGroup);
	TManagedArray<float>& DistanceThreshold = ManagedCollection->ModifyAttribute<float>(ProximityDistanceThreshold, ProximityPropertiesGroup);

	AsConnectionGraph[AttributeIndex] = InProximityAttributes.bUseAsConnectionGraph;
	DistanceThreshold[AttributeIndex] = InProximityAttributes.DistanceThreshold;
	Method[AttributeIndex] = (int32)InProximityAttributes.Method;
}
