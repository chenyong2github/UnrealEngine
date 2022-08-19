// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/NameTypes.h"

struct FBodyInstance;
class UMeshComponent;
class USkeletalMeshComponent;

/** 
 * Converts strength/damping ratio/extra damping into spring stiffness/damping.
 */
void ConvertSpringParams(
	double& OutSpring, double& OutDamping, 
	double InStrength, double InDampingRatio, double InExtraDamping);

/** 
 * Converts strength/damping ratio/extra damping into spring stiffness/damping 
 */
void ConvertSpringParams(
	FVector& OutSpring, FVector& OutDamping, 
	const FVector& InStrength, float InDampingRatio, const FVector& InExtraDamping);

/** 
 * Attempts to find a BodyInstance from the mesh. If it is a static mesh the single body instance
 * will be returned. If it is a skeletal mesh then if BoneName can be found, the body instance corresponding
 * to that bone will be returned. Otherwise it will return nullptr if the bone can't be found.
 */
FBodyInstance* GetBodyInstance(UMeshComponent* MeshComponent, const FName BoneName);

/**
 * Attempts to find the parent physical bone given a skeletal mesh and starting bone. This walks up
 * the hierarchy, ignoring non-physical bones, until either a physical bone is found, or it has reached
 * the root without finding a physical bone.
 */
FName GetPhysicalParentBone(USkeletalMeshComponent* SkeletalMeshComponent, FName BoneName);
