// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class UStaticMesh;
class UMaterialInterface;
class UAnimSequence;

struct FGLTFExporterUtility
{
	static const UStaticMesh* GetPreviewMesh(const UMaterialInterface* Material);
	static const USkeletalMesh* GetPreviewMesh(const UAnimSequence* AnimSequence);

	static TArray<UWorld*> GetAssociatedWorlds(const UObject* Object);
};
