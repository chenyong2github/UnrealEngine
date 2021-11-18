// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterRootActorSerializer.h"

#include "ILevelSnapshotsModule.h"
#include "Params/ObjectSnapshotSerializationData.h"

#include "UObject/UnrealType.h"

namespace UE::LevelSnapshots::nDisplay::Private::Internal
{
	const FName CurrentConfigDataPropertyName("CurrentConfigData");
}

UClass* UE::LevelSnapshots::nDisplay::Private::FDisplayClusterRootActorSerializer::GetSupportedClass()
{
	static const FSoftClassPath ClassPath("/Script/DisplayCluster.DisplayClusterRootActor");
	return ClassPath.ResolveClass();
}

void UE::LevelSnapshots::nDisplay::Private::FDisplayClusterRootActorSerializer::MarkPropertiesAsExplicitlyUnsupported(ILevelSnapshotsModule& Module)
{
	const FProperty* CurrentConfigDataProperty = GetSupportedClass()->FindPropertyByName(Internal::CurrentConfigDataPropertyName);
	if (ensure(CurrentConfigDataProperty))
	{
		Module.AddExplicitlyUnsupportedProperties( { CurrentConfigDataProperty } );
	}
}

void UE::LevelSnapshots::nDisplay::Private::FDisplayClusterRootActorSerializer::Register(ILevelSnapshotsModule& Module)
{
	MarkPropertiesAsExplicitlyUnsupported(Module);
	Module.RegisterCustomObjectSerializer(GetSupportedClass(), MakeShared<FDisplayClusterRootActorSerializer>());
}

UObject* UE::LevelSnapshots::nDisplay::Private::FDisplayClusterRootActorSerializer::FindSubobject(UObject* Owner) const
{
	const FObjectProperty* ObjectProp = CastField<FObjectProperty>(GetSupportedClass()->FindPropertyByName(Internal::CurrentConfigDataPropertyName));
	checkf(ObjectProp, TEXT("Expected class %s to have object property %s"), *GetSupportedClass()->GetName(), *Internal::CurrentConfigDataPropertyName.ToString());
	check(Owner->GetClass()->IsChildOf(GetSupportedClass()));

	return ObjectProp->GetObjectPropertyValue(ObjectProp->ContainerPtrToValuePtr<void>(Owner));
}
