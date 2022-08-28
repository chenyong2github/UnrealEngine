// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class UStaticMesh;
class UMaterialInterface;

struct FGLTFExporterUtility
{
	static const UStaticMesh* GetPreviewMesh(const UMaterialInterface* Material);
	static const USkeletalMesh* GetPreviewMesh(const UAnimSequence* AnimSequence);
};
