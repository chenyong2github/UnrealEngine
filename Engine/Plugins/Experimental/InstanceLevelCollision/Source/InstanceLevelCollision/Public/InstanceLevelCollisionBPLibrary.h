// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
/*#include "CoreMinimal.h"
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"*/
#include "LevelInstance/LevelInstanceActor.h"
#include "Engine/StaticMeshActor.h"
#include "Materials/MaterialInterface.h"
#include "AddPatchTool.h"
#include "InstanceLevelCollisionBPLibrary.generated.h"

/* 
*	Function library class.
*	Each function in it is expected to be static and represents blueprint node that can be called in any blueprint.
*
*	When declaring function you can define metadata for the node. Key function specifiers will be BlueprintPure and BlueprintCallable.
*	BlueprintPure - means the function does not affect the owning object in any way and thus creates a node without Exec pins.
*	BlueprintCallable - makes a function which can be executed in Blueprints - Thus it has Exec pins.
*	DisplayName - full name of the node, shown when you mouse over the node and in the blueprint drop down menu.
*				Its lets you name the node using characters not allowed in C++ function names.
*	CompactNodeTitle - the word(s) that appear on the node.
*	Keywords -	the list of keywords that helps you to find node when you search for it using Blueprint drop-down menu. 
*				Good example is "Print String" node which you can find also by using keyword "log".
*	Category -	the category your node will be under in the Blueprint drop-down menu.
*
*	For more info on custom blueprint nodes visit documentation:
*	https://wiki.unrealengine.com/Custom_Blueprint_Node_Creation
*/

UENUM(BlueprintType)
enum class ECollisionMaxSlice : uint8
{

	XYBound,

	MinZ,

	Custom
};

UCLASS()
class UInstanceLevelCollisionBPLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

		class IMeshReduction* MeshReduction;

		UFUNCTION(BlueprintCallable, meta = (DisplayName = "GenerateCollision", Keywords = "GenerateCollision"), Category = "MegaAssemblyLib")
		static void GenerateCollision(ALevelInstance* LevelInstanceBP, TArray<AStaticMeshActor*> MeshActor, float ZOffset, ECollisionMaxSlice CollisionType, int VoxelDensity = 64, float TargetPercentage = 50.0, float Winding = 0.5);
};
