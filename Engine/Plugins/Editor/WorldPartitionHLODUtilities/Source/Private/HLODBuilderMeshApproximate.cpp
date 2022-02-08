// Copyright Epic Games, Inc. All Rights Reserved.

#include "HLODBuilderMeshApproximate.h"

#include "Algo/ForEach.h"
#include "Algo/Transform.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODLayer.h"

#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "MaterialUtilities.h"

#include "Modules/ModuleManager.h"
#include "IGeometryProcessingInterfacesModule.h"
#include "GeometryProcessingInterfaces/ApproximateActors.h"

#include "Materials/Material.h"
#include "Engine/HLODProxy.h"
#include "Serialization/ArchiveCrc32.h"

#include "HLODBuilderInstancing.h"


UHLODBuilderMeshApproximate::UHLODBuilderMeshApproximate(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UHLODBuilderMeshApproximateSettings::UHLODBuilderMeshApproximateSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	if (!IsTemplate())
	{
		HLODMaterial = GEngine->DefaultHLODFlattenMaterial;
	}
#endif
}

uint32 UHLODBuilderMeshApproximateSettings::GetCRC() const
{
	UHLODBuilderMeshApproximateSettings& This = *const_cast<UHLODBuilderMeshApproximateSettings*>(this);

	FArchiveCrc32 Ar;

	Ar << This.MeshApproximationSettings;
	UE_LOG(LogHLODBuilder, VeryVerbose, TEXT(" - MeshApproximationSettings = %d"), Ar.GetCrc());

	uint32 Hash = Ar.GetCrc();

	if (!HLODMaterial.IsNull())
	{
		UMaterialInterface* Material = HLODMaterial.LoadSynchronous();
		if (Material)
		{
			uint32 MaterialCRC = UHLODProxy::GetCRC(Material);
			UE_LOG(LogHLODBuilder, VeryVerbose, TEXT(" - Material = %d"), MaterialCRC);
			Hash = HashCombine(Hash, MaterialCRC);
		}
	}

	return Hash;
}

TSubclassOf<UHLODBuilderSettings> UHLODBuilderMeshApproximate::GetSettingsClass() const
{
	return UHLODBuilderMeshApproximateSettings::StaticClass();
}

TArray<UActorComponent*> UHLODBuilderMeshApproximate::Build(const FHLODBuildContext& InHLODBuildContext, const TArray<UActorComponent*>& InSourceComponents) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UHLODBuilderMeshApproximate::Build);

	TArray<UStaticMeshComponent*> StaticMeshComponents;
	TArray<UActorComponent*> InstancedComponents;

	// Filter the input components
	for (UStaticMeshComponent* SubComponent : FilterComponents<UStaticMeshComponent>(InSourceComponents))
	{
		switch (SubComponent->HLODBatchingPolicy)
		{
		case EHLODBatchingPolicy::None:
			StaticMeshComponents.Add(SubComponent);
			break;
		case EHLODBatchingPolicy::Instancing:
			InstancedComponents.Add(SubComponent);
			break;
		case EHLODBatchingPolicy::MeshSection:
			InstancedComponents.Add(SubComponent);
			UE_LOG(LogHLODBuilder, Warning, TEXT("EHLODBatchingPolicy::MeshSection is not yet supported by the UHLODBuilderMeshSimplify builder."));
			break;
		}
	}

	TSet<AActor*> Actors;
	Algo::Transform(StaticMeshComponents, Actors, [](UPrimitiveComponent* PrimitiveComponent) { return PrimitiveComponent->GetOwner(); });
	
	IGeometryProcessingInterfacesModule& GeomProcInterfaces = FModuleManager::Get().LoadModuleChecked<IGeometryProcessingInterfacesModule>("GeometryProcessingInterfaces");
	IGeometryProcessing_ApproximateActors* ApproxActorsAPI = GeomProcInterfaces.GetApproximateActorsImplementation();

	//
	// Construct options for ApproximateActors operation
	//

	const UHLODBuilderMeshApproximateSettings* MeshApproximateSettings = CastChecked<UHLODBuilderMeshApproximateSettings>(HLODBuilderSettings);
	const FMeshApproximationSettings& UseSettings = MeshApproximateSettings->MeshApproximationSettings;
	UMaterial* HLODMaterial = MeshApproximateSettings->HLODMaterial.LoadSynchronous();

	IGeometryProcessing_ApproximateActors::FOptions Options = ApproxActorsAPI->ConstructOptions(UseSettings);
	Options.BasePackagePath = InHLODBuildContext.AssetsOuter->GetPackage()->GetName();
	Options.bGenerateLightmapUVs = false;
	Options.bCreatePhysicsBody = false;

	// Material baking settings
	Options.BakeMaterial = HLODMaterial;
	Options.BaseColorTexParamName = FName("BaseColorTexture");
	Options.NormalTexParamName = FName("NormalTexture");
	Options.MetallicTexParamName = FName("MetallicTexture");
	Options.RoughnessTexParamName = FName("RoughnessTexture");
	Options.SpecularTexParamName = FName("SpecularTexture");
	Options.EmissiveTexParamName = FName("EmissiveHDRTexture");
	Options.bUsePackedMRS = true;
	Options.PackedMRSTexParamName = FName("PackedTexture");

	// Gather bounds of the input components
	auto GetActorsBounds = [&]() -> FBoxSphereBounds
	{
		FBoxSphereBounds Bounds;
		bool bFirst = true;

		for (UPrimitiveComponent* Component : StaticMeshComponents)
		{
			FBoxSphereBounds ComponentBounds = Component->Bounds;
			Bounds = bFirst ? ComponentBounds : Bounds + ComponentBounds;
			bFirst = false;
		}

		return Bounds;
	};
	
	// Compute texel density if needed, depending on the TextureSizingType setting
	ETextureSizingType TextureSizingType = UseSettings.MaterialSettings.TextureSizingType;
	float TexelDensityPerMeter = 0.0f;

	IGeometryProcessing_ApproximateActors::ETextureSizePolicy TextureSizePolicy = IGeometryProcessing_ApproximateActors::ETextureSizePolicy::TextureSize;
	if (TextureSizingType == ETextureSizingType::TextureSizingType_AutomaticFromTexelDensity)
	{
		TexelDensityPerMeter = UseSettings.MaterialSettings.TargetTexelDensityPerMeter;
		TextureSizePolicy = IGeometryProcessing_ApproximateActors::ETextureSizePolicy::TexelDensity;
	}
	else if (TextureSizingType == ETextureSizingType::TextureSizingType_AutomaticFromMeshScreenSize)
	{
		TexelDensityPerMeter = FMaterialUtilities::ComputeRequiredTexelDensityFromScreenSize(UseSettings.MaterialSettings.MeshMaxScreenSizePercent, GetActorsBounds().SphereRadius);
		TextureSizePolicy = IGeometryProcessing_ApproximateActors::ETextureSizePolicy::TexelDensity;
	}
	else if (TextureSizingType == ETextureSizingType::TextureSizingType_AutomaticFromMeshDrawDistance)
	{
		TexelDensityPerMeter = FMaterialUtilities::ComputeRequiredTexelDensityFromDrawDistance(UseSettings.MaterialSettings.MeshMinDrawDistance, GetActorsBounds().SphereRadius);
		TextureSizePolicy = IGeometryProcessing_ApproximateActors::ETextureSizePolicy::TexelDensity;
	}

	Options.MeshTexelDensity = TexelDensityPerMeter;
	Options.TextureSizePolicy = TextureSizePolicy;

	// run actor approximation computation
	IGeometryProcessing_ApproximateActors::FResults Results;
	ApproxActorsAPI->ApproximateActors(Actors.Array(), Options, Results);

	TArray<UActorComponent*> Components;
	if (Results.ResultCode == IGeometryProcessing_ApproximateActors::EResultCode::Success)
	{
		auto FixupAsset = [InHLODBuildContext](UObject* Asset)
		{
			Asset->ClearFlags(RF_Public | RF_Standalone);
			Asset->Rename(nullptr, InHLODBuildContext.AssetsOuter, REN_NonTransactional | REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
		};
	
		Algo::ForEach(Results.NewMeshAssets, FixupAsset);
		Algo::ForEach(Results.NewMaterials, FixupAsset);
		Algo::ForEach(Results.NewTextures, FixupAsset);

		for (UStaticMesh* StaticMesh : Results.NewMeshAssets)
		{
			UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>();
			Component->SetStaticMesh(StaticMesh);

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

	// Batch instances
	if (InstancedComponents.Num())
	{
		UHLODBuilderInstancing* InstancingHLODBuilder = NewObject<UHLODBuilderInstancing>();
		Components.Append(InstancingHLODBuilder->Build(InHLODBuildContext, InstancedComponents));
	}

	return Components;
}
