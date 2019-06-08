// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/ChaosArchive.h"
#include "Chaos/Serializable.h"
#include "Chaos/ImplicitObject.h"

namespace Chaos
{

void FChaosArchive::SerializeLegacy(TUniquePtr<TImplicitObject<float, 3>>& Obj)
{
	TImplicitObject<float, 3>::SerializeLegacyHelper(InnerArchive, Obj);
}
}