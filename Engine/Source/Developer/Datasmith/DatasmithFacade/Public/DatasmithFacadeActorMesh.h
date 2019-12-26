// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// Datasmith facade.
#include "DatasmithFacadeActor.h"


class DATASMITHFACADE_API FDatasmithFacadeActorMesh :
	public FDatasmithFacadeActor
{
public:

	FDatasmithFacadeActorMesh(
		const TCHAR* InElementName, // Datasmith element name
		const TCHAR* InElementLabel // Datasmith element label
	);

	virtual ~FDatasmithFacadeActorMesh() {}

	// Set the static mesh of the Datasmith mesh actor.
	void SetMesh(
		const TCHAR* InMeshName // Datasmith static mesh name
	);

#ifdef SWIG_FACADE
protected:
#endif

	// Create and initialize a Datasmith mesh actor hierarchy, or
	// a Datasmith actor hierarchy when the static mesh name is not set.
	virtual TSharedPtr<IDatasmithActorElement> CreateActorHierarchy(
		TSharedRef<IDatasmithScene> IOSceneRef // Datasmith scene
	) const override;

private:

	// Datasmith static mesh name.
	FString MeshName;
};
