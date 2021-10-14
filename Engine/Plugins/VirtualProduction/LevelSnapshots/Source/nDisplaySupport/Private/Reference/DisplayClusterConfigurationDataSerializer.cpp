// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfigurationDataSerializer.h"

#include "ILevelSnapshotsModule.h"
#include "UObject/UnrealType.h"

namespace
{
	const FName ClusterPropertyName("Cluster");
}

UClass* FDisplayClusterConfigurationDataSerializer::GetSupportedClass()
{
	static const FSoftClassPath ClassPath("/Script/DisplayClusterConfiguration.DisplayClusterConfigurationData");
	return ClassPath.ResolveClass();
}

void FDisplayClusterConfigurationDataSerializer::BlacklistCustomProperties(ILevelSnapshotsModule& Module)
{
	const FName ExportedObjectsPropertyName("ExportedObjects");
	
	const FSoftClassPath ClassPath("/Script/DisplayClusterConfiguration.DisplayClusterConfigurationData_Base");
	UClass* ConfigDataBaseClass = ClassPath.ResolveClass();
	check(ConfigDataBaseClass);
	const FProperty* ExportedObjectsProperty = ConfigDataBaseClass->FindPropertyByName(ExportedObjectsPropertyName);
	
	const FProperty* ClusterProperty = GetSupportedClass()->FindPropertyByName(ClusterPropertyName);
	if (ensure(ClusterProperty && ExportedObjectsProperty))
	{
		Module.AddBlacklistedProperties( { ClusterProperty, ExportedObjectsProperty} );
	}
}

void FDisplayClusterConfigurationDataSerializer::Register(ILevelSnapshotsModule& Module)
{
	BlacklistCustomProperties(Module);
	Module.RegisterCustomObjectSerializer(GetSupportedClass(), MakeShared<FDisplayClusterConfigurationDataSerializer>());
}

UObject* FDisplayClusterConfigurationDataSerializer::FindSubobject(UObject* Owner) const
{
	const FObjectProperty* ObjectProp = CastField<FObjectProperty>(GetSupportedClass()->FindPropertyByName(ClusterPropertyName));
	checkf(ObjectProp, TEXT("Expected class %s to have object property %s"), *GetSupportedClass()->GetName(), *ClusterPropertyName.ToString());
	check(Owner->GetClass()->IsChildOf(GetSupportedClass()));

	return ObjectProp->GetObjectPropertyValue(ObjectProp->ContainerPtrToValuePtr<void>(Owner));
}