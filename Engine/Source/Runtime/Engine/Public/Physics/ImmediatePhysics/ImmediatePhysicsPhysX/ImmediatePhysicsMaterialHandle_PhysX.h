// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX

class UPhysicalMaterial;

namespace ImmediatePhysics_PhysX
{
	struct ENGINE_API FMaterialHandle
	{

	public:

		FMaterial& Get();
		void SetStaticFriction(float InFriction);
		void SetDynamicFriction(float InFriction);
		void SetRestitution(float InRestitution);
		void SetFrictionCombineMode(EFrictionCombineMode InCombineMode);
		void SetRestitutionCombineMode(EFrictionCombineMode InCombineMode);
		void SetFromPhysicalMaterial(UPhysicalMaterial* InMaterial);

	private:

		FSimulation & OwningSimulation;
		int32 DataIndex;

		friend struct FSimulation;

		FMaterialHandle(FSimulation& InOwningSimulation, int32 InDataIndex)
			: OwningSimulation(InOwningSimulation)
			, DataIndex(InDataIndex)
		{}

		~FMaterialHandle() 
		{}

		// Removed copy/assign
		FMaterialHandle(const FMaterialHandle& Other) = delete;
		FMaterialHandle(FMaterialHandle&& Other) = delete;
		FMaterialHandle& operator=(const FMaterialHandle& Other) = delete;
		FMaterialHandle& operator=(FMaterialHandle&& Other) = delete;
	};
}

#endif // WITH_PHYSX