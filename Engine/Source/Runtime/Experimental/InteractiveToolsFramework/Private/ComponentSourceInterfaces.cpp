// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ComponentSourceInterfaces.h"

#include "Containers/Array.h"

namespace
{
	TArray<MeshDescriptionSourceBuilder> Builders;
}

void
AddMeshDescriptionSourceBuilder( MeshDescriptionSourceBuilder Builder )
{
	Builders.Push(MoveTemp(Builder));
}

TUniquePtr<IMeshDescriptionSource>
MakeMeshDescriptionSource(UActorComponent* Component)
{
	for ( const auto& Builder : Builders )
	{
		auto Source = Builder( Component );
		if ( Source )
		{
			return Source;
		}
	}
	return {};
}
