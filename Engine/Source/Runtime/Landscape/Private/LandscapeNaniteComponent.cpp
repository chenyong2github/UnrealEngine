// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapeNaniteComponent.h"
#include "LandscapeEdit.h"
#include "LandscapeRender.h"
#include "NaniteSceneProxy.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSourceData.h"
#if WITH_EDITOR
#include "StaticMeshAttributes.h"
#include "StaticMeshDescription.h"
#include "StaticMeshOperations.h"
#include "MeshUtilitiesCommon.h"
#include "OverlappingCorners.h"
#include "MeshBuild.h"
#include "StaticMeshBuilder.h"
#include "NaniteBuilder.h"
#include "Rendering/NaniteResources.h"
#endif

ULandscapeNaniteComponent::ULandscapeNaniteComponent(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
}

#if WITH_EDITOR

void ULandscapeNaniteComponent::InitializeForLandscape(ALandscapeProxy* Landscape, const FGuid& NewProxyContentId)
{
	UStaticMesh* NaniteStaticMesh = NewObject<UStaticMesh>(this /* Outer */);
	FMeshDescription* MeshDescription = nullptr;

	// Mesh
	{
		FStaticMeshSourceModel& SrcModel = NaniteStaticMesh->AddSourceModel();
		
		// Don't allow the engine to recalculate normals
		SrcModel.BuildSettings.bRecomputeNormals = false;
		SrcModel.BuildSettings.bRecomputeTangents = false;
		SrcModel.BuildSettings.bRemoveDegenerates = false;
		SrcModel.BuildSettings.bUseHighPrecisionTangentBasis = false;
		SrcModel.BuildSettings.bUseFullPrecisionUVs = false;

		FMeshNaniteSettings& NaniteSettings = NaniteStaticMesh->NaniteSettings;
		NaniteSettings.bEnabled = true;
		NaniteSettings.FallbackPercentTriangles = 0.0f; // Keep effectively no fallback mesh triangles
		NaniteSettings.FallbackRelativeError = 1.0f;

		const int32 LOD = 0; // Always uses high quality LOD

		MeshDescription = NaniteStaticMesh->CreateMeshDescription(LOD);
		{
			TArray<UMaterialInterface*, TInlineAllocator<4>> InputMaterials;
			TInlineComponentArray<ULandscapeComponent*> InputComponents;

			for (ULandscapeComponent* Component : Landscape->LandscapeComponents)
			{
				if (Component)
				{
					InputMaterials.Add(Component->GetLandscapeMaterial(LOD));
					InputComponents.Add(Component);
				}
			}

			if (InputComponents.Num() == 0)
			{
				// TODO: Error
				return;
			}

			FBoxSphereBounds UnusedBounds;
			if (!Landscape->ExportToRawMesh(
				MakeArrayView(InputComponents.GetData(), InputComponents.Num()),
				LOD,
				*MeshDescription,
				UnusedBounds,
				true /* Ignore Bounds */
			))
			{
				// TODO: Error
				return;
			}

			for (UMaterialInterface* Material : InputMaterials)
			{
				if (Material == nullptr)
				{
					Material = UMaterial::GetDefaultMaterial(MD_Surface);
				}

				NaniteStaticMesh->GetStaticMaterials().Add(FStaticMaterial(Material));
			}
		}

		NaniteStaticMesh->CommitMeshDescription(0);
		NaniteStaticMesh->ImportVersion = EImportStaticMeshVersion::LastVersion;
	}

	// Disable collisions
	if (UBodySetup* BodySetup = NaniteStaticMesh->GetBodySetup())
	{
		BodySetup->DefaultInstance.SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		BodySetup->CollisionTraceFlag = CTF_UseSimpleAsComplex;
	}

	SetStaticMesh(NaniteStaticMesh);
	UStaticMesh::BatchBuild({ NaniteStaticMesh });

	ProxyContentId = NewProxyContentId;
}

#endif