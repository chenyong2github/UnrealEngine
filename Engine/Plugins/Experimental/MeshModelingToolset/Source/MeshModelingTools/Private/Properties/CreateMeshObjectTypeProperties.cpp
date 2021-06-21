// Copyright Epic Games, Inc. All Rights Reserved.

#include "Properties/CreateMeshObjectTypeProperties.h"
#include "ModelingObjectsCreationAPI.h"

static TAutoConsoleVariable<int32> CVarEnableDynamicMeshActors(
	TEXT("geometry.DynamicMeshActor.Enable"),
	0,
	TEXT("Enable DynamicMeshActor Output Type in various Modeling Mode Tools"));

const FString UCreateMeshObjectTypeProperties::StaticMeshIdentifier = TEXT("Static Mesh");
const FString UCreateMeshObjectTypeProperties::VolumeIdentifier = TEXT("Volume");
const FString UCreateMeshObjectTypeProperties::DynamicMeshActorIdentifier = TEXT("Dynamic Mesh");

void UCreateMeshObjectTypeProperties::InitializeDefault()
{
	bool bStaticMeshes = true;
	bool bVolumes = false;
	bool bDynamicMeshes = true;

#if WITH_EDITOR
	bVolumes = true;
#endif

	Initialize(bStaticMeshes, bVolumes, bDynamicMeshes);
}

void UCreateMeshObjectTypeProperties::Initialize(bool bEnableStaticMeshes, bool bEnableVolumes, bool bEnableDynamicMeshActor)
{
	if (bEnableStaticMeshes)
	{
		OutputTypeNamesList.Add(StaticMeshIdentifier);
	}
	if (bEnableVolumes)
	{
		OutputTypeNamesList.Add(VolumeIdentifier);
	}
	if (bEnableDynamicMeshActor && CVarEnableDynamicMeshActors.GetValueOnGameThread() != 0)
	{
		OutputTypeNamesList.Add(DynamicMeshActorIdentifier);
	}

	if ((OutputType.Len() == 0) || (OutputType.Len() > 0 && OutputTypeNamesList.Contains(OutputType) == false))
	{
		OutputType = OutputTypeNamesList[0];
	}
}

const TArray<FString>& UCreateMeshObjectTypeProperties::GetOutputTypeNamesFunc()
{
	return OutputTypeNamesList;
}



bool UCreateMeshObjectTypeProperties::ShouldShowPropertySet() const
{
	return (OutputTypeNamesList.Num() > 1)
		|| OutputTypeNamesList.Contains(VolumeIdentifier);
}

ECreateObjectTypeHint UCreateMeshObjectTypeProperties::GetCurrentCreateMeshType() const
{
	if (OutputType == StaticMeshIdentifier)
	{
		return ECreateObjectTypeHint::StaticMesh;
	}
	else if (OutputType == VolumeIdentifier)
	{
		return ECreateObjectTypeHint::Volume;
	}
	else if (OutputType == DynamicMeshActorIdentifier)
	{
		return ECreateObjectTypeHint::DynamicMeshActor;
	}
	return ECreateObjectTypeHint::Undefined;
}

void UCreateMeshObjectTypeProperties::UpdatePropertyVisibility()
{
	bShowVolumeList = (OutputType == VolumeIdentifier);
}


bool UCreateMeshObjectTypeProperties::ConfigureCreateMeshObjectParams(FCreateMeshObjectParams& ParamsOut) const
{
	if (OutputType == StaticMeshIdentifier)
	{
		ParamsOut.TypeHint = ECreateObjectTypeHint::StaticMesh;
		return true;
	}
	else if (OutputType == VolumeIdentifier)
	{
		ParamsOut.TypeHint = ECreateObjectTypeHint::Volume;
		ParamsOut.TypeHintClass = VolumeType.Get();
		return true;
	}
	else if (OutputType == DynamicMeshActorIdentifier)
	{
		ParamsOut.TypeHint = ECreateObjectTypeHint::DynamicMeshActor;
		return true;
	}
	return false;
}
