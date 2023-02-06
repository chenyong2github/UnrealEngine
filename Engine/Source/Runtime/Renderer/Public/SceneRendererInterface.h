// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class FSceneUniformBuffer;

/**
 * Public interface into FSceneRenderer. Used as the scope for scene rendering functions.
 */
class ISceneRenderer
{
public:
	virtual const FSceneUniformBuffer& GetSceneUniforms() const = 0;
	virtual FSceneUniformBuffer& GetSceneUniforms() = 0;
};
