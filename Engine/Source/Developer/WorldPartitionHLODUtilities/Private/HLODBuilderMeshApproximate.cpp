// Copyright Epic Games, Inc. All Rights Reserved.

#include "HLODBuilderMeshApproximate.h"

#include "Algo/ForEach.h"
#include "Algo/Transform.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODLayer.h"

#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"

#include "Modules/ModuleManager.h"
#include "IGeometryProcessingInterfacesModule.h"
#include "GeometryProcessingInterfaces/ApproximateActors.h"


TArray<UPrimitiveComponent*> FHLODBuilder_MeshApproximate::CreateComponents(AWorldPartitionHLOD* InHLODActor, const UHLODLayer* InHLODLayer, const TArray<UPrimitiveComponent*>& InSubComponents)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FHLODBuilder_MeshApproximate::CreateComponents);

	TSet<AActor*> Actors;
	Algo::Transform(InSubComponents, Actors, [](UPrimitiveComponent* InPrimitiveComponent) { return InPrimitiveComponent->GetOwner(); });

	IGeometryProcessingInterfacesModule& GeomProcInterfaces = FModuleManager::Get().LoadModuleChecked<IGeometryProcessingInterfacesModule>("GeometryProcessingInterfaces");
	IGeometryProcessing_ApproximateActors* ApproxActorsAPI = GeomProcInterfaces.GetApproximateActorsImplementation();

	//
	// Construct options for ApproximateActors operation
	//

	const FMeshApproximationSettings& UseSettings = InHLODLayer->GetMeshApproximationSettings();

	IGeometryProcessing_ApproximateActors::FOptions Options = ApproxActorsAPI->ConstructOptions(UseSettings);
	Options.BasePackagePath = InHLODActor->GetPackage()->GetName();
	Options.bGenerateLightmapUVs = false;
	Options.bCreatePhysicsBody = false;

	// Material baking settings
	Options.BakeMaterial = GEngine->DefaultHLODFlattenMaterial;
	Options.BaseColorTexParamName = FName("BaseColorTexture");
	Options.NormalTexParamName = FName("NormalTexture");
	Options.MetallicTexParamName = FName("MetallicTexture");
	Options.RoughnessTexParamName = FName("RoughnessTexture");
	Options.SpecularTexParamName = FName("SpecularTexture");
	Options.EmissiveTexParamName = FName("EmissiveHDRTexture");
	Options.bUsePackedMRS = true;
	Options.PackedMRSTexParamName = FName("PackedTexture");

	// Scale capture size based on grid promotion (higher z grid index -> higher area covered)
	uint64 GridIndexX = 0;
	uint64 GridIndexY = 0;
	uint64 GridIndexZ = 0;
	InHLODActor->GetGridIndices(GridIndexX, GridIndexY, GridIndexZ);
	uint64 TexSizeMultiplier = (uint64)FMath::Pow(2.0f, GridIndexZ);
	Options.TextureImageSize *= TexSizeMultiplier;
	Options.RenderCaptureImageSize *= TexSizeMultiplier;

	// run actor approximation computation
	IGeometryProcessing_ApproximateActors::FResults Results;
	ApproxActorsAPI->ApproximateActors(Actors.Array(), Options, Results);

	TArray<UPrimitiveComponent*> Components;
	if (Results.ResultCode == IGeometryProcessing_ApproximateActors::EResultCode::Success)
	{
		auto FixupAsset = [&InHLODActor](UObject* Asset)
		{
			Asset->ClearFlags(RF_Public | RF_Standalone);
			Asset->Rename(nullptr, InHLODActor->GetPackage(), REN_NonTransactional | REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
		};
	
		Algo::ForEach(Results.NewMeshAssets, FixupAsset);
		Algo::ForEach(Results.NewMaterials, FixupAsset);
		Algo::ForEach(Results.NewTextures, FixupAsset);

		for (UStaticMesh* StaticMesh : Results.NewMeshAssets)
		{
			UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(InHLODActor);
			Component->SetStaticMesh(StaticMesh);
			DisableCollisions(Component);

			Components.Add(Component);
		}

		for (UMaterialInterface* Material : Results.NewMaterials)
		{
			UMaterialInstance* MaterialInst = CastChecked<UMaterialInstance>(Material);

			FStaticParameterSet StaticParameterSet;
			
			auto SetStaticSwitch = [&StaticParameterSet](FName ParamName, bool bSet)
			{
				if (bSet)
				{
					FStaticSwitchParameter SwitchParameter;
					SwitchParameter.ParameterInfo.Name = ParamName;
					SwitchParameter.Value = true;
					SwitchParameter.bOverride = true;
					StaticParameterSet.StaticSwitchParameters.Add(SwitchParameter);
				}
			};

			// Set proper switches needed by our base flatten material
			SetStaticSwitch("UseBaseColor", Options.bBakeBaseColor);
			SetStaticSwitch("UseRoughness", Options.bBakeRoughness);
			SetStaticSwitch("UseMetallic", Options.bBakeMetallic);
			SetStaticSwitch("UseSpecular", Options.bBakeSpecular);
			SetStaticSwitch("UseEmissiveHDR", Options.bBakeEmissive);
			SetStaticSwitch("UseNormal", Options.bBakeNormalMap);
			SetStaticSwitch("PackMetallic", Options.bUsePackedMRS);
			SetStaticSwitch("PackSpecular", Options.bUsePackedMRS);
			SetStaticSwitch("PackRoughness", Options.bUsePackedMRS);

			// Force initializing the static permutations according to the switches we have set
			MaterialInst->UpdateStaticPermutation(StaticParameterSet);
			MaterialInst->InitStaticPermutation();
			MaterialInst->PostEditChange();
		}
	}

	return Components;
}
