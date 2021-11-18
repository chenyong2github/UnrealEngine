// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Chaos/Core.h"

namespace Chaos
{
	class FPBDCollisionConstraint;

	using FPBDCollisionVisitor = TFunction<void(const FPBDCollisionConstraint*)>;
}