// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithFacadeActorMesh.h"


FDatasmithFacadeActorMesh::FDatasmithFacadeActorMesh(
	const TCHAR* InElementName,
	const TCHAR* InElementLabel
) :
	FDatasmithFacadeActor(InElementName, InElementLabel)
{
}

void FDatasmithFacadeActorMesh::SetMesh(
	const TCHAR* InMeshName
)
{
	MeshName = InMeshName;

	// Prevent the Datasmith mesh actor from being removed by optimization.
	KeepActor();
}

TSharedPtr<IDatasmithActorElement> FDatasmithFacadeActorMesh::CreateActorHierarchy(
	TSharedRef<IDatasmithScene> IOSceneRef
) const
{
	if (MeshName.IsEmpty())
	{
		// Create and initialize a Datasmith actor hierarchy.
		return FDatasmithFacadeActor::CreateActorHierarchy(IOSceneRef);
	}
	else
	{
		// Create a Datasmith mesh actor element.
		TSharedPtr<IDatasmithMeshActorElement> MeshActorPtr = FDatasmithSceneFactory::CreateMeshActor(*ElementName);

		// Set the Datasmith mesh actor base properties.
		SetActorProperties(IOSceneRef, MeshActorPtr);

		// Set the static mesh used by the Datasmith mesh actor.
		MeshActorPtr->SetStaticMeshPathName(*MeshName);

		// Add the hierarchy of children to the Datasmith actor.
		AddActorChildren(IOSceneRef, MeshActorPtr);

		return MeshActorPtr;
	}
}

