// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfigurationClusterSerializer.h"

#include "ILevelSnapshotsModule.h"
#include "Serialization/ObjectSnapshotSerializationData.h"

#include "UObject/UnrealType.h"

UClass* FDisplayClusterConfigurationClusterSerializer::GetSupportedClass()
{
	static const FSoftClassPath ClassPath("/Script/DisplayClusterConfiguration.DisplayClusterConfigurationCluster");
	return ClassPath.ResolveClass();
}

void FDisplayClusterConfigurationClusterSerializer::BlacklistCustomProperties(ILevelSnapshotsModule& Module)
{
	const FProperty* NodesProperty = GetMapProperty();
	if (ensure(NodesProperty))
	{
		Module.AddBlacklistedProperties( { NodesProperty } );
	}
}

void FDisplayClusterConfigurationClusterSerializer::Register(ILevelSnapshotsModule& Module)
{
	BlacklistCustomProperties(Module);
	Module.RegisterCustomObjectSerializer(GetSupportedClass(), MakeShared<FDisplayClusterConfigurationClusterSerializer>());
}

const FMapProperty* FDisplayClusterConfigurationClusterSerializer::GetMapProperty()
{
	const FName NodesPropertyName("Nodes");
	const FMapProperty* Result = CastField<FMapProperty>(GetSupportedClass()->FindPropertyByName(NodesPropertyName));
	check(Result);
	return Result;
}
