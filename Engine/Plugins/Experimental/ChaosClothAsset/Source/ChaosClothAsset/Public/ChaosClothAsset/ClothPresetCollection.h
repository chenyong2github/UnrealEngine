// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeometryCollection/ManagedArrayCollection.h"

namespace UE::Chaos::ClothAsset
{
	/**
	 * Tailored Cloth Preset Collection containing draping and pattern information.
	 */
	class CHAOSCLOTHASSET_API FClothPresetCollection : public FManagedArrayCollection
	{
	public:
		typedef FManagedArrayCollection Super;

		FClothPresetCollection();
		FClothPresetCollection(FClothPresetCollection&) = delete;
		FClothPresetCollection& operator=(const FClothPresetCollection&) = delete;
		FClothPresetCollection(FClothPresetCollection&&) = default;
		FClothPresetCollection& operator=(FClothPresetCollection&&) = default;

		using Super::Serialize;
		void Serialize(FArchive& Ar);

		// Attribute groups, predefined data member of the FClothLod object.
		static const FName PropertyGroup;  // Fabric properties

		// PropertyGroup
		TManagedArray<FString> Name;
		TManagedArray<FVector3f> LowValue;
		TManagedArray<FVector3f> HighValue;
		TManagedArray<FString> StringValue;
		TManagedArray<bool> Enable;
		TManagedArray<bool> Animatable;

	protected:
		void Construct();

		// TODO: Add predefined sim properties:
		//	int32 MassMode;
		//	float MassValue;
		//	FVector2f EdgeStiffness;
		//	FVector2f BendingStiffness;
		//	FVector2f AreaStiffness;
		//	bool bUseGeodesicTether;
		//	FVector2f TetherStiffness;
		//	FVector2f TetherScale;
		//	FVector2f MaxDistance;
		//	FVector2f BackstopDistance;
		//	FVector2f BackstopRadius;
		//	FVector2f CollisionThickness;
		//	FVector2f FrictionCoefficient;
		//	bool bUseCCD;
		//	float DampingCoefficient;
		//	float LocalDampingCoefficient;
		//	FVector3f Drag;
		//	FVector3f Lift;
		//	float GravityScale;
		//	bool bGravityOverride;
		//	FVector3f Gravity;
		//	FVector2f AnimDriveStiffness;
		//	FVector2f AnimDriveDamping;
		//	float LinearVelocityScale;
		//	float AngularVelocityScale;
		//	float FictitiousAngularScale;
	};
}  // End namespace UE::Chaos::ClothAsset
