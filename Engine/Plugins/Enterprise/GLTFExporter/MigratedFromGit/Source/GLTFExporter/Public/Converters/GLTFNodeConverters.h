// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"

typedef TGLTFConverter<FGLTFJsonNode*, const AActor*> IGLTFActorConverter;
typedef TGLTFConverter<FGLTFJsonNode*, const USceneComponent*> IGLTFComponentConverter;
typedef TGLTFConverter<FGLTFJsonNode*, const USceneComponent*, FName> IGLTFComponentSocketConverter;
typedef TGLTFConverter<FGLTFJsonNode*, FGLTFJsonNode*, const UStaticMesh*, FName> IGLTFStaticSocketConverter;
typedef TGLTFConverter<FGLTFJsonNode*, FGLTFJsonNode*, const USkeletalMesh*, FName> IGLTFSkeletalSocketConverter;
typedef TGLTFConverter<FGLTFJsonNode*, FGLTFJsonNode*, const USkeletalMesh*, int32> IGLTFSkeletalBoneConverter;

class GLTFEXPORTER_API FGLTFActorConverter final : public FGLTFBuilderContext, public IGLTFActorConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonNode* Convert(const AActor* Actor) override;
};

class GLTFEXPORTER_API FGLTFComponentConverter final : public FGLTFBuilderContext, public IGLTFComponentConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonNode* Convert(const USceneComponent* SceneComponent) override;
};

class GLTFEXPORTER_API FGLTFComponentSocketConverter final : public FGLTFBuilderContext, public IGLTFComponentSocketConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonNode* Convert(const USceneComponent* SceneComponent, FName SocketName) override;
};

class GLTFEXPORTER_API FGLTFStaticSocketConverter final : public FGLTFBuilderContext, public IGLTFStaticSocketConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonNode* Convert(FGLTFJsonNode* RootNode, const UStaticMesh* StaticMesh, FName SocketName) override;
};

class GLTFEXPORTER_API FGLTFSkeletalSocketConverter final : public FGLTFBuilderContext, public IGLTFSkeletalSocketConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonNode* Convert(FGLTFJsonNode* RootNode, const USkeletalMesh* SkeletalMesh, FName SocketName) override;
};

class GLTFEXPORTER_API FGLTFSkeletalBoneConverter final : public FGLTFBuilderContext, public IGLTFSkeletalBoneConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonNode* Convert(FGLTFJsonNode* RootNode, const USkeletalMesh* SkeletalMesh, int32 BoneIndex) override;
};
