// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectCacheContext.h"
#include "UObject/UObjectIterator.h"
#include "Materials/MaterialInstance.h"
#include "Engine/StaticMesh.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Templates/UniquePtr.h"

const TArray<UPrimitiveComponent*>& FObjectCacheContext::GetPrimitiveComponents()
{
	if (!PrimitiveComponents.IsSet())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ComputePrimitiveComponents);

		TArray<UPrimitiveComponent*> Array;
		Array.Reserve(4096);
		for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
		{
			Array.Add(*It);
		}
		PrimitiveComponents = MoveTemp(Array);
	}
	return PrimitiveComponents.GetValue();
}

const TArray<UStaticMeshComponent*>& FObjectCacheContext::GetStaticMeshComponents()
{
	if (!StaticMeshComponents.IsSet())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ComputeStaticMeshComponents);

		TArray<UStaticMeshComponent*> Array;
		Array.Reserve(4096);
		for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
		{
			Array.Add(*It);
		}
		StaticMeshComponents = MoveTemp(Array);
	}
	return StaticMeshComponents.GetValue();
}

const TSet<UTexture*>& FObjectCacheContext::GetUsedTextures(UMaterialInterface* MaterialInterface)
{
	TSet<UTexture*>* Textures = MaterialUsedTextures.Find(MaterialInterface);
	if (Textures == nullptr)
	{
		Textures = &MaterialUsedTextures.Add(MaterialInterface);
		for (UObject* TextureObject : MaterialInterface->GetReferencedTextures())
		{
			UTexture* Texture = Cast<UTexture>(TextureObject);
			if (Texture)
			{
				Textures->Add(Texture);
			}
		}

		// Fix in CL 13480995 broke GetReferencedTextures() ability to return all referenced textures
		// so we manually gather them instead...
		// Recursion is required for MIC to resolve all TextureParameterValues in the hierarchy.
		UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(MaterialInterface);
		while (MaterialInstance)
		{
			for (const FTextureParameterValue& TextureParam : MaterialInstance->TextureParameterValues)
			{
				if (TextureParam.ParameterValue)
				{
					Textures->Add(TextureParam.ParameterValue);
				}
			}

			MaterialInstance = Cast<UMaterialInstance>(MaterialInstance->Parent);
		}
	}

	return *Textures;
}

const TArray<UMaterialInterface*>& FObjectCacheContext::GetUsedMaterials(UPrimitiveComponent* Component)
{
	TArray<UMaterialInterface*>* Materials = PrimitiveComponentToMaterial.Find(Component);
	if (Materials == nullptr)
	{
		Materials = &PrimitiveComponentToMaterial.Add(Component);
		Component->GetUsedMaterials(*Materials);
	}

	return *Materials;
}

const TArray<UStaticMeshComponent*>& FObjectCacheContext::GetStaticMeshComponents(UStaticMesh* InStaticMesh)
{
	if (!StaticMeshToComponents.IsSet())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ComputeStaticMeshToComponents);

		TMap<TObjectKey<UStaticMesh>, TArray<UStaticMeshComponent*>> TempMap;
		TempMap.Reserve(8192);
		for (UStaticMeshComponent* Component : GetStaticMeshComponents())
		{
			TempMap.FindOrAdd(Component->GetStaticMesh()).Add(Component);
		}
		StaticMeshToComponents = MoveTemp(TempMap);
	}

	static TArray<UStaticMeshComponent*> EmptyArray;
	TArray<UStaticMeshComponent*>* Array = StaticMeshToComponents.GetValue().Find(InStaticMesh);
	return Array ? *Array : EmptyArray;
}

const TSet<UMaterialInterface*>& FObjectCacheContext::GetMaterialsAffectedByTexture(UTexture* InTexture)
{
	if (!TextureToMaterials.IsSet())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ComputeMaterialsAffectedByTexture);

		TMap<TObjectKey<UTexture>, TSet<UMaterialInterface*>> TempMap;
		TempMap.Reserve(8192);
		for (TObjectIterator<UMaterialInterface> It; It; ++It)
		{
			UMaterialInterface* MaterialInterface = *It;
			for (UTexture* Texture : GetUsedTextures(MaterialInterface))
			{
				TempMap.FindOrAdd(Texture).Add(MaterialInterface);
			}
		}
		TextureToMaterials = MoveTemp(TempMap);
	}

	static TSet<UMaterialInterface*> EmptySet;
	TSet<UMaterialInterface*>* Set = TextureToMaterials.GetValue().Find(InTexture);
	return Set ? *Set : EmptySet;
}

const TSet<UPrimitiveComponent*>& FObjectCacheContext::GetPrimitivesAffectedByMaterial(UMaterialInterface* InMaterialInterface)
{
	if (!MaterialToPrimitives.IsSet())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ComputePrimitivesAffectedByMaterial);

		TMap<TObjectKey<UMaterialInterface>, TSet<UPrimitiveComponent*>> TempMap;
		for (UPrimitiveComponent* Component : GetPrimitiveComponents())
		{
			if (Component->IsRegistered() && Component->IsRenderStateCreated())
			{
				for (UMaterialInterface* MaterialInterface : GetUsedMaterials(Component))
				{
					if (MaterialInterface)
					{
						TempMap.FindOrAdd(MaterialInterface).Add(Component);
					}
				}
			}
		}

		MaterialToPrimitives = MoveTemp(TempMap);
	}

	static TSet<UPrimitiveComponent*> EmptySet;
	TSet<UPrimitiveComponent*>* Set = MaterialToPrimitives.GetValue().Find(InMaterialInterface);
	return Set ? *Set : EmptySet;
}

namespace ObjectCacheContextScopeImpl
{
	static thread_local TUniquePtr<FObjectCacheContext> Current = nullptr;
}

FObjectCacheContextScope::FObjectCacheContextScope()
{
	using namespace ObjectCacheContextScopeImpl;
	if (Current == nullptr)
	{
		Current.Reset(new FObjectCacheContext());
		bIsOwner = true;
	}
}

FObjectCacheContextScope::~FObjectCacheContextScope()
{
	using namespace ObjectCacheContextScopeImpl;
	if (bIsOwner)
	{
		Current.Reset();
	}
}

FObjectCacheContext& FObjectCacheContextScope::GetContext() 
{ 
	using namespace ObjectCacheContextScopeImpl;
	return *Current; 
}
