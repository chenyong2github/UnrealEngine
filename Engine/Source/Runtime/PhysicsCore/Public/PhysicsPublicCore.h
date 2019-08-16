// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_PHYSX
namespace physx
{
	class PxPhysics;
}

/** Pointer to PhysX SDK object */
extern PHYSICSCORE_API physx::PxPhysics* GPhysXSDK;
/** Pointer to PhysX allocator */
extern PHYSICSCORE_API class FPhysXAllocator* GPhysXAllocator;

extern PHYSICSCORE_API class IPhysXCookingModule* GetPhysXCookingModule(bool bForceLoad = true);


#if WITH_APEX

namespace nvidia
{
	namespace apex
	{
		class ApexSDK;
		class Module;
		class ModuleClothing;
	}
}

using namespace nvidia;

/** Pointer to APEX SDK object */
extern PHYSICSCORE_API apex::ApexSDK* GApexSDK;
/** Pointer to APEX legacy module object */
extern PHYSICSCORE_API apex::Module* GApexModuleLegacy;
#if WITH_APEX_CLOTHING
/** Pointer to APEX Clothing module object */
extern PHYSICSCORE_API apex::ModuleClothing* GApexModuleClothing;
#endif //WITH_APEX_CLOTHING

#endif // #if WITH_APEX
#endif