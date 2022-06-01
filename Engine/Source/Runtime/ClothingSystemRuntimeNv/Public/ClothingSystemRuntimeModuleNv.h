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

};
