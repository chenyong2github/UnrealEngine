// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "HitProxies.h"
#include "RendererInterface.h"
#include "DepthRendering.h"

class FPrimitiveSceneInfo;
class FPrimitiveSceneProxy;
class FScene;
class FStaticMeshBatch;
class FViewInfo;

struct FPrimitiveViewRelevance;

enum class EVelocityPass : uint32
{
	// Renders a separate velocity pass for opaques.
	Opaque = 0,

	// Renders a separate velocity / depth pass for translucency AFTER the translucent pass.
	Translucent,

	Count
};

EMeshPass::Type GetMeshPassFromVelocityPass(EVelocityPass VelocityPass);

// Group Velocity Rendering accessors, types, etc.
struct FVelocityRendering
{
	static EPixelFormat GetFormat();
	static FPooledRenderTargetDesc GetRenderTargetDesc();

	/** Returns true if the separate velocity pass is enabled. */
	static bool IsSeparateVelocityPassSupported();

	/** Returns true if the velocity can be output in the BasePass. */
	static bool BasePassCanOutputVelocity(EShaderPlatform ShaderPlatform);

	/** Returns true if the velocity can be output in the BasePass. Only valid for the current platform. */
	static bool BasePassCanOutputVelocity(ERHIFeatureLevel::Type FeatureLevel);

	/** Returns true if a separate velocity pass is required (i.e. not rendered by the base pass) given the provided vertex factory settings. */
	static bool IsSeparateVelocityPassRequiredByVertexFactory(EShaderPlatform ShaderPlatform, bool bVertexFactoryUsesStaticLighting);
};

/**
 * Base velocity mesh pass processor class. Used for both opaque and translucent velocity passes.
 */
class FVelocityMeshProcessor : public FMeshPassProcessor
{
public:
	FVelocityMeshProcessor(
		const FScene* Scene,
		const FSceneView* InViewIfDynamicMeshCommand,
		const FMeshPassProcessorRenderState& InPassDrawRenderState,
		FMeshPassDrawListContext* InDrawListContext);

	FMeshPassProcessorRenderState PassDrawRenderState;

	/* Checks whether the primitive should emit velocity for the current view by comparing screen space size against a threshold. */
	static bool PrimitiveHasVelocityForView(const FViewInfo& View, const FPrimitiveSceneProxy* PrimitiveSceneProxy);

protected:
	void Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		int32 StaticMeshId,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode);
};

/**
 * Velocity pass processor for rendering opaques into a separate velocity pass (i.e. separate from the base pass).
 */
class FOpaqueVelocityMeshProcessor : public FVelocityMeshProcessor
{
public:
	FOpaqueVelocityMeshProcessor(
		const FScene* Scene,
		const FSceneView* InViewIfDynamicMeshCommand,
		const FMeshPassProcessorRenderState& InPassDrawRenderState,
		FMeshPassDrawListContext* InDrawListContext);

	/** Returns true if the object is capable of having velocity for any frame. */
	static bool PrimitiveCanHaveVelocity(EShaderPlatform ShaderPlatform, const FPrimitiveSceneProxy* PrimitiveSceneProxy);

	/** Returns true if the primitive has velocity for the current frame. */
	static bool PrimitiveHasVelocityForFrame(const FPrimitiveSceneProxy* PrimitiveSceneProxy);

private:
	void AddMeshBatch(
		const FMeshBatch& RESTRICT MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		int32 StaticMeshId = -1) override final;
};

/**
 * Velocity pass processor for rendering translucent object velocity and depth. This pass is rendered AFTER the
 * translucent pass so that depth can safely be written.
 */
class FTranslucentVelocityMeshProcessor : public FVelocityMeshProcessor
{
public:
	FTranslucentVelocityMeshProcessor(
		const FScene* Scene,
		const FSceneView* InViewIfDynamicMeshCommand,
		const FMeshPassProcessorRenderState& InPassDrawRenderState,
		FMeshPassDrawListContext* InDrawListContext);

	/** Returns true if the object is capable of having velocity for any frame. */
	static bool PrimitiveCanHaveVelocity(EShaderPlatform ShaderPlatform, const FPrimitiveSceneProxy* PrimitiveSceneProxy);

	/** Returns true if the primitive has velocity for the current frame. */
	static bool PrimitiveHasVelocityForFrame(const FPrimitiveSceneProxy* PrimitiveSceneProxy);

private:
	void AddMeshBatch(
		const FMeshBatch& RESTRICT MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		int32 StaticMeshId = -1) override final;
};