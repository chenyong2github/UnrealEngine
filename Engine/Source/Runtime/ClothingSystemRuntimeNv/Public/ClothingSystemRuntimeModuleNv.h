// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "ClothingSimulationFactory.h"

namespace nv
{
	namespace cloth
	{
		class Factory;
		class ClothMeshQuadifier;
	}
}

class FClothingSystemRuntimeModuleNv : public IModuleInterface, public IClothingSimulationFactoryClassProvider
{

public:

	FClothingSystemRuntimeModuleNv();

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	nv::cloth::Factory* GetSoftwareFactory();
	nv::cloth::ClothMeshQuadifier* GetMeshQuadifier();

	// IClothingSimulationFactoryClassProvider Interface
	virtual TSubclassOf<UClothingSimulationFactory> GetClothingSimulationFactoryClass() const override;
	//////////////////////////////////////////////////////////////////////////

private:

#if WITH_NVCLOTH
	nv::cloth::Factory* ClothFactory;
	nv::cloth::ClothMeshQuadifier* Quadifier;

	void DelayLoadNvCloth();
	void ShutdownNvClothLibs();

#if PLATFORM_WINDOWS || PLATFORM_MAC
	void* NvClothHandle;
#endif

#if PLATFORM_WINDOWS
	void DelayLoadNvCloth_Windows();
	void ShutdownNvCloth_Windows();
#endif

#if PLATFORM_MAC
	void DelayLoadNvCloth_Mac();
	void ShutdownNvCloth_Mac();
#endif

#endif
};
