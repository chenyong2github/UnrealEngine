// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShaderCompilerShared.h"

#include "ComponentRecreateRenderStateContext.h"
#include "ObjectCacheContext.h"
#if WITH_EDITOR
#include "Rendering/StaticLightingSystemInterface.h"
#endif

void PropagateGlobalShadersToAllPrimitives()
{
	// Re-register everything to work around FShader lifetime issues - it currently lives and dies with the
	// shadermap it is stored in, while cached MDCs can reference its memory. Re-registering will
	// re-create the cache.
	TRACE_CPUPROFILER_EVENT_SCOPE(PropagateGlobalShadersToAllPrimitives);

	FObjectCacheContextScope ObjectCacheScope;
	TSet<FSceneInterface*> ScenesToUpdate;
	TIndirectArray<FComponentRecreateRenderStateContext> ComponentContexts;
	for (UPrimitiveComponent* PrimitiveComponent : ObjectCacheScope.GetContext().GetPrimitiveComponents())
	{
		if (PrimitiveComponent->IsRenderStateCreated())
		{
			ComponentContexts.Add(new FComponentRecreateRenderStateContext(PrimitiveComponent, &ScenesToUpdate));
#if WITH_EDITOR
			if (PrimitiveComponent->HasValidSettingsForStaticLighting(false))
			{
				FStaticLightingSystemInterface::OnPrimitiveComponentUnregistered.Broadcast(PrimitiveComponent);
				FStaticLightingSystemInterface::OnPrimitiveComponentRegistered.Broadcast(PrimitiveComponent);
			}
#endif
		}
	}

	UpdateAllPrimitiveSceneInfosForScenes(ScenesToUpdate);
	ComponentContexts.Empty();
	UpdateAllPrimitiveSceneInfosForScenes(ScenesToUpdate);
}
