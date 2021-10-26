// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterRootActorSerializer.h"

#include "ILevelSnapshotsModule.h"
#include "Params/ObjectSnapshotSerializationData.h"

#include "UObject/UnrealType.h"

namespace
{
	const FName CurrentConfigDataPropertyName("CurrentConfigData");
}

UClass* FDisplayClusterRootActorSerializer::GetSupportedClass()
{
	static const FSoftClassPath ClassPath("/Script/DisplayCluster.DisplayClusterRootActor");
	return ClassPath.ResolveClass();
}

void FDisplayClusterRootActorSerializer::MarkPropertiesAsExplicitlyUnsupported(ILevelSnapshotsModule& Module)
{
	const FProperty* CurrentConfigDataProperty = GetSupportedClass()->FindPropertyByName(CurrentConfigDataPropertyName);
	if (ensure(CurrentConfigDataProperty))
	{
		Module.AddExplicitlyUnsupportedProperties( { CurrentConfigDataProperty } );
	}
}

void FDisplayClusterRootActorSerializer::Register(ILevelSnapshotsModule& Module)
{
	MarkPropertiesAsExplicitlyUnsupported(Module);
	Module.RegisterCustomObjectSerializer(GetSupportedClass(), MakeShared<FDisplayClusterRootActorSerializer>());
}

UObject* FDisplayClusterRootActorSerializer::FindSubobject(UObject* Owner) const
{
	const FObjectProperty* ObjectProp = CastField<FObjectProperty>(GetSupportedClass()->FindPropertyByName(CurrentConfigDataPropertyName));
	checkf(ObjectProp, TEXT("Expected class %s to have object property %s"), *GetSupportedClass()->GetName(), *CurrentConfigDataPropertyName.ToString());
	check(Owner->GetClass()->IsChildOf(GetSupportedClass()));

	return ObjectProp->GetObjectPropertyValue(ObjectProp->ContainerPtrToValuePtr<void>(Owner));
}
