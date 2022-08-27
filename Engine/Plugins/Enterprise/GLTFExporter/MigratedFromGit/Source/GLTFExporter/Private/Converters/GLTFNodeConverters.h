// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"

typedef TGLTFConverter<FGLTFJsonNodeIndex, const AActor*> IGLTFActorConverter;
typedef TGLTFConverter<FGLTFJsonNodeIndex, const USceneComponent*> IGLTFComponentConverter;
typedef TGLTFConverter<FGLTFJsonNodeIndex, const USceneComponent*, FName> IGLTFComponentSocketConverter;
typedef TGLTFConverter<FGLTFJsonNodeIndex, FGLTFJsonNodeIndex, const UStaticMesh*, FName> IGLTFStaticSocketConverter;
typedef TGLTFConverter<FGLTFJsonNodeIndex, FGLTFJsonNodeIndex, const USkeletalMesh*, FName> IGLTFSkeletalSocketConverter;
typedef TGLTFConverter<FGLTFJsonNodeIndex, FGLTFJsonNodeIndex, const USkeletalMesh*, int32> IGLTFSkeletalBoneConverter;

class FGLTFActorConverter final : public FGLTFBuilderContext, public IGLTFActorConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonNodeIndex Convert(const AActor* Actor) override;
};

class FGLTFComponentConverter final : public FGLTFBuilderContext, public IGLTFComponentConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonNodeIndex Convert(const USceneComponent* SceneComponent) override;
};

class FGLTFComponentSocketConverter final : public FGLTFBuilderContext, public IGLTFComponentSocketConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonNodeIndex Convert(const USceneComponent* SceneComponent, FName SocketName) override;
};

class FGLTFStaticSocketConverter final : public FGLTFBuilderContext, public IGLTFStaticSocketConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonNodeIndex Convert(FGLTFJsonNodeIndex RootNode, const UStaticMesh* StaticMesh, FName SocketName) override;
};

class FGLTFSkeletalSocketConverter final : public FGLTFBuilderContext, public IGLTFSkeletalSocketConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonNodeIndex Convert(FGLTFJsonNodeIndex RootNode, const USkeletalMesh* SkeletalMesh, FName SocketName) override;
};

class FGLTFSkeletalBoneConverter final : public FGLTFBuilderContext, public IGLTFSkeletalBoneConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonNodeIndex Convert(FGLTFJsonNodeIndex RootNode, const USkeletalMesh* SkeletalMesh, int32 BoneIndex) override;
};
