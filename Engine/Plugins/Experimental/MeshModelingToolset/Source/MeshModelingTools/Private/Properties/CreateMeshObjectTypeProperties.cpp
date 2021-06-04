// Copyright Epic Games, Inc. All Rights Reserved.

#include "Properties/CreateMeshObjectTypeProperties.h"
#include "ModelingObjectsCreationAPI.h"

const FString UCreateMeshObjectTypeProperties::StaticMeshIdentifier = TEXT("Static Mesh");
const FString UCreateMeshObjectTypeProperties::VolumeIdentifier = TEXT("Volume");

void UCreateMeshObjectTypeProperties::InitializeDefault()
{
	bool bStaticMeshes = true;
	bool bVolumes = false;

#if WITH_EDITOR
	bVolumes = true;
#endif

	Initialize(bStaticMeshes, bVolumes);
}

void UCreateMeshObjectTypeProperties::Initialize(bool bEnableStaticMeshes, bool bEnableVolumes)
{
	if (bEnableStaticMeshes)
	{
		OutputTypeNamesList.Add(StaticMeshIdentifier);
	}
	if (bEnableVolumes)
	{
		OutputTypeNamesList.Add(VolumeIdentifier);
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
	return false;
}
