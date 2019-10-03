// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MeshAttributes.h"

namespace MeshAttribute
{
	const FName Vertex::Position("Position ");
}


void FMeshAttributes::Register()
{
	// Nothing to do here: Vertex positions are already registered by the FMeshDescription constructor
}

