// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PhysicsInitialization.h"
#include "PhysXPublicCore.h"
#include "PhysicsPublicCore.h"
#include "PhysXSupportCore.h"
#include "Misc/CommandLine.h"
#include "IPhysXCookingModule.h"
#include "IPhysXCooking.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"

#ifndef APEX_STATICALLY_LINKED
#define APEX_STATICALLY_LINKED	0
#endif


// CVars
TAutoConsoleVariable<float> CVarToleranceScaleLength(
	TEXT("p.ToleranceScale_Length"),
	100.f,
	TEXT("The approximate size of objects in the simulation. Default: 100"),
	ECVF_ReadOnly);

TAutoConsoleVariable<float> CVarToleranceScaleSpeed(
	TEXT("p.ToleranceScale_Speed"),
	1000.f,
	TEXT("The typical magnitude of velocities of objects in simulation. Default: 1000"),
	ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarUseUnifiedHeightfield(
	TEXT("p.bUseUnifiedHeightfield"),
	1,
	TEXT("Whether to use the PhysX unified heightfield. This feature of PhysX makes landscape collision consistent with triangle meshes but the thickness parameter is not supported for unified heightfields. 1 enables and 0 disables. Default: 1"),
	ECVF_ReadOnly);

bool InitGamePhysCore()
{
#if INCLUDE_CHAOS
	// If we're running with Chaos enabled, load its module
	FModuleManager::Get().LoadModule("Chaos");
	FModuleManager::Get().LoadModule("ChaosSolvers");
#if WITH_ENGINE
	FModuleManager::Get().LoadModule("ChaosSolverEngine");
#endif
#endif

#if WITH_PHYSX
	// Do nothing if SDK already exists
	if (GPhysXFoundation != nullptr)
	{
		return true;
	}

	// Make sure 
	if (!PhysDLLHelper::LoadPhysXModules(/*bLoadCookingModule=*/ false))
	{
		// This is fatal. We were not able to successfully load the physics modules
		return false;
	}

	// Create Foundation
	GPhysXAllocator = new FPhysXAllocator();
	FPhysXErrorCallback* ErrorCallback = new FPhysXErrorCallback();

	GPhysXFoundation = PxCreateFoundation(PX_FOUNDATION_VERSION, *GPhysXAllocator, *ErrorCallback);
	check(GPhysXFoundation);

#if PHYSX_MEMORY_STATS
	// Want names of PhysX allocations
	GPhysXFoundation->setReportAllocationNames(true);
#endif

	// Create profile manager
	GPhysXVisualDebugger = PxCreatePvd(*GPhysXFoundation);
	check(GPhysXVisualDebugger);

	// Create Physics
	PxTolerancesScale PScale;
	PScale.length = CVarToleranceScaleLength.GetValueOnGameThread();
	PScale.speed = CVarToleranceScaleSpeed.GetValueOnGameThread();

	GPhysXSDK = PxCreatePhysics(PX_PHYSICS_VERSION, *GPhysXFoundation, PScale, false, GPhysXVisualDebugger);
	check(GPhysXSDK);

	FPhysxSharedData::Initialize();

	// Init Extensions
	PxInitExtensions(*GPhysXSDK, GPhysXVisualDebugger);

	if (CVarUseUnifiedHeightfield.GetValueOnGameThread())
	{
		//Turn on PhysX 3.3 unified height field collision detection. 
		//This approach shares the collision detection code between meshes and height fields such that height fields behave identically to the equivalent terrain created as a mesh. 
		//This approach facilitates mixing the use of height fields and meshes in the application with no tangible difference in collision behavior between the two approaches except that 
		//heightfield thickness is not supported for unified heightfields.
		PxRegisterUnifiedHeightFields(*GPhysXSDK);
	}
	else
	{
		PxRegisterHeightFields(*GPhysXSDK);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("PVD")))
	{
		PvdConnect(TEXT("localhost"), true);
	}

	// Create Cooking
	PxCooking* PhysXCooking = nullptr;
	if (IPhysXCookingModule* Module = GetPhysXCookingModule())
	{
		PhysXCooking = Module->GetPhysXCooking()->GetCooking();
	}

#if WITH_APEX
	check(PhysXCooking);	//APEX requires cooking

	// Build the descriptor for the APEX SDK
	apex::ApexSDKDesc ApexDesc;
	ApexDesc.foundation = GPhysXFoundation;	// Pointer to the PxFoundation
	ApexDesc.physXSDK = GPhysXSDK;	// Pointer to the PhysXSDK
	ApexDesc.cooking = PhysXCooking;	// Pointer to the cooking library
	ApexDesc.renderResourceManager = &GApexNullRenderResourceManager;	// We will not be using the APEX rendering API, so just use a dummy render resource manager
	ApexDesc.resourceCallback = &GApexResourceCallback;	// The resource callback is how APEX asks the application to find assets when it needs them

#if PLATFORM_MAC
	FString DylibFolder = FPaths::EngineDir() / TEXT("Binaries/ThirdParty/PhysX3/");
	ANSICHAR* DLLLoadPath = (ANSICHAR*)FMemory::Malloc(DylibFolder.Len() + 1);
	FCStringAnsi::Strcpy(DLLLoadPath, DylibFolder.Len() + 1, TCHAR_TO_UTF8(*DylibFolder));
	ApexDesc.dllLoadPath = DLLLoadPath;
#endif

	// Create the APEX SDK
	apex::ApexCreateError ErrorCode;
	GApexSDK = apex::CreateApexSDK(ApexDesc, &ErrorCode);
	check(ErrorCode == APEX_CE_NO_ERROR);
	check(GApexSDK);

#if PLATFORM_MAC
	FMemory::Free(DLLLoadPath);
#endif

#if UE_BUILD_SHIPPING
	GApexSDK->setEnableApexStats(false);
#endif

#if APEX_STATICALLY_LINKED

#if WITH_APEX_CLOTHING
	instantiateModuleClothing();
#endif

#if WITH_APEX_LEGACY
	instantiateModuleLegacy();
#endif
#endif

	// 1 legacy module for all in APEX 1.3
	// Load the only 1 legacy module
#if WITH_APEX_LEGACY
	GApexModuleLegacy = GApexSDK->createModule("Legacy");
	check(GApexModuleLegacy);
#endif // WITH_APEX_LEGACY

#if WITH_APEX_CLOTHING
	// Load APEX Clothing module
	GApexModuleClothing = static_cast<apex::ModuleClothing*>(GApexSDK->createModule("Clothing"));
	check(GApexModuleClothing);
	// Set Clothing module parameters
	NvParameterized::Interface* ModuleParams = GApexModuleClothing->getDefaultModuleDesc();

	// Can be tuned for switching between more memory and more spikes.
	NvParameterized::setParamU32(*ModuleParams, "maxUnusedPhysXResources", 5);

	// If true, let fetch results tasks run longer than the fetchResults call. 
	// Setting to true could not ensure same finish timing with Physx simulation phase
	NvParameterized::setParamBool(*ModuleParams, "asyncFetchResults", false);

	// ModuleParams contains the default module descriptor, which may be modified here before calling the module init function
	GApexModuleClothing->init(*ModuleParams);
#endif	//WITH_APEX_CLOTHING

#endif // #if WITH_APEX

#endif // WITH_PHYSX

	return true;
}

void TermGamePhysCore()
{
#if WITH_PHYSX

	FPhysxSharedData::Terminate();

	// Do nothing if they were never initialized
	if (GPhysXFoundation == nullptr)
	{
		return;
	}

#if WITH_APEX
#if WITH_APEX_LEGACY
	if (GApexModuleLegacy != nullptr)
	{
		GApexModuleLegacy->release();
		GApexModuleLegacy = nullptr;
	}
#endif // WITH_APEX_LEGACY
	if (GApexSDK != nullptr)
	{
		GApexSDK->release();
		GApexSDK = nullptr;
	}
#endif	// #if WITH_APEX

	//Remove all scenes still registered
	if (GPhysXSDK != nullptr)
	{
		if (int32 NumScenes = GPhysXSDK->getNbScenes())
		{
			TArray<PxScene*> PScenes;
			PScenes.AddUninitialized(NumScenes);
			GPhysXSDK->getScenes(PScenes.GetData(), sizeof(PxScene*)* NumScenes);

			for (PxScene* PScene : PScenes)
			{
				if (PScene)
				{
					PScene->release();
				}
			}
		}
	}

	// Unload dependent modules
	if(FModuleManager::Get().GetModule("PhysXVehicles"))
	{
		// Vehicles is actually in a plugin, but in order to shut down the foundation
		// below - all dependents must release which requires us to shut this module
		// down slightly early.
		FModuleManager::Get().UnloadModule("PhysXVehicles", true);
	}

	if (IPhysXCookingModule* PhysXCookingModule = GetPhysXCookingModule( /*bforceLoad=*/false))
	{
		PhysXCookingModule->Terminate();
	}

	if (GPhysXSDK != nullptr)
	{
		PxCloseExtensions();
	}

	if (GPhysXSDK != nullptr)
	{
		GPhysXSDK->release();
		GPhysXSDK = nullptr;
	}

	if(GPhysXVisualDebugger)
	{
		GPhysXVisualDebugger->release();
		GPhysXVisualDebugger = nullptr;
	}

	GPhysXFoundation->release();
	GPhysXFoundation = nullptr;

	// @todo delete FPhysXAllocator
	// @todo delete FPhysXOutputStream

	PhysDLLHelper::UnloadPhysXModules();
#endif // WITH_PHYSX
}