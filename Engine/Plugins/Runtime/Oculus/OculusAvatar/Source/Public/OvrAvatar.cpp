// Copyright Epic Games, Inc. All Rights Reserved.

#include "OvrAvatar.h"
#include "OvrAvatarManager.h"
#include "OvrAvatarHelpers.h"
#include "Engine/SkeletalMesh.h"
#include "Components/MeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Model.h"
#include "Animation/MorphTarget.h"
#include "MotionControllerComponent.h"
#include "OculusHMD.h"
#include "OvrAvatarGazeTarget.h"
#include "Misc/ScopeLock.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "XRMotionControllerBase.h"

static float GAvatarVisemeMultiplier = 1.5f;
static FAutoConsoleVariableRef CVarOVRGBlendShapeIndex(
	TEXT("oculus.avatars.visemeMultiplier"),
	GAvatarVisemeMultiplier,
	TEXT("Use To Set Avatar Viseme Scalar.\n"),
	ECVF_Default
);


FString UOvrAvatar::HandNames[HandType_Count] = { FString("hand_left"), FString("hand_right") };
FString UOvrAvatar::ControllerNames[HandType_Count] = { FString("controller_left"), FString("controller_right") };
FString UOvrAvatar::BodyName = FString("body");

const uint64 UOvrAvatar::GearVRControllerMeshID = 7900549095409034633ull;
const uint64 UOvrAvatar::GoControllerMeshID = 14216321678048096174ull;

#if PLATFORM_ANDROID
	FString UOvrAvatar::Single = FString(TEXT("/OculusAvatar/Materials/AvatarsPBR_2/OculusAvatars_PBRV2_Mobile"));
	FString UOvrAvatar::Combined = FString(TEXT("/OculusAvatar/Materials/AvatarsPBR_2/OculusAvatars_PBRV2_Mobile_Combined"));

	FString UOvrAvatar::ExpressiveMaskedBody = FString(TEXT("/OculusAvatar/Materials/AvatarsPBR_2/OculusAvatars_PBRV2_Exp_Opaque_Body_Mobile"));
	FString UOvrAvatar::ExpressiveAlphaBody = FString(TEXT("/OculusAvatar/Materials/AvatarsPBR_2/OculusAvatars_PBRV2_Exp_Opaque_Body_Mobile"));
	FString UOvrAvatar::ExpressiveOpaqueBody = FString(TEXT("/OculusAvatar/Materials/AvatarsPBR_2/OculusAvatars_PBRV2_Exp_Opaque_Body_Mobile"));

	FString UOvrAvatar::ExpressiveAlphaSimple = FString(TEXT("/OculusAvatar/Materials/AvatarsPBR_2/OculusAvatars_PBRV2_Exp_Opaque_Simple_Mobile"));
	FString UOvrAvatar::ExpressiveMaskedSimple = FString(TEXT("/OculusAvatar/Materials/AvatarsPBR_2/OculusAvatars_PBRV2_Exp_Opaque_Simple_Mobile"));
	FString UOvrAvatar::ExpressiveOpaqueSimple = FString(TEXT("/OculusAvatar/Materials/AvatarsPBR_2/OculusAvatars_PBRV2_Exp_Opaque_Simple_Mobile"));

	FString UOvrAvatar::ExpressiveCombinedMasked = FString(TEXT("/OculusAvatar/Materials/AvatarsPBR_2/OculusAvatars_PBRV2_Combined_Exp_Opaque_Mobile"));
	FString UOvrAvatar::ExpressiveCombinedOpaque = FString(TEXT("/OculusAvatar/Materials/AvatarsPBR_2/OculusAvatars_PBRV2_Combined_Exp_Opaque_Mobile"));
	FString UOvrAvatar::ExpressiveCombinedAlpha = FString(TEXT("/OculusAvatar/Materials/AvatarsPBR_2/OculusAvatars_PBRV2_Combined_Exp_Opaque_Mobile"));
#else
	FString UOvrAvatar::Single = FString(TEXT("/OculusAvatar/Materials/AvatarsPBR_2/OculusAvatars_PBRV2.OculusAvatars_PBRV2"));
	FString UOvrAvatar::Combined = FString(TEXT("/OculusAvatar/Materials/AvatarsPBR_2/OculusAvatars_PBRV2_Combined"));

	FString UOvrAvatar::ExpressiveMaskedBody = FString(TEXT("/OculusAvatar/Materials/AvatarsPBR_2/OculusAvatars_PBRV2_Exp_Masked_Body"));
	FString UOvrAvatar::ExpressiveAlphaBody = FString(TEXT("/OculusAvatar/Materials/AvatarsPBR_2/OculusAvatars_PBRV2_Exp_Alpha_Body"));
	FString UOvrAvatar::ExpressiveOpaqueBody = FString(TEXT("/OculusAvatar/Materials/AvatarsPBR_2/OculusAvatars_PBRV2_Exp_Opaque_Body"));

	FString UOvrAvatar::ExpressiveAlphaSimple = FString(TEXT("/OculusAvatar/Materials/AvatarsPBR_2/OculusAvatars_PBRV2_Exp_Alpha_Simple"));
	FString UOvrAvatar::ExpressiveMaskedSimple = FString(TEXT("/OculusAvatar/Materials/AvatarsPBR_2/OculusAvatars_PBRV2_Exp_Masked_Simple"));
	FString UOvrAvatar::ExpressiveOpaqueSimple = FString(TEXT("/OculusAvatar/Materials/AvatarsPBR_2/OculusAvatars_PBRV2_Exp_Opaque_Simple"));

	FString UOvrAvatar::ExpressiveCombinedMasked = FString(TEXT("/OculusAvatar/Materials/AvatarsPBR_2/OculusAvatars_PBRV2_Combined_Exp_Masked"));
	FString UOvrAvatar::ExpressiveCombinedOpaque = FString(TEXT("/OculusAvatar/Materials/AvatarsPBR_2/OculusAvatars_PBRV2_Combined_Exp_Opaque"));
	FString UOvrAvatar::ExpressiveCombinedAlpha = FString(TEXT("/OculusAvatar/Materials/AvatarsPBR_2/OculusAvatars_PBRV2_Combined_Exp_Alpha"));
#endif
	FString UOvrAvatar::ExpressiveEyeShell = FString(TEXT("/OculusAvatar/Materials/AvatarsPBR_2/OculusAvatars_PBRV2_EyeShell"));
	FString UOvrAvatar::ExpressiveController = FString(TEXT("/OculusAvatar/Materials/AvatarsPBR_2/OculusAvatars_PBRV2_ExpressiveController"));


FColor UOvrAvatar::GetColorFromVertex(const ovrAvatarMeshVertex& vertex)
{
	return FColor::Black;
}

FColor UOvrAvatar::GetColorFromVertex(const ovrAvatarMeshVertexV2& vertex)
{
	FLinearColor LinearColor(vertex.r, vertex.g, vertex.b, vertex.a);
	return LinearColor.ToFColor(false);
}

UOvrAvatar::UOvrAvatar()
{
	PrimaryComponentTick.bCanEverTick = true;

	OvrAvatarHelpers::OvrAvatarIdentity(WorldTransform);
}

void UOvrAvatar::BeginPlay()
{
	Super::BeginPlay();

	OvrAvatarHelpers::OvrAvatarHandISZero(HandInputState[HandType_Left]);
	OvrAvatarHelpers::OvrAvatarHandISZero(HandInputState[HandType_Right]);
	HandInputState[HandType_Left].isActive = true;
	HandInputState[HandType_Right].isActive = true;
	AvatarHands[HandType_Left] = nullptr;
	AvatarHands[HandType_Right] = nullptr;

	b3DofHardware = Is3DOFHardware();

	ControllerType = GetControllerTypeByHardware();
}

void UOvrAvatar::BeginDestroy()
{
	Super::BeginDestroy();

	if (Avatar)
	{
		ovrAvatar_Destroy(Avatar);
		Avatar = nullptr;
	}

}

void UOvrAvatar::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!Avatar || AssetIds.Num() != 0)
		return;

	const FTransform UETrans = GetOwner()->GetRootComponent()->GetComponentTransform();
	OvrAvatarHelpers::FTransfromToOvrAvatarTransform(UETrans, WorldTransform);

	ovrAvatar_UpdateWorldTransform(Avatar, WorldTransform);

	UpdateHeadGazeTarget();

	UpdateSDK(DeltaTime);
	UpdatePostSDK();
	UpdateV2VoiceOffsetParams();
}

void UOvrAvatar::AddMeshComponent(ovrAvatarAssetID id, UPoseableMeshComponent* mesh)
{
	if (!GetMeshComponent(id))
	{
		MeshComponents.Add(id, mesh);
	}
}

void UOvrAvatar::AddDepthMeshComponent(ovrAvatarAssetID id, UPoseableMeshComponent* mesh)
{
	if (!GetDepthMeshComponent(id))
	{
		DepthMeshComponents.Add(id, mesh);
	}
}

inline ovrAvatarCapabilities operator|(ovrAvatarCapabilities a, ovrAvatarCapabilities b)
{
	return static_cast<ovrAvatarCapabilities>(static_cast<int>(a) | static_cast<int>(b));
}

inline ovrAvatarCapabilities& operator|=(ovrAvatarCapabilities& a, ovrAvatarCapabilities b)
{
	return (ovrAvatarCapabilities&)((int&)a |= (int)b);
}

void UOvrAvatar::HandleAvatarSpecification(const ovrAvatarMessage_AvatarSpecification* message)
{
	if (Avatar || !message->oculusUserID || OnlineUserID != message->oculusUserID)
		return;

	ovrAvatarCapabilities AvatarCapabilities = (ovrAvatarCapabilities)0;
	if (EnableBody) AvatarCapabilities |= ovrAvatarCapability_Body;
	if (EnableHands) AvatarCapabilities |= ovrAvatarCapability_Hands;
	if (EnableBase && EnableBody) AvatarCapabilities |= ovrAvatarCapability_Base;
	if (EnableExpressive) AvatarCapabilities |= ovrAvatarCapability_Expressive;
	if (!Is3DOFHardware()) AvatarCapabilities |= ovrAvatarCapability_BodyTilt;

	Avatar = ovrAvatar_Create(message->avatarSpec, AvatarCapabilities);

	ovrAvatar_SetLeftControllerVisibility(Avatar, LeftControllerVisible);
	ovrAvatar_SetRightControllerVisibility(Avatar, RightControllerVisible);

	NativeBodyComponent = ovrAvatarPose_GetBodyComponent(Avatar);
	NativeLeftHandComponent = ovrAvatarPose_GetLeftHandComponent(Avatar);
	NativeRightHandComponent = ovrAvatarPose_GetRightHandComponent(Avatar);
	NativeRightControllerComponent = ovrAvatarPose_GetRightControllerComponent(Avatar);
	NativeLeftControllerComponent = ovrAvatarPose_GetLeftControllerComponent(Avatar);

	const uint32_t ComponentCount = ovrAvatarComponent_Count(Avatar);
	RootAvatarComponents.Reserve(ComponentCount);

	for (uint32_t CompIndex = 0; CompIndex < ComponentCount; ++CompIndex)
	{
		const ovrAvatarComponent* AvatarComponent = ovrAvatarComponent_Get(Avatar, CompIndex);

		const bool IsBodyComponent = NativeBodyComponent && AvatarComponent == NativeBodyComponent->renderComponent;
		const bool IsLeftHandComponent = NativeLeftHandComponent && AvatarComponent == NativeLeftHandComponent->renderComponent;
		const bool IsRightHandComponent = NativeRightHandComponent && AvatarComponent == NativeRightHandComponent->renderComponent;
		const bool IsRightControllerComponent = NativeRightControllerComponent && AvatarComponent == NativeRightControllerComponent->renderComponent;
		const bool IsLeftControllerComponent = NativeLeftControllerComponent && AvatarComponent == NativeLeftControllerComponent->renderComponent;

		const bool ShouldUseMotionControllerComponent
			= (IsLeftHandComponent ||
				IsRightControllerComponent ||
				IsLeftControllerComponent ||
				IsRightHandComponent)
			&& GetOwner()->HasLocalNetOwner();

		FString Name = AvatarComponent->name;
		USceneComponent* BaseComponent = nullptr;
			
		if (ShouldUseMotionControllerComponent)
		{
			BaseComponent = NewObject<UMotionControllerComponent>(this, *Name);
			static_cast<UMotionControllerComponent*>(BaseComponent)->MotionSource
				= IsLeftControllerComponent || IsLeftHandComponent 
				? FXRMotionControllerBase::LeftHandSourceId 
				: FXRMotionControllerBase::RightHandSourceId;
		}
		else 
		{
			BaseComponent = NewObject<USceneComponent>(this, *Name);
		}

		BaseComponent->AttachToComponent(GetOwner()->GetRootComponent(), FAttachmentTransformRules::SnapToTargetIncludingScale);
		BaseComponent->RegisterComponent();
		RootAvatarComponents.Add(Name, BaseComponent);

		if (EnableExpressive)
		{
			if (IsLeftHandComponent)
			{
				static const FName LeftHandGazeName = "LeftHandGazeTarget";
				AvatarLeftHandTarget = NewObject<UOvrAvatarGazeTarget>(this, LeftHandGazeName);
				AvatarLeftHandTarget->SetGazeTransform(BaseComponent);
				AvatarLeftHandTarget->SetGazeTargetType(OculusAvatarGazeTargetType::AvatarHand);
				AvatarLeftHandTarget->RegisterComponent();
			}

			if (IsRightHandComponent)
			{
				static const FName RightHandGazeName = "RightHandGazeTarget";
				AvatarRightHandTarget = NewObject<UOvrAvatarGazeTarget>(this, RightHandGazeName);
				AvatarRightHandTarget->SetGazeTransform(BaseComponent);
				AvatarRightHandTarget->SetGazeTargetType(OculusAvatarGazeTargetType::AvatarHand);
				AvatarRightHandTarget->RegisterComponent();
			}

			if (IsBodyComponent)
			{
				static const FName AvatarHeadGazeName = "AvatarHeadGazeTarget";
				AvatarHeadTarget = NewObject<UOvrAvatarGazeTarget>(this, AvatarHeadGazeName);
				AvatarHeadTarget->SetGazeTargetType(OculusAvatarGazeTargetType::AvatarHead);
				AvatarHeadTarget->RegisterComponent();
			}
		}

		for (uint32_t RenderIndex = 0; RenderIndex < AvatarComponent->renderPartCount; ++RenderIndex)
		{
			const ovrAvatarRenderPart* RenderPart = AvatarComponent->renderParts[RenderIndex];

			switch (ovrAvatarRenderPart_GetType(RenderPart))
			{
			case ovrAvatarRenderPartType_SkinnedMeshRender:
			{
				const ovrAvatarRenderPart_SkinnedMeshRender* RenderData = ovrAvatarRenderPart_GetSkinnedMeshRender(RenderPart);
				FString MeshNameString = Name + FString::Printf(TEXT("_%u"), RenderIndex);
				UPoseableMeshComponent* MeshComponent = CreateMeshComponent(BaseComponent, RenderData->meshAssetID, *MeshNameString);

				if (IsBodyComponent)
				{
					ovrAvatarBodyPartType PartType = ovrAvatarRenderPart_GetBodyPartType(Avatar, RenderPart);
					if (PartType == ovrAvatarBodyPartType_Body)
					{
						BodyMeshID = RenderData->meshAssetID;
					}
				}

				if (UseDepthMeshes)
				{
					MeshNameString += TEXT("_Depth");
					UPoseableMeshComponent* DepthMesh = CreateDepthMeshComponent(BaseComponent, RenderData->meshAssetID, *MeshNameString);
					DepthMesh->SetMasterPoseComponent(MeshComponent);
				}

				const auto& material = RenderData->materialState;
				const bool UseNormalMap = material.normalMapTextureID > 0;
				bool UseParallax = material.parallaxMapTextureID > 0;

				for (uint32_t l = 0; l < material.layerCount && !UseParallax; ++l)
				{
					UseParallax = material.layers[l].sampleMode == ovrAvatarMaterialLayerSampleMode_Parallax;
				}

				FString MaterialFolder = TEXT("");
				FString AlphaFolder = material.alphaMaskTextureID > 0 ? TEXT("On/") : TEXT("Off/");

				if (UseNormalMap && UseParallax)
				{
					MaterialFolder = TEXT("N_ON_P_ON/");
				}
				else if (UseNormalMap && !UseParallax)
				{
					MaterialFolder = TEXT("N_ON_P_OFF/");
				}
				else if (!UseNormalMap && UseParallax)
				{
					MaterialFolder = TEXT("N_OFF_P_ON/");
				}
				else
				{
					MaterialFolder = TEXT("N_OFF_P_OFF/");
				}

				FString sMaterialName = TEXT("OculusAvatar8Layers_Inst_") + FString::FromInt(material.layerCount) + TEXT("Layers");
				FString sMaterialPath = TEXT("/OculusAvatar/Materials/v1/Inst/") + AlphaFolder + MaterialFolder + sMaterialName + TEXT(".") + sMaterialName;

				auto Material = LoadObject<UMaterialInstance>(nullptr, *sMaterialPath, nullptr, LOAD_None, nullptr);
				MeshComponent->SetMaterial(0, UMaterialInstanceDynamic::Create(Material, GetTransientPackage()));
			}
			break;
			case ovrAvatarRenderPartType_SkinnedMeshRenderPBS:
			{
				const ovrAvatarRenderPart_SkinnedMeshRenderPBS* RenderData = ovrAvatarRenderPart_GetSkinnedMeshRenderPBS(RenderPart);
				FString MeshName = Name + FString::Printf(TEXT("_%u"), RenderIndex);

				auto MeshComponent = CreateMeshComponent(BaseComponent, RenderData->meshAssetID, *MeshName);

				auto Material = (UMaterialInterface*)StaticLoadObject(UMaterial::StaticClass(), NULL, TEXT("/OculusAvatar/Materials/OculusAvatarsPBR.OculusAvatarsPBR"), NULL, LOAD_None, NULL);
				MeshComponent->SetMaterial(0, UMaterialInstanceDynamic::Create(Material, GetTransientPackage()));
			}
			break;
			case ovrAvatarRenderPartType_SkinnedMeshRenderPBS_V2:
			{
				const ovrAvatarRenderPart_SkinnedMeshRenderPBS_V2* RenderData = ovrAvatarRenderPart_GetSkinnedMeshRenderPBSV2(RenderPart);

				FString MaterialString;
				ovrAvatarBodyPartType PartType = ovrAvatarRenderPart_GetBodyPartType(Avatar, RenderPart);

				if (IsBodyComponent)
				{
					switch (PartType)
					{
					case ovrAvatarBodyPartType_Body:
						BodyMeshID = UseCombinedBodyMesh ? 0 : RenderData->meshAssetID;
						MaterialString = GetPBRV2BodyMaterial(false);
						break;
					case ovrAvatarBodyPartType_Eyewear:
						MaterialString = GetPBRV2EyeWearMaterial();
						break;
					default:
						MaterialString = GetPBRV2Material();
						break;
					}

					UE_LOG(LogAvatars, Display, TEXT("[Avatars] - Loading Material %s for body part %d"), *MaterialString, PartType);
				}
				else if (IsLeftHandComponent || IsRightHandComponent)
				{
					MaterialString = GetPBRV2HandMaterial();
					UE_LOG(LogAvatars, Display, TEXT("[Avatars] - Loading Material %s for Hands %d"), *MaterialString, PartType);
				}
				else if (IsLeftControllerComponent || IsRightControllerComponent)
				{
					MaterialString = GetPBRV2ControllerMaterial();
				}
				else
				{
					MaterialString = GetPBRV2Material();
				}

				FString MeshName = Name + FString::Printf(TEXT("_%u"), RenderIndex);

				UPoseableMeshComponent* MeshComponent = CreateMeshComponent(BaseComponent, RenderData->meshAssetID, *MeshName);
				MeshComponent->SetVisibility(false, true);

				check(!AssetToMaterialStringsMap.Contains(RenderData->meshAssetID));

				TArray<FString> Materials;
				Materials.Add(MaterialString);

				if (EnableExpressive && PartType == ovrAvatarBodyPartType_Body)
				{
					Materials.Add(ExpressiveEyeShell);
				}

				AssetToMaterialStringsMap.Add(RenderData->meshAssetID, Materials);

				bool AddDepthMesh = BodyMaterial == MaterialType::Translucent && IsBodyComponent;
				AddDepthMesh |= (IsLeftHandComponent || IsRightHandComponent) && HandMaterial == MaterialType::Translucent;
				AddDepthMesh |= !EnableExpressive && UseDepthMeshes;
				
				if (AddDepthMesh)
				{
					MeshName += TEXT("_Depth");
					UPoseableMeshComponent* DepthMesh = CreateDepthMeshComponent(BaseComponent, RenderData->meshAssetID, *MeshName);
					DepthMesh->SetMasterPoseComponent(MeshComponent);
					auto DepthMaterial = (UMaterialInterface*)StaticLoadObject(UMaterial::StaticClass(), NULL, TEXT("/OculusAvatar/Materials/AvatarsPBR_2/OculusAvatars_PBRV2_2_Depth"));
					DepthMesh->SetMaterial(0, UMaterialInstanceDynamic::Create(DepthMaterial, GetTransientPackage()));
					DepthMesh->SetVisibility(false, true);
				}

				// Cache the Normal Map ID for appropriate tagging on Load.
				UOvrAvatarManager::Get().CacheNormalMapID(RenderData->materialState.normalTextureID);
				UOvrAvatarManager::Get().CacheRoughnessMapID(RenderData->materialState.metallicnessTextureID);
			}
			break;
			default:
				break;
			}
		}
	}

	Root3DofControllers();

	const auto AssetsWaitingToLoad = ovrAvatar_GetReferencedAssetCount(Avatar);

	for (uint32_t AssetIndex = 0; AssetIndex < AssetsWaitingToLoad; ++AssetIndex)
	{
		const ovrAvatarAssetID Asset = ovrAvatar_GetReferencedAsset(Avatar, AssetIndex);
		AssetIds.Add(Asset);
		UE_LOG(LogAvatars, Display, TEXT("[Avatars] - Loading Asset ID: %llu"), Asset);

		ovrAvatarAsset_BeginLoadingLOD(Asset, LevelOfDetail);
	}
}

void UOvrAvatar::HandleAssetLoaded(const ovrAvatarMessage_AssetLoaded* message)
{
	const ovrAvatarAssetType assetType = ovrAvatarAsset_GetType(message->asset);
	const bool OurCombinedMeshLoaded = assetType == ovrAvatarAssetType_CombinedMesh && ovrAvatarAsset_GetAvatar(message->asset) == Avatar && Avatar != nullptr;

	if (OurCombinedMeshLoaded)
	{
		UE_LOG(LogAvatars, Display, TEXT("[Avatars] ovrAvatarAssetType_CombinedMesh Loaded"));

		auto BaseComponent = RootAvatarComponents.Find(BodyName);
		if (BaseComponent && BaseComponent->Get())
		{
			BodyMeshID = message->assetID;

			const uint32_t BlendShapeCount = ovrAvatarAsset_GetMeshBlendShapeCount(message->asset);
			BodyBlendShapeNames.Empty();
			for (uint32_t BlendIndex = 0; BlendIndex < BlendShapeCount; BlendIndex++)
			{
				FName BlendName = FName(ovrAvatarAsset_GetMeshBlendShapeName(message->asset, BlendIndex));
				BodyBlendShapeNames.Add(BlendName);
			}

			USkeletalMesh * mesh = NewObject<USkeletalMesh>(GetTransientPackage(), NAME_None, RF_Transient);

			UE_LOG(LogAvatars, Display, TEXT("Loading Combined Mesh"));
			FString MeshName = BodyName + TEXT("_Combined");

			UPoseableMeshComponent* MeshComponent = CreateMeshComponent(BaseComponent->Get(), message->assetID, *MeshName);

			TArray<FString> Materials;
			auto MaterialString = GetPBRV2BodyMaterial(true);
			Materials.Add(MaterialString);

			UE_LOG(LogAvatars, Display, TEXT("[Avatars] - Loading Combined Mesh Material %s "), *MaterialString);

			if (EnableExpressive)
			{
				Materials.Add(ExpressiveEyeShell);
			}

			AssetToMaterialStringsMap.Add(message->assetID, Materials);

			LoadCombinedMesh(mesh, ovrAvatarAsset_GetCombinedMeshData(message->asset), message->asset, message->assetID);

			MeshComponent->SetSkeletalMesh(mesh);
			MeshComponent->RecreateRenderState_Concurrent();
			MeshComponent->SetVisibility(false, true);

			if (BodyMaterial == MaterialType::Translucent || (!EnableExpressive && UseDepthMeshes))
			{
				MeshName += TEXT("_Depth");
				UPoseableMeshComponent* DepthMesh = CreateDepthMeshComponent(BaseComponent->Get(), message->assetID, *MeshName);
				auto DepthMaterial = (UMaterialInterface*)StaticLoadObject(UMaterial::StaticClass(), NULL, TEXT("/OculusAvatar/Materials/AvatarsPBR_2/OculusAvatars_PBRV2_2_Depth"));
				DepthMesh->SetMaterial(0, UMaterialInstanceDynamic::Create(DepthMaterial, GetTransientPackage()));
				DepthMesh->SetSkeletalMesh(mesh);
				DepthMesh->RecreateRenderState_Concurrent();
				DepthMesh->SetMasterPoseComponent(MeshComponent);
				DepthMesh->SetVisibility(false, true);
			}

			uint32_t MeshCount = 0;
			const auto MeshIds = ovrAvatarAsset_GetCombinedMeshIDs(message->asset, &MeshCount);
			for (uint32_t meshIndex = 0; meshIndex < MeshCount; meshIndex++)
			{
				// TODO: Check if they have a mesh assigned and destroy it
				// Can happen if other avatars load the same mesh asset we are waiting on (unlikely as all will be mesh combining, but who knows)
				RemoveMeshComponent(MeshIds[meshIndex]);
				RemoveDepthMeshComponent(MeshIds[meshIndex]);

				AssetIds.Remove(MeshIds[meshIndex]);
			}
		}
	}
	else if (auto Asset = AssetIds.Find(message->assetID))
	{
		AssetIds.Remove(*Asset);

		switch (assetType)
		{
		case ovrAvatarAssetType_Mesh:
		{
			if (UPoseableMeshComponent* MeshComp = GetMeshComponent(message->assetID))
			{
				USkeletalMesh* mesh = NewObject<USkeletalMesh>(GetTransientPackage(), NAME_None, RF_Transient);
				LoadMesh(mesh, ovrAvatarAsset_GetMeshData(message->asset), message->asset, message->assetID);

				if (BodyMeshID == message->assetID)
				{
					const uint32_t BlendShapeCount = ovrAvatarAsset_GetMeshBlendShapeCount(message->asset);
					BodyBlendShapeNames.Empty();
					for (uint32_t BlendIndex = 0; BlendIndex < BlendShapeCount; BlendIndex++)
					{
						FName BlendName = FName(ovrAvatarAsset_GetMeshBlendShapeName(message->asset, BlendIndex));
						BodyBlendShapeNames.Add(BlendName);
					}
				}

				MeshComp->SetSkeletalMesh(mesh);
				MeshComp->RecreateRenderState_Concurrent();

				if (UPoseableMeshComponent* DepthMesh = GetDepthMeshComponent(message->assetID))
				{
					DepthMesh->SetSkeletalMesh(mesh);
					DepthMesh->RecreateRenderState_Concurrent();
				}
			}
		}
		break;
		case ovrAvatarAssetType_Texture:
			if (!UOvrAvatarManager::Get().FindTexture(message->assetID))
			{
				UOvrAvatarManager::Get().LoadTexture(message->assetID, ovrAvatarAsset_GetTextureData(message->asset));
			}
			break;
		default:
			UE_LOG(LogAvatars, Warning, TEXT("[Avatars] Unknown Asset Type"));
			break;
		}
	}

	if (Avatar && AssetIds.Num() == 0 && !AreMaterialsInitialized)
	{
		AreMaterialsInitialized = true;
		InitializeMaterials();
	}
}

UPoseableMeshComponent* UOvrAvatar::GetMeshComponent(ovrAvatarAssetID id) const
{
	UPoseableMeshComponent* Return = nullptr;

	auto MeshComponent = MeshComponents.Find(id);
	if (MeshComponent && MeshComponent->IsValid())
	{
		Return = MeshComponent->Get();
	}

	return Return;
}

void UOvrAvatar::RemoveMeshComponent(ovrAvatarAssetID id)
{
	auto MeshComponent = MeshComponents.Find(id);
	if (MeshComponent && MeshComponent->IsValid())
	{
		MeshComponents.Remove(id);
		MeshComponent->Get()->DestroyComponent(true);
	}
}

UPoseableMeshComponent* UOvrAvatar::GetDepthMeshComponent(ovrAvatarAssetID id) const
{
	UPoseableMeshComponent* Return = nullptr;

	auto MeshComponent = DepthMeshComponents.Find(id);
	if (MeshComponent && MeshComponent->IsValid())
	{
		Return = MeshComponent->Get();
	}

	return Return;
}

void UOvrAvatar::RemoveDepthMeshComponent(ovrAvatarAssetID id)
{
	auto MeshComponent = DepthMeshComponents.Find(id);
	if (MeshComponent && MeshComponent->IsValid())
	{
		DepthMeshComponents.Remove(id);
		MeshComponent->Get()->DestroyComponent(true);
	}
}


void UOvrAvatar::DebugDrawBoneTransforms()
{
	for (auto mesh : MeshComponents)
	{
		if (mesh.Value.IsValid())
		{
			auto skeletalMesh = mesh.Value.Get();
			const auto BoneCount = skeletalMesh->GetNumBones();
			auto BoneTransform = FTransform::Identity;
			for (auto index = 0; index < BoneCount; index++)
			{
				BoneTransform = skeletalMesh->GetBoneTransform(index);
				OvrAvatarHelpers::DebugDrawCoords(GetWorld(), BoneTransform);
			}
		}
	}
}

void UOvrAvatar::DebugDrawSceneComponents()
{
	FTransform world_trans = GetOwner()->GetRootComponent()->GetComponentTransform();
	OvrAvatarHelpers::DebugDrawCoords(GetWorld(), world_trans);

	for (auto comp : RootAvatarComponents)
	{
		if (comp.Value.IsValid())
		{
			world_trans = comp.Value->GetComponentTransform();
			OvrAvatarHelpers::DebugDrawCoords(GetWorld(), world_trans);
		}
	}

	for (auto mesh : MeshComponents)
	{
		if (mesh.Value.IsValid())
		{
			world_trans = mesh.Value->GetComponentTransform();
			OvrAvatarHelpers::DebugDrawCoords(GetWorld(), world_trans);
		}
	}
}

void UOvrAvatar::UpdateSDK(float DeltaTime)
{
	{
		FScopeLock Lock(&VisemeMutex);

		if (VisemeValues.visemeParamCount > 0)
		{
			ovrAvatar_SetVisemes(Avatar, &VisemeValues);
		}
	}


	UpdateTransforms(DeltaTime);
	ovrAvatarPose_Finalize(Avatar, DeltaTime);
}

void UOvrAvatar::UpdatePostSDK()
{
	ovrpHandedness HandedNess = ovrpHandedness_Unsupported;
	ovrp_GetDominantHand(&HandedNess);

	if (HandedNess != DominantHand)
	{
		Root3DofControllers();
	}

	const uint32_t ComponentCount = ovrAvatarComponent_Count(Avatar);
	for (uint32_t ComponentIndex = 0; ComponentIndex < ComponentCount; ComponentIndex++)
	{
		const ovrAvatarComponent* OvrComponent = ovrAvatarComponent_Get(Avatar, ComponentIndex);
		USceneComponent* OvrSceneComponent = nullptr;
		TWeakObjectPtr<UOvrAvatarGazeTarget> TargetToUpdate = nullptr;

		const bool IsBodyComponent = NativeBodyComponent && OvrComponent == NativeBodyComponent->renderComponent;
		const bool IsLeftHandComponent = NativeLeftHandComponent && OvrComponent == NativeLeftHandComponent->renderComponent;
		const bool IsRightHandComponent = NativeRightHandComponent && OvrComponent == NativeRightHandComponent->renderComponent;
		const bool IsRightControllerComponent = NativeRightControllerComponent && OvrComponent == NativeRightControllerComponent->renderComponent;
		const bool IsLeftControllerComponent = NativeLeftControllerComponent && OvrComponent == NativeLeftControllerComponent->renderComponent;

		if (b3DofHardware)
		{
			if (DominantHand == ovrpHandedness_LeftHanded && IsRightControllerComponent ||
				DominantHand == ovrpHandedness_RightHanded && IsLeftControllerComponent)

			continue;
		}

		const bool ShouldSkipTransformUpdate
			= (IsLeftHandComponent ||
				IsRightControllerComponent ||
				IsLeftControllerComponent ||
				IsRightHandComponent)
			&& GetOwner()->HasLocalNetOwner();

		if (IsBodyComponent)
		{
			TargetToUpdate = AvatarHeadTarget;
		}
		else if (IsLeftHandComponent)
		{
			TargetToUpdate = AvatarLeftHandTarget;
		}
		else if (IsRightHandComponent)
		{
			TargetToUpdate = AvatarRightHandTarget;
		}

		if (auto ScenePtr = RootAvatarComponents.Find(FString(OvrComponent->name)))
		{
			OvrSceneComponent = ScenePtr->Get();
			if (!ShouldSkipTransformUpdate && OvrSceneComponent)
			{
				OvrAvatarHelpers::OvrAvatarTransformToSceneComponent(*OvrSceneComponent, OvrComponent->transform);
			}
		}

		for (uint32_t RenderIndex = 0; RenderIndex < OvrComponent->renderPartCount; ++RenderIndex)
		{
			const ovrAvatarRenderPart* RenderPart = OvrComponent->renderParts[RenderIndex];

			switch (ovrAvatarRenderPart_GetType(RenderPart))
			{
			case ovrAvatarRenderPartType_SkinnedMeshRender:
			{
				const ovrAvatarRenderPart_SkinnedMeshRender* RenderData = ovrAvatarRenderPart_GetSkinnedMeshRender(RenderPart);
				const bool MeshVisible = (VisibilityMask & RenderData->visibilityMask) != 0;

				if (UPoseableMeshComponent* mesh = GetMeshComponent(RenderData->meshAssetID))
				{
					if (MeshVisible)
					{
						UpdateMeshComponent(*mesh, RenderData->localTransform);
						UpdateSkeleton(*mesh, RenderData->skinnedPose);
					}

					mesh->SetVisibility(MeshVisible, true);
				}

				if (UPoseableMeshComponent* depthMesh = GetDepthMeshComponent(RenderData->meshAssetID))
				{
					const bool IsSelfOccluding = (RenderData->visibilityMask & ovrAvatarVisibilityFlag_SelfOccluding) > 0;

					if (MeshVisible && IsSelfOccluding)
					{
						UpdateMeshComponent(*depthMesh, RenderData->localTransform);
						depthMesh->MarkRefreshTransformDirty();
					}

					depthMesh->SetVisibility(MeshVisible && IsSelfOccluding, true);
				}
			}
			break;
			case ovrAvatarRenderPartType_SkinnedMeshRenderPBS:
			{
				const ovrAvatarRenderPart_SkinnedMeshRenderPBS* RenderData = ovrAvatarRenderPart_GetSkinnedMeshRenderPBS(RenderPart);
				if (UPoseableMeshComponent* mesh = GetMeshComponent(RenderData->meshAssetID))
				{
					const bool MeshVisible = (VisibilityMask & RenderData->visibilityMask) != 0;
					if (MeshVisible)
					{
						UpdateMeshComponent(*mesh, RenderData->localTransform);
						UpdateSkeleton(*mesh, RenderData->skinnedPose);
					}

					mesh->SetVisibility(MeshVisible, true);
				}
			}
			break;
			case ovrAvatarRenderPartType_SkinnedMeshRenderPBS_V2:
			{
				const ovrAvatarRenderPart_SkinnedMeshRenderPBS_V2* RenderData = ovrAvatarRenderPart_GetSkinnedMeshRenderPBSV2(RenderPart);
				const bool MeshVisible = (VisibilityMask & RenderData->visibilityMask) != 0;

				if (UPoseableMeshComponent* mesh = GetMeshComponent(RenderData->meshAssetID))
				{
					if (MeshVisible)
					{
						UpdateMeshComponent(*mesh, RenderData->localTransform);
						UpdateSkeleton(*mesh, RenderData->skinnedPose);
						UpdateMorphTargets(*mesh, RenderPart);
					}

					mesh->SetVisibility(MeshVisible, true);

					if (TargetToUpdate.Get())
					{
						TargetToUpdate->EnableGazeTarget(MeshVisible);
					}
				}

				if (UPoseableMeshComponent* depthMesh = GetDepthMeshComponent(RenderData->meshAssetID))
				{
					const bool IsSelfOccluding = (RenderData->visibilityMask & ovrAvatarVisibilityFlag_SelfOccluding) > 0;

					if (MeshVisible && IsSelfOccluding)
					{
						UpdateMeshComponent(*depthMesh, RenderData->localTransform);
						depthMesh->MarkRefreshTransformDirty();
						UpdateMorphTargets(*depthMesh, RenderPart);
					}

					depthMesh->SetVisibility(MeshVisible && IsSelfOccluding, true);
				}
			}
			break;
			default:
				break;
			}
		}
	}
}

void UOvrAvatar::UpdateTransforms(float DeltaTime)
{
	if (PlayerType != ePlayerType::Local)
		return;

	if (!UOvrAvatarManager::Get().IsOVRPluginValid())
		return;

	OculusHMD::FOculusHMD* OculusHMD = (OculusHMD::FOculusHMD*)(GEngine->XRSystem.Get());

	if (!OculusHMD)
		return;

	OculusHMD::CheckInGameThread();
	OculusHMD::FSettings* Settings = OculusHMD->GetSettings();
	OculusHMD::FGameFrame* CurrentFrame = OculusHMD->GetNextFrameToRender();

	if (!Settings || !CurrentFrame)
		return;

	OvrAvatarHelpers::OvrAvatarIdentity(BodyTransform);

	// Head
	{
		ovrpPoseStatef InPoseState;
		OculusHMD::FPose OutPose;

		if (OVRP_SUCCESS(ovrp_GetNodePoseState3(ovrpStep_Render, CurrentFrame->FrameNumber, ovrpNode_Head, &InPoseState)) &&
			OculusHMD->ConvertPose_Internal(InPoseState.Pose, OutPose, Settings, 1.0f))
		{
			ovrpPosef ovrPose;
			ovrPose.Orientation = OculusHMD::ToOvrpQuatf(OutPose.Orientation);
			ovrPose.Position = OculusHMD::ToOvrpVector3f(OutPose.Position);

			OvrAvatarHelpers::OvrPoseToAvatarTransform(BodyTransform, ovrPose);
		}
	}

	ovrpResult result;
	ovrpController ControllerMask = ovrpController_None;
	ovrpController ActiveController = ovrpController_None;

	if (OVRP_FAILURE(result = ovrp_GetConnectedControllers2(&ControllerMask)))
	{
		UE_LOG(LogAvatars, Display, TEXT("ovrp_GetConnectedControllers2 failed %d"), result);
	}

	if (OVRP_FAILURE(result = ovrp_GetActiveController2(&ActiveController)))
	{
		UE_LOG(LogAvatars, Display, TEXT("ovrp_GetActiveController2 failed %d"), result);
	}

	// Left hand
	{
		ovrpController LeftControllerType = ovrpController_None;

		if (ControllerMask & ovrpController_LTouch)
		{
			LeftControllerType = ovrpController_LTouch;
		}
		else if (ControllerMask & ovrpController_LTrackedRemote)
		{
			LeftControllerType = ovrpController_LTrackedRemote;
		}

		ovrAvatarHandInputState& handInputState = HandInputState[HandType_Left];
		handInputState.isActive = false;

		if (LeftControllerType != ovrpController_None)
		{
			ovrpPoseStatef InPoseState;
			OculusHMD::FPose OutPose;

			if (OVRP_SUCCESS(ovrp_GetNodePoseState3(ovrpStep_Render, CurrentFrame->FrameNumber, ovrpNode_HandLeft, &InPoseState)) &&
				OculusHMD->ConvertPose_Internal(InPoseState.Pose, OutPose, Settings, 1.0f))
			{
				ovrpPosef ovrPose;
				ovrPose.Orientation = OculusHMD::ToOvrpQuatf(OutPose.Orientation);
				ovrPose.Position = OculusHMD::ToOvrpVector3f(OutPose.Position);

				OvrAvatarHelpers::OvrPoseToAvatarTransform(handInputState.transform, ovrPose);
			}

			ovrpControllerState4 controllerState;
			if (OVRP_SUCCESS(ovrp_GetControllerState4(LeftControllerType, &controllerState)))
			{
				handInputState.isActive = true;
				handInputState.indexTrigger = controllerState.IndexTrigger[ovrpHand_Left];
				handInputState.handTrigger = controllerState.HandTrigger[ovrpHand_Left];
				handInputState.joystickX = controllerState.Thumbstick[ovrpHand_Left].x;
				handInputState.joystickY = controllerState.Thumbstick[ovrpHand_Left].y;

				OvrAvatarHelpers::OvrAvatarParseButtonsAndTouches(controllerState, ovrpHand_Left, handInputState);
			}
		}
	}
	// Right hand
	{
		ovrpController RightControllerType = ovrpController_None;

		if (ControllerMask & ovrpController_RTouch)
		{
			RightControllerType = ovrpController_RTouch;
		}
		else if (ControllerMask & ovrpController_RTrackedRemote)
		{
			RightControllerType = ovrpController_RTrackedRemote;
		}

		ovrAvatarHandInputState& handInputState = HandInputState[ovrpHand_Right];
		handInputState.isActive = false;

		if (RightControllerType != ovrpController_None)
		{
			ovrpPoseStatef InPoseState;
			OculusHMD::FPose OutPose;

			if (OVRP_SUCCESS(ovrp_GetNodePoseState3(ovrpStep_Render, CurrentFrame->FrameNumber, ovrpNode_HandRight, &InPoseState)) &&
				OculusHMD->ConvertPose_Internal(InPoseState.Pose, OutPose, Settings, 1.0f))
			{
				ovrpPosef ovrPose;
				ovrPose.Orientation = OculusHMD::ToOvrpQuatf(OutPose.Orientation);
				ovrPose.Position = OculusHMD::ToOvrpVector3f(OutPose.Position);

				OvrAvatarHelpers::OvrPoseToAvatarTransform(handInputState.transform, ovrPose);
			}

			ovrpControllerState4 controllerState;
			if (OVRP_SUCCESS(ovrp_GetControllerState4(RightControllerType, &controllerState)))
			{
				handInputState.isActive = true;
				handInputState.indexTrigger = controllerState.IndexTrigger[ovrpHand_Right];
				handInputState.handTrigger = controllerState.HandTrigger[ovrpHand_Right];
				handInputState.joystickX = controllerState.Thumbstick[ovrpHand_Right].x;
				handInputState.joystickY = controllerState.Thumbstick[ovrpHand_Right].y;

				OvrAvatarHelpers::OvrAvatarParseButtonsAndTouches(controllerState, ovrpHand_Right, handInputState);
			}
		}
	}

	ovrAvatarPose_UpdateBody(Avatar, BodyTransform);
	ovrAvatarPose_UpdateHandsWithType(Avatar, HandInputState[HandType_Left], HandInputState[HandType_Right], ControllerType);
}

void UOvrAvatar::RequestAvatar(
	uint64_t userId,
	ovrAvatarAssetLevelOfDetail lod,
	bool useCombinedBodyMesh)
{
	UE_LOG(LogAvatars, Display, TEXT("[Avatars] RequestAvatar %llu, %d, %d"), userId, lod, useCombinedBodyMesh);

	OnlineUserID = userId;
	LevelOfDetail = lod;

	if (LookAndFeel != ovrAvatarLookAndFeelVersion_Two && useCombinedBodyMesh)
	{
		UE_LOG(LogAvatars, Display, TEXT("[Avatars] RequestAvatar: Combined Body Mesh only compatible with ovrAvatarLookAndFeelVersion_Two"));
	}

	UseCombinedBodyMesh = LookAndFeel == ovrAvatarLookAndFeelVersion_Two && useCombinedBodyMesh;

	auto requestSpec = ovrAvatarSpecificationRequest_Create(userId);
	ovrAvatarSpecificationRequest_SetLookAndFeelVersion(requestSpec, LookAndFeel);
	ovrAvatarSpecificationRequest_SetLevelOfDetail(requestSpec, LevelOfDetail);
	ovrAvatarSpecificationRequest_SetExpressiveFlag(requestSpec, EnableExpressive);
	ovrAvatarSpecificationRequest_SetCombineMeshes(requestSpec, UseCombinedBodyMesh);

	ovrAvatar_RequestAvatarSpecificationFromSpecRequest(requestSpec);
	ovrAvatarSpecificationRequest_Destroy(requestSpec);
}

void UOvrAvatar::UpdateSkeleton(UPoseableMeshComponent& mesh, const ovrAvatarSkinnedMeshPose& pose)
{
	FTransform LocalBone = FTransform::Identity;
	for (uint32 BoneIndex = 0; BoneIndex < pose.jointCount; BoneIndex++)
	{
		OvrAvatarHelpers::OvrAvatarTransformToFTransfrom(pose.jointTransform[BoneIndex], LocalBone);
		mesh.BoneSpaceTransforms[BoneIndex] = LocalBone;
	}

	mesh.MarkRefreshTransformDirty();
}

void UOvrAvatar::UpdateMorphTargets(UPoseableMeshComponent& mesh, const ovrAvatarRenderPart* renderPart)
{
	const uint32_t EXPECTED_BLENDSHAPE_COUNT = 29;
	mesh.ActiveMorphTargets.Empty();
	mesh.MorphTargetWeights.Empty();

	const ovrAvatarBlendShapeParams* pBlendParams = ovrAvatarSkinnedMeshRender_GetBlendShapeParams(renderPart);

	if (BodyBlendShapeNames.Num() >= EXPECTED_BLENDSHAPE_COUNT)
	{
		// min
		int num = BodyBlendShapeNames.Num();
		for (int i = 0; i < num; i++)
		{
			float val = 0.0f;
			if (i < (int)pBlendParams->blendShapeParamCount)
			{
				if (auto MorphTarget = mesh.FindMorphTarget(BodyBlendShapeNames[i]))
				{
					float v = pBlendParams->blendShapeParams[i];
					if (v > 0.f)
					{
						FActiveMorphTarget TargetBlend;
						TargetBlend.MorphTarget = MorphTarget;
						TargetBlend.WeightIndex = i;

						mesh.ActiveMorphTargets.Add(TargetBlend);
						val = v;
					}
				}
			}
			mesh.MorphTargetWeights.Add(val);
		}
	}
}

USceneComponent* UOvrAvatar::DetachHand(HandType hand)
{
	USceneComponent* handComponent = nullptr;

	if (hand >= HandType_Count || AvatarHands[hand].IsValid())
		return handComponent;

	if (auto ScenePtr = RootAvatarComponents.Find(HandNames[hand]))
	{
		if (auto Hand = ScenePtr->Get())
		{
			Hand->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
			RootAvatarComponents.Remove(HandNames[hand]);
			AvatarHands[hand] = Hand;
			handComponent = Hand;
		}
	}

	return handComponent;
}

void UOvrAvatar::ReAttachHand(HandType hand)
{
	if (hand < HandType_Count && AvatarHands[hand].IsValid() && !RootAvatarComponents.Find(HandNames[hand]))
	{
		AvatarHands[hand]->AttachToComponent(GetOwner()->GetRootComponent(), FAttachmentTransformRules::SnapToTargetIncludingScale);
		AvatarHands[hand]->RegisterComponent();
		RootAvatarComponents.Add(HandNames[hand], AvatarHands[hand]);
		AvatarHands[hand] = nullptr;
	}
}

void UOvrAvatar::SetRightHandPose(ovrAvatarHandGesture pose)
{
	if (!Avatar || pose == ovrAvatarHandGesture_Count)
		return;

	ovrAvatar_SetRightHandGesture(Avatar, pose);
}

void UOvrAvatar::SetLeftHandPose(ovrAvatarHandGesture pose)
{
	if (!Avatar || pose == ovrAvatarHandGesture_Count)
		return;

	ovrAvatar_SetLeftHandGesture(Avatar, pose);
}

void UOvrAvatar::SetCustomGesture(HandType hand, ovrAvatarTransform* joints, uint32_t numJoints)
{
	if (!Avatar)
		return;

	switch (hand)
	{
	case HandType::HandType_Left:
		ovrAvatar_SetLeftHandCustomGesture(Avatar, numJoints, joints);
		break;
	case HandType::HandType_Right:
		ovrAvatar_SetRightHandCustomGesture(Avatar, numJoints, joints);
		break;
	default:
		break;
	}
}

void UOvrAvatar::SetControllerVisibility(HandType hand, bool visible)
{
	if (!Avatar)
		return;

	switch (hand)
	{
	case HandType::HandType_Left:
		ovrAvatar_SetLeftControllerVisibility(Avatar, visible);

		break;
	case HandType::HandType_Right:
		ovrAvatar_SetRightControllerVisibility(Avatar, visible);
		break;
	default:
		break;
	}
}

void UOvrAvatar::StartPacketRecording()
{
	if (!Avatar)
		return;

	ovrAvatarPacket_BeginRecording(Avatar);
}

ovrAvatarPacket* UOvrAvatar::EndPacketRecording()
{
	if (!Avatar)
		return nullptr;

	return ovrAvatarPacket_EndRecording(Avatar);
}

void UOvrAvatar::UpdateFromPacket(ovrAvatarPacket* packet, const float time)
{
	if (Avatar && packet && time > 0.f)
	{
		ovrAvatar_UpdatePoseFromPacket(Avatar, packet, time);
	}
}

void UOvrAvatar::UpdateMeshComponent(USceneComponent& mesh, const ovrAvatarTransform& transform)
{
	OvrAvatarHelpers::OvrAvatarTransformToSceneComponent(mesh, transform);
	mesh.SetVisibility(true, true);
}

void UOvrAvatar::UpdateMaterial(UMeshComponent& mesh, const ovrAvatarMaterialState& material)
{
	UMaterialInstanceDynamic* MaterialInstance = Cast<UMaterialInstanceDynamic>(mesh.GetMaterial(0));

	check(MaterialInstance);

	if (auto AlphaTexture = UOvrAvatarManager::Get().FindTexture(material.alphaMaskTextureID))
	{
		MaterialInstance->SetVectorParameterValue(FName("alphaMaskScaleOffset"), OvrAvatarHelpers::OvrAvatarVec4ToLinearColor(material.alphaMaskScaleOffset));
		MaterialInstance->SetTextureParameterValue(FName("alphaMask"), AlphaTexture);
	}

	if (auto NormalTexture = UOvrAvatarManager::Get().FindTexture(material.normalMapTextureID))
	{
		MaterialInstance->SetVectorParameterValue(FName("normalMapScaleOffset"), OvrAvatarHelpers::OvrAvatarVec4ToLinearColor(material.normalMapScaleOffset));
		MaterialInstance->SetTextureParameterValue(FName("normalMap"), NormalTexture);
	}

	if (auto RoughnessTexture = UOvrAvatarManager::Get().FindTexture(material.roughnessMapTextureID))
	{
		MaterialInstance->SetScalarParameterValue(FName("useRoughnessMap"), 1.0f);
		MaterialInstance->SetTextureParameterValue(FName("roughnessMap"), RoughnessTexture);
		MaterialInstance->SetVectorParameterValue(FName("roughnessMapScaleOffset"), OvrAvatarHelpers::OvrAvatarVec4ToLinearColor(material.roughnessMapScaleOffset));
	}
	else
	{
		MaterialInstance->SetScalarParameterValue(FName("useRoughnessMap"), 0.0f);
	}

	MaterialInstance->SetVectorParameterValue(FName("parallaxMapScaleOffset"), OvrAvatarHelpers::OvrAvatarVec4ToLinearColor(material.parallaxMapScaleOffset));
	if (auto ParallaxTexture = UOvrAvatarManager::Get().FindTexture(material.parallaxMapTextureID))
	{
		MaterialInstance->SetTextureParameterValue(FName("parallaxMap"), ParallaxTexture);
	}

	MaterialInstance->SetVectorParameterValue(FName("baseColor"), OvrAvatarHelpers::OvrAvatarVec4ToLinearColor(material.baseColor));
	MaterialInstance->SetScalarParameterValue(FName("baseMaskType"), material.baseMaskType);
	MaterialInstance->SetVectorParameterValue(FName("baseMaskParameters"), OvrAvatarHelpers::OvrAvatarVec4ToLinearColor(material.baseMaskParameters));

	// Converts vector from Oculus to Unreal because of coordinate system difference
	ovrAvatarVector4f baseMaskAxis;
	baseMaskAxis.x = -material.baseMaskAxis.z;
	baseMaskAxis.y = material.baseMaskAxis.x;
	baseMaskAxis.z = material.baseMaskAxis.y;
	baseMaskAxis.w = material.baseMaskAxis.w;
	MaterialInstance->SetVectorParameterValue(FName("baseMaskAxis"), OvrAvatarHelpers::OvrAvatarVec4ToLinearColor(baseMaskAxis));

	for (uint32_t l = 0; l < material.layerCount; ++l)
	{
		FString ParamName;

		ParamName = FString::Printf(TEXT("Layer%u_SamplerMode"), l);
		MaterialInstance->SetScalarParameterValue(FName(*ParamName), material.layers[l].sampleMode);
		ParamName = FString::Printf(TEXT("Layer%u_MaskType"), l);
		MaterialInstance->SetScalarParameterValue(FName(*ParamName), material.layers[l].maskType);
		ParamName = FString::Printf(TEXT("Layer%u_BlendMode"), l);
		MaterialInstance->SetScalarParameterValue(FName(*ParamName), material.layers[l].blendMode);

		ParamName = FString::Printf(TEXT("Layer%u_Color"), l);
		MaterialInstance->SetVectorParameterValue(FName(*ParamName), OvrAvatarHelpers::OvrAvatarVec4ToLinearColor(material.layers[l].layerColor));
		ParamName = FString::Printf(TEXT("Layer%u_SurfaceScaleOffset"), l);
		MaterialInstance->SetVectorParameterValue(FName(*ParamName), OvrAvatarHelpers::OvrAvatarVec4ToLinearColor(material.layers[l].sampleScaleOffset));
		ParamName = FString::Printf(TEXT("Layer%u_SampleParameters"), l);
		MaterialInstance->SetVectorParameterValue(FName(*ParamName), OvrAvatarHelpers::OvrAvatarVec4ToLinearColor(material.layers[l].sampleParameters));

		ParamName = FString::Printf(TEXT("Layer%u_MaskParameters"), l);
		MaterialInstance->SetVectorParameterValue(FName(*ParamName), OvrAvatarHelpers::OvrAvatarVec4ToLinearColor(material.layers[l].maskParameters));
		ParamName = FString::Printf(TEXT("Layer%u_MaskAxis"), l);

		ovrAvatarVector4f layerMaskAxis;
		layerMaskAxis.x = -material.layers[l].maskAxis.z;
		layerMaskAxis.y = material.layers[l].maskAxis.x;
		layerMaskAxis.z = material.layers[l].maskAxis.y;
		layerMaskAxis.w = material.layers[l].maskAxis.w;
		MaterialInstance->SetVectorParameterValue(FName(*ParamName), OvrAvatarHelpers::OvrAvatarVec4ToLinearColor(layerMaskAxis));

		if (auto SampleTexture = UOvrAvatarManager::Get().FindTexture(material.layers[l].sampleTexture))
		{
			ParamName = FString::Printf(TEXT("Layer%u_Surface"), l);
			MaterialInstance->SetTextureParameterValue(FName(*ParamName), SampleTexture);
		}
	}
}

void UOvrAvatar::UpdateMaterialPBR(UPoseableMeshComponent& mesh, const ovrAvatarRenderPart_SkinnedMeshRenderPBS& data)
{
	UMaterialInstanceDynamic* MaterialInstance = Cast<UMaterialInstanceDynamic>(mesh.GetMaterial(0));

	if (auto AlbedoTexture = UOvrAvatarManager::Get().FindTexture(data.albedoTextureAssetID))
	{
		MaterialInstance->SetTextureParameterValue(FName("AlbedoMap"), AlbedoTexture);
	}

	if (auto SurfaceTexture = UOvrAvatarManager::Get().FindTexture(data.surfaceTextureAssetID))
	{
		MaterialInstance->SetTextureParameterValue(FName("SurfaceMap"), SurfaceTexture);
	}
}

void UOvrAvatar::UpdateMaterialPBRV2(UPoseableMeshComponent& mesh, const ovrAvatarRenderPart_SkinnedMeshRenderPBS_V2& data, bool IsBodyMaterial)
{
	UMaterialInstanceDynamic* MaterialInstance = Cast<UMaterialInstanceDynamic>(mesh.GetMaterial(0));

	if (auto AlbedoTexture = UOvrAvatarManager::Get().FindTexture(data.materialState.albedoTextureID))
	{
		static FName AlbedoParamName(TEXT("AlbedoTexture"));
		MaterialInstance->SetTextureParameterValue(AlbedoParamName, AlbedoTexture);
	}

	static FName AlbedoMultiplierParamName(TEXT("AlbedoMultiplier"));
	MaterialInstance->SetVectorParameterValue(AlbedoMultiplierParamName, OvrAvatarHelpers::OvrAvatarVec4ToLinearColor(data.materialState.albedoMultiplier));

	if (auto MetallicnessTexture = UOvrAvatarManager::Get().FindTexture(data.materialState.metallicnessTextureID))
	{
		static FName MetalicnessParamName(TEXT("Roughness"));
		MaterialInstance->SetTextureParameterValue(MetalicnessParamName, MetallicnessTexture);
	}

	if (auto NormalTexture = UOvrAvatarManager::Get().FindTexture(data.materialState.normalTextureID))
	{
		static FName MetalicnessParamName(TEXT("NormalMap"));
		MaterialInstance->SetTextureParameterValue(MetalicnessParamName, NormalTexture);
	}

	if (EnableExpressive)
	{
		SetExpressiveMaterialParamters(MaterialInstance, IsBodyMaterial);
	}
}

void UOvrAvatar::SetExpressiveMaterialParamters(UMaterialInstanceDynamic* MaterialInstance, bool IsBodyMaterial)
{
	ovrAvatarExpressiveParameters params = ovrAvatar_GetExpressiveParameters(Avatar);

	MaterialInstance->OpacityMaskClipValue = 0.f;

	static FName LipSmoothnessParam("LipSmoothness");
	MaterialInstance->SetScalarParameterValue(LipSmoothnessParam, params.lipSmoothness);

	static FName IrisColorParam("IrisColor");
	MaterialInstance->SetVectorParameterValue(IrisColorParam, OvrAvatarHelpers::OvrAvatarVec4ToLinearColor(params.irisColor));

	static FName ScleraColorParam("ScleraColor");
	MaterialInstance->SetVectorParameterValue(ScleraColorParam, OvrAvatarHelpers::OvrAvatarVec4ToLinearColor(params.scleraColor));

	static FName LashColorParam("LashColor");
	MaterialInstance->SetVectorParameterValue(LashColorParam, OvrAvatarHelpers::OvrAvatarVec4ToLinearColor(params.lashColor));

	static FName BrowColorParam("BrowColor");
	MaterialInstance->SetVectorParameterValue(BrowColorParam, OvrAvatarHelpers::OvrAvatarVec4ToLinearColor(params.browColor));

	static FName LipColorParam("LipColor");
	MaterialInstance->SetVectorParameterValue(LipColorParam, OvrAvatarHelpers::OvrAvatarVec4ToLinearColor(params.lipColor));

	static FName TeethColorParam("TeethColor");
	MaterialInstance->SetVectorParameterValue(TeethColorParam, OvrAvatarHelpers::OvrAvatarVec4ToLinearColor(params.teethColor));

	static FName GumColorParam("GumColor");
	MaterialInstance->SetVectorParameterValue(GumColorParam, OvrAvatarHelpers::OvrAvatarVec4ToLinearColor(params.gumColor));
}

UPoseableMeshComponent* UOvrAvatar::CreateMeshComponent(USceneComponent* parent, ovrAvatarAssetID assetID, const FName& name)
{
	UPoseableMeshComponent* MeshComponent = NewObject<UPoseableMeshComponent>(parent->GetOwner(), name);
	MeshComponent->AttachToComponent(parent, FAttachmentTransformRules::SnapToTargetIncludingScale);
	MeshComponent->RegisterComponent();

	MeshComponent->bCastDynamicShadow = false;
	MeshComponent->CastShadow = false;
	MeshComponent->bRenderCustomDepth = false;
	MeshComponent->bRenderInMainPass = true;
	MeshComponent->SetVisibility(false, true);

	AddMeshComponent(assetID, MeshComponent);

	return MeshComponent;
}

UPoseableMeshComponent* UOvrAvatar::CreateDepthMeshComponent(USceneComponent* parent, ovrAvatarAssetID assetID, const FName& name)
{
	UPoseableMeshComponent* MeshComponent = NewObject<UPoseableMeshComponent>(parent->GetOwner(), name);
	MeshComponent->AttachToComponent(parent, FAttachmentTransformRules::SnapToTargetIncludingScale);
	MeshComponent->RegisterComponent();

	MeshComponent->bCastDynamicShadow = false;
	MeshComponent->CastShadow = false;
	MeshComponent->bRenderCustomDepth = true;
	MeshComponent->bRenderInMainPass = false;
	MeshComponent->SetVisibility(false, true);

	AddDepthMeshComponent(assetID, MeshComponent);

	return MeshComponent;
}

void UOvrAvatar::LoadMesh(USkeletalMesh* SkeletalMesh, const ovrAvatarMeshAssetData* data, ovrAvatarAsset* asset, const ovrAvatarAssetID& assetID)
{
#if WITH_EDITOR
	UE_LOG(LogAvatars, Display, TEXT("[Avatars] Loaded Mesh WITH_EDITOR"));
	FSkeletalMeshLODModel* LodRenderData = new FSkeletalMeshLODModel();
	SkeletalMesh->GetImportedModel()->LODModels.Add(LodRenderData);
#else
	UE_LOG(LogAvatars, Display, TEXT("[Avatars] Loaded Mesh"));
	FSkeletalMeshLODRenderData* LodRenderData = new FSkeletalMeshLODRenderData();
	SkeletalMesh->AllocateResourceForRendering();
	SkeletalMesh->GetResourceForRendering()->LODRenderData.Add(LodRenderData);
#endif

	FSkeletalMeshLODInfo& LodInfo = SkeletalMesh->AddLODInfo();
	LodInfo.ScreenSize = 0.3f;
	LodInfo.LODHysteresis = 0.2f;

	SkeletalMesh->RefSkeleton.Empty(data->skinnedBindPose.jointCount);
	LodInfo.BuildSettings.bUseFullPrecisionUVs = true;
	SkeletalMesh->bHasBeenSimplified = false;
	SkeletalMesh->bHasVertexColors = true;

	for (uint32 BoneIndex = 0; BoneIndex < data->skinnedBindPose.jointCount; BoneIndex++)
	{
		LodRenderData->RequiredBones.Add(BoneIndex);
		LodRenderData->ActiveBoneIndices.Add(BoneIndex);

		FString BoneString = data->skinnedBindPose.jointNames[BoneIndex];

		// Not allowed to duplicate bone names...
		static FString RootBoneName = FString(TEXT("root"));
		if (BoneString == RootBoneName)
		{
			BoneString += FString::Printf(TEXT("_%u"), BoneIndex);
		}

		FName BoneName = FName(*BoneString);

		FTransform Transform = FTransform::Identity;
		OvrAvatarHelpers::OvrAvatarTransformToFTransfrom(data->skinnedBindPose.jointTransform[BoneIndex], Transform);

		FReferenceSkeletonModifier Modifier = FReferenceSkeletonModifier(SkeletalMesh->RefSkeleton, nullptr);
		int32 ParentIndex = BoneIndex > 0 && data->skinnedBindPose.jointParents[BoneIndex] < 0 ? 0 : data->skinnedBindPose.jointParents[BoneIndex];
		Modifier.Add(FMeshBoneInfo(BoneName, BoneString, ParentIndex), Transform);
	}

	check(data->indexCount % 3 == 0);
	check(data->vertexCount > 0);

	FBox BoundBox = FBox();
	BoundBox.Init();

	const auto SubmeshCount = ovrAvatarAsset_GetSubmeshCount(asset);
	uint32_t BaseVertexIndex = 0;
	const uint32_t NumBlendWeights = 4;

#if WITH_EDITOR
	LodRenderData->Sections.SetNumUninitialized(SubmeshCount);
#else
	TArray<FColor> ColorArray;
	TArray<TSkinWeightInfo<true>> InWeights;
	InWeights.AddUninitialized(data->vertexCount);
	TMap<int32, TArray<int32>> OverlappingVertices;

	LodRenderData->RenderSections.SetNumUninitialized(SubmeshCount);
	LodRenderData->StaticVertexBuffers.PositionVertexBuffer.Init(data->vertexCount);
	LodRenderData->StaticVertexBuffers.ColorVertexBuffer.Init(data->vertexCount);
	LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.Init(data->vertexCount, 1);
#endif

	UE_LOG(LogAvatars, Display, TEXT("[Mesh] - Vertex Count: %d"), data->vertexCount);
	UE_LOG(LogAvatars, Display, TEXT("[Mesh] - Index Count: %d"), data->indexCount);
	UE_LOG(LogAvatars, Display, TEXT("[Mesh] - Submesh Count: %d"), SubmeshCount);

	for (uint32_t submeshCounter = 0; submeshCounter < SubmeshCount; submeshCounter++)
	{
		const auto submeshLastIndex = ovrAvatarAsset_GetSubmeshLastIndex(asset, submeshCounter);
		const auto submeshFirstIndex = submeshCounter == 0 ? 0 : ovrAvatarAsset_GetSubmeshLastIndex(asset, submeshCounter - 1);

		UE_LOG(LogAvatars, Display, TEXT("[Mesh] - Submesh index: %d First Index: %d"), submeshCounter, submeshFirstIndex);
		UE_LOG(LogAvatars, Display, TEXT("[Mesh] - Submesh index: %d Last Index: %d"), submeshCounter, submeshLastIndex);

		LodInfo.LODMaterialMap.Add(submeshCounter);

		UMaterialInterface* submeshMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
		if (AssetToMaterialStringsMap.Contains(assetID))
		{
			auto MaterialArray = AssetToMaterialStringsMap[assetID];

			if (submeshCounter < (uint32_t)MaterialArray.Num())
			{
				auto Material = (UMaterialInterface*)StaticLoadObject(UMaterial::StaticClass(), NULL, *MaterialArray[submeshCounter]);
				submeshMaterial = UMaterialInstanceDynamic::Create(Material, GetTransientPackage());

				UE_LOG(LogAvatars, Display, TEXT("[Mesh] - Submesh index: %d Material: %s"), submeshCounter, *MaterialArray[submeshCounter]);
			}
		}

		SkeletalMesh->Materials.Add(submeshMaterial);
		SkeletalMesh->Materials[submeshCounter].UVChannelData.bInitialized = true;

#if WITH_EDITOR
		new(&LodRenderData->Sections[submeshCounter]) FSkelMeshSection();
		auto& MeshSection = LodRenderData->Sections[submeshCounter];
#else
		new(&LodRenderData->RenderSections[submeshCounter]) FSkelMeshRenderSection();
		auto& MeshSection = LodRenderData->RenderSections[submeshCounter];
#endif
		MeshSection.MaterialIndex = submeshCounter;
		MeshSection.BaseIndex = submeshFirstIndex;
		MeshSection.NumTriangles = (submeshLastIndex - submeshFirstIndex) / 3;
		MeshSection.BaseVertexIndex = BaseVertexIndex;
		MeshSection.MaxBoneInfluences = NumBlendWeights;
		check((submeshLastIndex - submeshFirstIndex) % 3 == 0);

		TSet<int16_t> uniqueVertIndices;
		for (uint32_t index = submeshFirstIndex; index < submeshLastIndex; index++)
		{
			const int16_t VertIndex = data->indexBuffer[index];

			if (uniqueVertIndices.Contains(VertIndex))
				continue;

			uniqueVertIndices.Add(VertIndex);
		}

		MeshSection.NumVertices = uniqueVertIndices.Num();

#if !WITH_EDITOR
		MeshSection.DuplicatedVerticesBuffer.Init(MeshSection.NumVertices, OverlappingVertices);
#endif

		BaseVertexIndex += MeshSection.NumVertices;

		UE_LOG(LogAvatars, Display, TEXT("[Mesh][MeshSecion] Material Index - %d"), MeshSection.MaterialIndex);
		UE_LOG(LogAvatars, Display, TEXT("[Mesh][MeshSecion] BaseIndex - %d"), MeshSection.BaseIndex);
		UE_LOG(LogAvatars, Display, TEXT("[Mesh][MeshSecion] NumTriangles - %d"), MeshSection.NumTriangles);
		UE_LOG(LogAvatars, Display, TEXT("[Mesh][MeshSecion] BaseVertexIndex - %d"), MeshSection.BaseVertexIndex);
		UE_LOG(LogAvatars, Display, TEXT("[Mesh][MeshSecion] MaxBoneInfluences - %d"), MeshSection.MaxBoneInfluences);
		UE_LOG(LogAvatars, Display, TEXT("[Mesh][MeshSecion] NumVertices - %d"), MeshSection.NumVertices);

		UE_LOG(LogAvatars, Display, TEXT("[Mesh][MeshSecion] Loading Vertices"));

		for (uint32_t VertIndex = 0; VertIndex < data->vertexCount; VertIndex++)
		{
			if (!uniqueVertIndices.Contains(VertIndex))
				continue;

			uniqueVertIndices.Remove(VertIndex);

			const ovrAvatarMeshVertex* SourceVertex = &data->vertexBuffer[VertIndex];

#if WITH_EDITOR
			FSoftSkinVertex ModelVertex;
			ModelVertex.Position = 100.0f * FVector(-SourceVertex->z, SourceVertex->x, SourceVertex->y);
			BoundBox += ModelVertex.Position;

			ModelVertex.Color = UOvrAvatar::GetColorFromVertex(*SourceVertex);


			FVector n = FVector(-SourceVertex->nz, SourceVertex->nx, SourceVertex->ny);
			FVector t = FVector(-SourceVertex->tz, SourceVertex->tx, SourceVertex->ty);
			FVector bt = FVector::CrossProduct(t, n) * FMath::Sign(SourceVertex->tw);
			ModelVertex.TangentX = t;
			ModelVertex.TangentY = bt;
			ModelVertex.TangentZ = n;
			ModelVertex.UVs[0] = FVector2D(SourceVertex->u, SourceVertex->v);

			uint32 RecomputeIndex = -1;
			uint32 RecomputeIndexWeight = 0;

			for (uint32_t BlendIndex = 0; BlendIndex < MAX_TOTAL_INFLUENCES; BlendIndex++)
			{
				ModelVertex.InfluenceWeights[BlendIndex] = BlendIndex < NumBlendWeights ? (uint8_t)(255.9999f*SourceVertex->blendWeights[BlendIndex]) : 0;
				ModelVertex.InfluenceBones[BlendIndex] = BlendIndex < NumBlendWeights ? SourceVertex->blendIndices[BlendIndex] : 0;

				uint32 Weight = ModelVertex.InfluenceWeights[BlendIndex];
				if (Weight > RecomputeIndexWeight)
				{
					RecomputeIndexWeight = Weight;
					RecomputeIndex = BlendIndex;
				}
			}

			uint32 SumExceptRecompute = 0;
			for (uint32_t BlendIndex = 0; BlendIndex < NumBlendWeights; BlendIndex++)
			{
				if (BlendIndex != RecomputeIndex)
				{
					SumExceptRecompute += ModelVertex.InfluenceWeights[BlendIndex];
				}
			}

			ensure(SumExceptRecompute >= 0 && SumExceptRecompute <= 255);
			ModelVertex.InfluenceWeights[RecomputeIndex] = 255 - SumExceptRecompute;

			MeshSection.SoftVertices.Add(ModelVertex);
#else
			FModelVertex ModelVertex;
			ModelVertex.Position = 100.0f * FVector(-SourceVertex->z, SourceVertex->x, SourceVertex->y);
			BoundBox += ModelVertex.Position;

			ColorArray.Add(UOvrAvatar::GetColorFromVertex(*SourceVertex));

			FVector n = FVector(-SourceVertex->nz, SourceVertex->nx, SourceVertex->ny);
			FVector t = FVector(-SourceVertex->tz, SourceVertex->tx, SourceVertex->ty);
			ModelVertex.TangentX = t;
			ModelVertex.TangentZ = n;
			ModelVertex.TexCoord = FVector2D(SourceVertex->u, SourceVertex->v);

			LodRenderData->StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertIndex) = ModelVertex.Position;
			LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(VertIndex, ModelVertex.TangentX, ModelVertex.GetTangentY(), ModelVertex.TangentZ);
			LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(VertIndex, 0, ModelVertex.TexCoord);

			uint32 RecomputeIndex = -1;
			uint32 RecomputeIndexWeight = 0;

			for (uint32_t BlendIndex = 0; BlendIndex < MAX_TOTAL_INFLUENCES; BlendIndex++)
			{
				InWeights[VertIndex].InfluenceWeights[BlendIndex] = BlendIndex < NumBlendWeights ? (uint8_t)(255.9999f*SourceVertex->blendWeights[BlendIndex]) : 0;
				InWeights[VertIndex].InfluenceBones[BlendIndex] = BlendIndex < NumBlendWeights ? SourceVertex->blendIndices[BlendIndex] : 0;

				uint32 Weight = InWeights[VertIndex].InfluenceWeights[BlendIndex];
				if (Weight > RecomputeIndexWeight)
				{
					RecomputeIndexWeight = Weight;
					RecomputeIndex = BlendIndex;
				}
			}

			uint32 SumExceptRecompute = 0;
			for (uint32_t BlendIndex = 0; BlendIndex < NumBlendWeights; BlendIndex++)
			{
				if (BlendIndex != RecomputeIndex)
				{
					SumExceptRecompute += InWeights[VertIndex].InfluenceWeights[BlendIndex];
				}
			}

			ensure(SumExceptRecompute >= 0 && SumExceptRecompute <= 255);
			InWeights[VertIndex].InfluenceWeights[RecomputeIndex] = 255 - SumExceptRecompute;
#endif
		}

		UE_LOG(LogAvatars, Display, TEXT("[Mesh][MeshSecion] Loading Bone Map"));

		for (uint32 BoneIndex = 0; BoneIndex < data->skinnedBindPose.jointCount; BoneIndex++)
		{
			MeshSection.BoneMap.Add(BoneIndex);
		}
	}

	check(BaseVertexIndex == data->vertexCount);

	UE_LOG(LogAvatars, Display, TEXT("[Mesh] Loading Indicies"));

#if WITH_EDITOR
	LodRenderData->NumVertices = data->vertexCount;
	LodRenderData->NumTexCoords = 1;

	for (uint32_t index = 0; index < data->indexCount; index++)
	{
		LodRenderData->IndexBuffer.Add(data->indexBuffer[index]);
	}
#else
	LodRenderData->StaticVertexBuffers.ColorVertexBuffer.InitFromColorArray(ColorArray);
	LodRenderData->SkinWeightVertexBuffer.SetHasExtraBoneInfluences(true);
	LodRenderData->SkinWeightVertexBuffer = InWeights;
	LodRenderData->MultiSizeIndexContainer.CreateIndexBuffer(sizeof(uint16_t));

	for (uint32_t index = 0; index < data->indexCount; index++)
	{
		LodRenderData->MultiSizeIndexContainer.GetIndexBuffer()->AddItem(data->indexBuffer[index]);
	}
#endif

	const uint32_t BlendShapeCount = ovrAvatarAsset_GetMeshBlendShapeCount(asset);
	const ovrAvatarBlendVertex* blendVerts = ovrAvatarAsset_GetMeshBlendShapeVertices(asset);
	int CurrentBlendVert = 0;

	UE_LOG(LogAvatars, Display, TEXT("[Mesh] Loading BlendShapes - %d"), BlendShapeCount);


	for (uint32_t BlendIndex = 0; BlendIndex < BlendShapeCount; BlendIndex++)
	{
		FName BlendName = FName(ovrAvatarAsset_GetMeshBlendShapeName(asset, BlendIndex));

		UE_LOG(LogAvatars, Display, TEXT("[Mesh] Blend Name %s"), *BlendName.ToString());

		UMorphTarget* MorphTarget = NewObject<UMorphTarget>(SkeletalMesh, BlendName);
		FMorphTargetLODModel MorphLODModel;
		MorphLODModel.NumBaseMeshVerts = data->vertexCount;
		MorphLODModel.SectionIndices.Add(0);

		ovrAvatarBlendVertex CurrentVert;
		for (uint32_t VertIndex = 0; VertIndex < data->vertexCount; VertIndex++)
		{
			FMorphTargetDelta NewVertData;
			FMemory::Memcpy((void*)&CurrentVert, (void*)(blendVerts + BlendIndex*data->vertexCount + VertIndex), sizeof(ovrAvatarBlendVertex));

			NewVertData.PositionDelta = 100.0f * FVector(-CurrentVert.z, CurrentVert.x, CurrentVert.y);
			NewVertData.TangentZDelta = FVector(-CurrentVert.nz, CurrentVert.nx, CurrentVert.ny);
			NewVertData.SourceIdx = VertIndex;
			MorphLODModel.Vertices.Add(NewVertData);
		}

		MorphTarget->MorphLODModels.Add(MorphLODModel);
		SkeletalMesh->RegisterMorphTarget(MorphTarget, false);
	}

	FBoxSphereBounds Bounds(BoundBox);
	Bounds = Bounds.ExpandBy(100000.0f);
	SkeletalMesh->SetImportedBounds(Bounds);

#if WITH_EDITOR
	SkeletalMesh->PostEditChange();
#endif

	UE_LOG(LogAvatars, Display, TEXT("[Mesh] PostLoad"));

	SkeletalMesh->Skeleton = NewObject<USkeleton>();
	SkeletalMesh->Skeleton->MergeAllBonesToBoneTree(SkeletalMesh);
	SkeletalMesh->PostLoad();

	UE_LOG(LogAvatars, Display, TEXT("[Mesh] LoadMeshEnd"));
}

void UOvrAvatar::InitializeMaterials()
{
	const uint32_t ComponentCount = ovrAvatarComponent_Count(Avatar);

	for (uint32_t ComponentIndex = 0; ComponentIndex < ComponentCount; ComponentIndex++)
	{
		const ovrAvatarComponent* OvrComponent = ovrAvatarComponent_Get(Avatar, ComponentIndex);
		const bool IsBodyComponent = NativeBodyComponent && NativeBodyComponent->renderComponent == OvrComponent;
		const bool IsLeftHandComponent = NativeLeftHandComponent && NativeLeftHandComponent->renderComponent == OvrComponent;
		const bool IsRightHandComponent = NativeRightHandComponent && NativeRightHandComponent->renderComponent == OvrComponent;

		for (uint32_t RenderIndex = 0; RenderIndex < OvrComponent->renderPartCount; ++RenderIndex)
		{
			const ovrAvatarRenderPart* RenderPart = OvrComponent->renderParts[RenderIndex];

			switch (ovrAvatarRenderPart_GetType(RenderPart))
			{
			case ovrAvatarRenderPartType_SkinnedMeshRender:
			{
				const ovrAvatarRenderPart_SkinnedMeshRender* RenderData = ovrAvatarRenderPart_GetSkinnedMeshRender(RenderPart);
				if (UPoseableMeshComponent* mesh = GetMeshComponent(RenderData->meshAssetID))
				{
					UpdateMaterial(*mesh, RenderData->materialState);
				}
			}
			break;
			case ovrAvatarRenderPartType_SkinnedMeshRenderPBS:
			{
				const ovrAvatarRenderPart_SkinnedMeshRenderPBS* RenderData = ovrAvatarRenderPart_GetSkinnedMeshRenderPBS(RenderPart);
				if (UPoseableMeshComponent* mesh = GetMeshComponent(RenderData->meshAssetID))
				{
					UpdateMaterialPBR(*mesh, *RenderData);
				}
			}
			break;
			case ovrAvatarRenderPartType_SkinnedMeshRenderPBS_V2:
			{
				static float DiffuseIntensityValues[ovrAvatarBodyPartType_Count] = { 0.3f, 0.1f, 0.0f, 0.15f, 0.15f };
				static float RimIntensityValues[ovrAvatarBodyPartType_Count] = { 5.f, 32.f, 2.84f, 4.f, 4.f };

				if (UseCombinedBodyMesh && IsBodyComponent)
				{
					const ovrAvatarRenderPart_SkinnedMeshRenderPBS_V2* RenderData = ovrAvatarRenderPart_GetSkinnedMeshRenderPBSV2(RenderPart);
					if (UPoseableMeshComponent* mesh = GetMeshComponent(RenderData->meshAssetID))
					{
						static FName AlbedoFields[] =
						{
							"Albedo_Body",
							"Albedo_Clothing",
							"Albedo_Visor",
							"Albedo_Hair",
							"Albedo_Beard"
						};

						static FName AlbedoMultiplierFields[] =
						{
							"AlbedoMultiplier_Body",
							"AlbedoMultiplier_Clothing",
							"AlbedoMultiplier_Visor",
							"AlbedoMultiplier_Hair",
							"AlbedoMultiplier_Beard"
						};

						static FName RoughnessFields[] =
						{
							"BodyRoughness",
							"ClothingRoughness",
							"VisorRoughness",
							"HairRoughness",
							"BeardRoughness"
						};

						static FName NormalFields[] =
						{
							"BodyNormal",
							"ClothingNormal",
							"VisorNormal",
							"HairNormal",
							"BeardNormal"
						};

						static FName RedThresholds[] =
						{
							"BodyRedThreshold",
							"ClothingRedThreshold",
							"VisorRedThreshold",
							"HairRedThreshold",
							"BeardRedThreshold"
						};

						static FName TuningParameterFields[] =
						{
							"TuningParamaters_Body",
							"TuningParamaters_Clothing",
							"TuningParamaters_Visor",
							"TuningParamaters_Hair",
							"TuningParamaters_Beard"
						};

						UMaterialInstanceDynamic* MaterialInstance = Cast<UMaterialInstanceDynamic>(mesh->GetMaterial(0));

						uint32_t Count = 0;
						const ovrAvatarPBSMaterialState* MaterialStates = ovrAvatar_GetBodyPBSMaterialStates(RenderPart, &Count);

						for (uint32_t MatIndex = 0; MatIndex < Count; MatIndex++)
						{
							auto matState = MaterialStates[MatIndex];

							const float ThresholdValue = (float)MatIndex * 0.25f + 0.05f;
							MaterialInstance->SetScalarParameterValue(RedThresholds[MatIndex], ThresholdValue);

							if (auto AlbedoTexture = UOvrAvatarManager::Get().FindTexture(matState.albedoTextureID))
							{
								MaterialInstance->SetTextureParameterValue(AlbedoFields[MatIndex], AlbedoTexture);
							}

							MaterialInstance->SetVectorParameterValue(
								AlbedoMultiplierFields[MatIndex],
								OvrAvatarHelpers::OvrAvatarVec4ToLinearColor(matState.albedoMultiplier));

							if (auto MetallicnessTexture = UOvrAvatarManager::Get().FindTexture(matState.metallicnessTextureID))
							{
								MaterialInstance->SetTextureParameterValue(RoughnessFields[MatIndex], MetallicnessTexture);
							}

							if (auto NormalTexture = UOvrAvatarManager::Get().FindTexture(matState.normalTextureID))
							{
								MaterialInstance->SetTextureParameterValue(NormalFields[MatIndex], NormalTexture);
							}
							
							FLinearColor TuningParam{ DiffuseIntensityValues[MatIndex], RimIntensityValues[MatIndex], 0.0f, 0.f };
							MaterialInstance->SetVectorParameterValue(TuningParameterFields[MatIndex], TuningParam);
						}

						if (EnableExpressive)
						{
							SetExpressiveMaterialParamters(MaterialInstance, IsBodyComponent && RenderIndex == 0);
						}
					}
				}
				else
				{
					const ovrAvatarRenderPart_SkinnedMeshRenderPBS_V2* RenderData = ovrAvatarRenderPart_GetSkinnedMeshRenderPBSV2(RenderPart);
					if (UPoseableMeshComponent* mesh = GetMeshComponent(RenderData->meshAssetID))
					{
						UpdateMaterialPBRV2(*mesh, *RenderData, IsBodyComponent && RenderIndex == 0);

						UMaterialInstanceDynamic* MaterialInstance = Cast<UMaterialInstanceDynamic>(mesh->GetMaterial(0));
						ovrAvatarBodyPartType PartType = ovrAvatarRenderPart_GetBodyPartType(Avatar, RenderPart);

						if (MaterialInstance)
						{	
							FLinearColor TuningParam{ 0.f, 0.f, 0.3f, 0.f };

							if (IsBodyComponent && PartType < ovrAvatarBodyPartType_Count)
							{
								TuningParam.R = DiffuseIntensityValues[PartType];
								TuningParam.G = RimIntensityValues[PartType];
								TuningParam.B = 0.f;
								TuningParam.A = 0.f;
							}
							else if (IsLeftHandComponent || IsRightHandComponent)
							{
								TuningParam.R = DiffuseIntensityValues[0];
								TuningParam.G = RimIntensityValues[0];
								TuningParam.B = 0.f;
								TuningParam.A = 0.f;
							}

							static FName TuningParamatersParamName(TEXT("TuningParamaters"));
							MaterialInstance->SetVectorParameterValue(TuningParamatersParamName, TuningParam);
						}

					}
				}
			}
			break;
			default:
				break;
			}
		}
	}
}

void UOvrAvatar::UpdateV2VoiceOffsetParams()
{
	if (!UseV2VoiceVisualization)
	{
		return;
	}

	if (auto BodyMesh = GetMeshComponent(BodyMeshID))
	{
		UpdateVoiceVizOnMesh(BodyMesh);
	}

	if (auto DepthMesh = GetDepthMeshComponent(BodyMeshID))
	{
		UpdateVoiceVizOnMesh(DepthMesh);
	}
}

void UOvrAvatar::UpdateVoiceVizOnMesh(UPoseableMeshComponent* Mesh)
{
	static const FName VoiceScaleParam(TEXT("VoiceScale"));
	static const FName VoiceDirectionParam(TEXT("VoiceDirection"));
	static const FName VoicePositionParam(TEXT("VoicePosition"));
	static const FName VoiceComponentScaleParam(TEXT("VoiceComponentScale"));

	static const FVector4 MOUTH_POSITION_OFFSET = FVector4(10.51, 0.f, -1.4f, 0.f);
	static const float MOUTH_SCALE = 0.7f;
	static const float MOUTH_MAX = 0.7f;
	static const int32 NECK_JOINT = 4;
	static const FVector4 UP(0.f, 0.f, 1.f, 0.f);

	auto parentTransform = Mesh->GetAttachParent();
	auto scale = parentTransform->GetComponentScale();
	Mesh->GetBoneTransform(NECK_JOINT).TransformFVector4(FVector::UpVector);
	if (UMaterialInstanceDynamic* MaterialInstance = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0)))
	{
		auto NeckJoint = Mesh->GetBoneTransform(NECK_JOINT);
		FVector transUp = NeckJoint.TransformFVector4(UP);
		transUp.Normalize();

		auto direction = FLinearColor(transUp);
		auto position = NeckJoint.TransformFVector4(MOUTH_POSITION_OFFSET);
		auto neckPosition = NeckJoint.GetTranslation();

		MaterialInstance->SetVectorParameterValue(
			VoicePositionParam,
			FLinearColor(neckPosition + position));


		MaterialInstance->SetVectorParameterValue(
			VoiceDirectionParam,
			FLinearColor(direction));

		FTransform mouthPos;
		mouthPos.SetRotation(NeckJoint.GetRotation());
		mouthPos.SetTranslation(neckPosition + position);

		const float appliedValue = FMath::Min(scale.Z * MOUTH_MAX, scale.Z * VoiceVisualValue * MOUTH_SCALE);
		MaterialInstance->SetScalarParameterValue(VoiceScaleParam, appliedValue);

		// Assume Uniform Scale, it's going to be messed up anyway if not
		MaterialInstance->SetScalarParameterValue(VoiceComponentScaleParam, scale.Z);
	}
}

const FString& UOvrAvatar::GetPBRV2BodyMaterial(bool UseCombinedMesh)
{
	if (EnableExpressive)
	{
		switch (BodyMaterial)
		{
		case MaterialType::Masked:
			return UseCombinedMesh ? ExpressiveCombinedMasked : ExpressiveMaskedBody;
		case MaterialType::Translucent:
			return UseCombinedMesh ? ExpressiveCombinedAlpha : ExpressiveAlphaBody;
		case MaterialType::Opaque:
			return UseCombinedMesh ? ExpressiveCombinedOpaque : ExpressiveOpaqueBody;
		}
	}

	return UseCombinedMesh ? Combined : Single;
}

const FString& UOvrAvatar::GetPBRV2HandMaterial()
{
	if (EnableExpressive)
	{
		switch (HandMaterial)
		{
		case MaterialType::Masked:
			return ExpressiveMaskedSimple;
		case MaterialType::Translucent:
			return ExpressiveAlphaSimple;
		case MaterialType::Opaque:
			return ExpressiveOpaqueSimple;
		}
	}

	return Single;
}

const FString& UOvrAvatar::GetPBRV2Material()
{
	if (EnableExpressive)
	{
		switch (BodyMaterial)
		{
		case MaterialType::Masked:
			return ExpressiveMaskedSimple;
		case MaterialType::Translucent:
			return ExpressiveAlphaSimple;
		case MaterialType::Opaque:
			return ExpressiveOpaqueSimple;
		}
	}

	return Single;
}

const FString& UOvrAvatar::GetPBRV2ControllerMaterial()
{
	return ExpressiveController;
}

const FString& UOvrAvatar::GetPBRV2EyeWearMaterial()
{
	if (EnableExpressive)
	{
		return BodyMaterial != MaterialType::Translucent ? ExpressiveAlphaSimple : ExpressiveOpaqueSimple;
	}

	return GetPBRV2Material();
}

ovrAvatarControllerType UOvrAvatar::GetControllerTypeByHardware()
{
	if (!UOvrAvatarManager::Get().IsOVRPluginValid())
		return ovrAvatarControllerType_Touch;

	ovrAvatarControllerType controllerType = ovrAvatarControllerType_Touch;

	ovrpSystemHeadset hmdType = ovrpSystemHeadset_None;
	ovrpResult result = ovrp_GetSystemHeadsetType2(&hmdType);

	if (result != ovrpSuccess)
	{
		UE_LOG(LogAvatars, Warning, TEXT("GetControllerTypeByHardware: ovrp_GetSystemHeadsetType2 failed %d"), result);
	}

	switch (hmdType)
	{
	case ovrpSystemHeadset_GearVR_R320:
	case ovrpSystemHeadset_GearVR_R321:
	case ovrpSystemHeadset_GearVR_R322:
	case ovrpSystemHeadset_GearVR_R323:
	case ovrpSystemHeadset_GearVR_R324:
	case ovrpSystemHeadset_GearVR_R325:
		controllerType = ovrAvatarControllerType_Malibu;
		break;
	case ovrpSystemHeadset_Oculus_Go:
		controllerType = ovrAvatarControllerType_Go;
		break;
	case ovrpSystemHeadset_Oculus_Quest:
	case ovrpSystemHeadset_Rift_S:
		controllerType = ovrAvatarControllerType_Quest;
		break;
	case ovrpSystemHeadset_Rift_DK1:
	case ovrpSystemHeadset_Rift_DK2:
	case ovrpSystemHeadset_Rift_CV1:
	case ovrpSystemHeadset_Rift_CB:
	default:
		controllerType = ovrAvatarControllerType_Touch;
		break;
	}

	return controllerType;
}

bool UOvrAvatar::Is3DOFHardware()
{
	if (!UOvrAvatarManager::Get().IsOVRPluginValid())
		return false;

	ovrpSystemHeadset hmdType = ovrpSystemHeadset_None;
	ovrpResult result = ovrp_GetSystemHeadsetType2(&hmdType);

	if (result != ovrpSuccess)
	{
		UE_LOG(LogAvatars, Warning, TEXT("GetControllerTypeByHardware: ovrp_GetSystemHeadsetType2 failed %d"), result);
	}

	switch (hmdType)
	{
	case ovrpSystemHeadset_GearVR_R320:
	case ovrpSystemHeadset_GearVR_R321:
	case ovrpSystemHeadset_GearVR_R322:
	case ovrpSystemHeadset_GearVR_R323:
	case ovrpSystemHeadset_GearVR_R324:
	case ovrpSystemHeadset_GearVR_R325:
	case ovrpSystemHeadset_Oculus_Go:
		return true;
	case ovrpSystemHeadset_Oculus_Quest:
	case ovrpSystemHeadset_Rift_DK1:
	case ovrpSystemHeadset_Rift_DK2:
	case ovrpSystemHeadset_Rift_CV1:
	case ovrpSystemHeadset_Rift_CB:
	default:
		return false;
	}

	return false;
}

void UOvrAvatar::UpdateVisemeValues(const TArray<float>& visemes, const float laughterScore)
{
	FScopeLock Lock(&VisemeMutex);

	int32 VisemeCount = FMath::Min(OVR_AVATAR_MAXIMUM_VISEME_PARAM_COUNT - 1, visemes.Num());

	for (int i = 0; i < VisemeCount; i++)
	{
		VisemeValues.visemeParams[i] = visemes[i] * GAvatarVisemeMultiplier;
	}

	VisemeValues.visemeParams[VisemeCount] = laughterScore * GAvatarVisemeMultiplier;
	VisemeValues.visemeParamCount = VisemeCount + 1;
}

void UOvrAvatar::UpdateHeadGazeTarget()
{
	static const int32 EYE_JOINT = 5;

	if (AvatarHeadTarget.IsValid())
	{
		if (UPoseableMeshComponent* BodyMesh = GetMeshComponent(BodyMeshID))
		{
			FTransform NeckJoint = BodyMesh->GetBoneTransform(EYE_JOINT);
			AvatarHeadTarget->SetAvatarHeadTransform(NeckJoint);
		}
	}
}

void  UOvrAvatar::SetHandMaterial(MaterialType type)
{ 
#if PLATFORM_ANDROID
	HandMaterial = type == MaterialType::Translucent ? MaterialType::Masked : type;
#else
	HandMaterial = type; 
#endif
}

void  UOvrAvatar::SetBodyMaterial(MaterialType type) 
{ 
#if PLATFORM_ANDROID
	BodyMaterial = type == MaterialType::Translucent ? MaterialType::Masked : type;
#else
	BodyMaterial = type;
#endif
}

void UOvrAvatar::LoadCombinedMesh(USkeletalMesh* SkeletalMesh, const ovrAvatarMeshAssetDataV2* data, ovrAvatarAsset* asset, const ovrAvatarAssetID& assetID)
{
#if WITH_EDITOR
	UE_LOG(LogAvatars, Display, TEXT("[Avatars] Loaded Mesh WITH_EDITOR."));

	FSkeletalMeshLODModel* LodModel = new FSkeletalMeshLODModel();
	SkeletalMesh->GetImportedModel()->LODModels.Add(LodModel);

	FSkeletalMeshLODInfo& LodInfo = SkeletalMesh->AddLODInfo();

	LodInfo.ScreenSize = 0.3f;
	LodInfo.LODHysteresis = 0.2f;
	SkeletalMesh->RefSkeleton.Empty(data->skinnedBindPose.jointCount);

	LodInfo.BuildSettings.bUseFullPrecisionUVs = true;
	SkeletalMesh->bHasBeenSimplified = false;
	SkeletalMesh->bHasVertexColors = true;

	for (uint32 BoneIndex = 0; BoneIndex < data->skinnedBindPose.jointCount; BoneIndex++)
	{
		LodModel->RequiredBones.Add(BoneIndex);
		LodModel->ActiveBoneIndices.Add(BoneIndex);

		FString BoneString = data->skinnedBindPose.jointNames[BoneIndex];

		// Not allowed to duplicate bone names...
		static FString RootBoneName = FString(TEXT("root"));
		if (BoneString == RootBoneName)
		{
			BoneString += FString::Printf(TEXT("_%u"), BoneIndex);
		}

		FName BoneName = FName(*BoneString);

		FTransform Transform = FTransform::Identity;
		OvrAvatarHelpers::OvrAvatarTransformToFTransfrom(data->skinnedBindPose.jointTransform[BoneIndex], Transform);

		FReferenceSkeletonModifier Modifier = FReferenceSkeletonModifier(SkeletalMesh->RefSkeleton, nullptr);
		int32 ParentIndex = BoneIndex > 0 && data->skinnedBindPose.jointParents[BoneIndex] < 0 ? 0 : data->skinnedBindPose.jointParents[BoneIndex];
		Modifier.Add(FMeshBoneInfo(BoneName, BoneString, ParentIndex), Transform);
	}

	check(data->indexCount % 3 == 0);
	check(data->vertexCount > 0);

	FBox BoundBox = FBox();
	BoundBox.Init();

	const auto SubmeshCount = ovrAvatarAsset_GetSubmeshCount(asset);
	uint32_t BaseVertexIndex = 0;

	LodModel->Sections.SetNumUninitialized(SubmeshCount);
	uint32_t MeshVertexCountAfterSubmeshes = 0;

	bool SumMeshVertexOffsetFlag = false;
	uint32_t submeshFirstIndex = 0;
	for (uint32_t submeshCounter = 0; submeshCounter < SubmeshCount; submeshCounter++)
	{
		const auto submeshLastIndex = ovrAvatarAsset_GetSubmeshLastIndex(asset, submeshCounter);
		submeshFirstIndex = submeshCounter == 0 ? 0 : ovrAvatarAsset_GetSubmeshLastIndex(asset, submeshCounter - 1);
		const uint32_t NumBlendWeights = 4;

		LodInfo.LODMaterialMap.Add(submeshCounter);

		UMaterialInterface* submeshMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
		if (AssetToMaterialStringsMap.Contains(assetID))
		{
			auto MaterialArray = AssetToMaterialStringsMap[assetID];

			if (submeshCounter < (uint32_t)MaterialArray.Num())
			{
				auto Material = (UMaterialInterface*)StaticLoadObject(UMaterial::StaticClass(), NULL, *MaterialArray[submeshCounter]);
				submeshMaterial = UMaterialInstanceDynamic::Create(Material, GetTransientPackage());
			}
		}

		SkeletalMesh->Materials.Add(submeshMaterial);
		SkeletalMesh->Materials[submeshCounter].UVChannelData.bInitialized = true;

		new(&LodModel->Sections[submeshCounter]) FSkelMeshSection();
		auto& MeshSection = LodModel->Sections[submeshCounter];
		MeshSection.MaterialIndex = submeshCounter;
		MeshSection.BaseIndex = submeshFirstIndex;
		MeshSection.NumTriangles = (submeshLastIndex - submeshFirstIndex) / 3;
		MeshSection.BaseVertexIndex = BaseVertexIndex;
		MeshSection.MaxBoneInfluences = NumBlendWeights;
		check((submeshLastIndex - submeshFirstIndex) % 3 == 0);

		TSet<int16_t> uniqueVertIndices;
		for (uint32_t index = submeshFirstIndex; index < submeshLastIndex; index++)
		{
			const int16_t VertIndex = data->indexBuffer[index];

			if (uniqueVertIndices.Contains(VertIndex))
				continue;

			uniqueVertIndices.Add(VertIndex);
		}

		const uint32_t TotalSubmeshVertCount = uniqueVertIndices.Num();
		MeshSection.NumVertices = submeshCounter == 0 ? data->vertexCount : uniqueVertIndices.Num();
		BaseVertexIndex += MeshSection.NumVertices;

		UE_LOG(LogAvatars, Display, TEXT("[Mesh][MeshSecion] Material Index - %d"), MeshSection.MaterialIndex);
		UE_LOG(LogAvatars, Display, TEXT("[Mesh][MeshSecion] BaseIndex - %d"), MeshSection.BaseIndex);
		UE_LOG(LogAvatars, Display, TEXT("[Mesh][MeshSecion] NumTriangles - %d"), MeshSection.NumTriangles);
		UE_LOG(LogAvatars, Display, TEXT("[Mesh][MeshSecion] BaseVertexIndex - %d"), MeshSection.BaseVertexIndex);
		UE_LOG(LogAvatars, Display, TEXT("[Mesh][MeshSecion] MaxBoneInfluences - %d"), MeshSection.MaxBoneInfluences);
		UE_LOG(LogAvatars, Display, TEXT("[Mesh][MeshSecion] NumVertices - %d"), MeshSection.NumVertices);

		UE_LOG(LogAvatars, Display, TEXT("[Mesh][MeshSecion] Loading Vertices"));

		for (uint32_t VertIndex = 0; VertIndex < data->vertexCount; VertIndex++)
		{
			if (!uniqueVertIndices.Contains(VertIndex))
			{
				// This caches the offset set we need to apply to the eye submesh indices
				// We will be adding this many verticies between where the eye mesh verts used to be in the array and where they will get loaded into
				// Combined mesh vertex layout
				// -----------------------------------------------------------------------------------
				// | Body Submesh 0 | Body Submesh 1 (eye submesh) | Clothing | Visor | Hair | Beard |
				// |-----------------------------------------------------------------------------------
				// 
				// Now Becomes: Eye submesh is only 238 verts.
				// -----------------------------------------------------------------------------------
				// | Body Submesh 0 | Body Submesh 1 (eye submesh) | Clothing | Visor | Hair | Beard | Body Submesh 1 (eye submesh) |
				// |-----------------------------------------------------------------------------------
				// So we need to update the index values for the eye submesh

				if (!SumMeshVertexOffsetFlag)
				{
					MeshVertexCountAfterSubmeshes = TotalSubmeshVertCount - VertIndex;
					SumMeshVertexOffsetFlag = true;
				}

				// Just load the eye verts so we don't have to mess with the blend shapes, small number
				// They won't have any triangles in the index buffer
				if (submeshCounter > 0)
				{
					continue;
				}
			}

			const ovrAvatarMeshVertexV2* SourceVertex = &data->vertexBuffer[VertIndex];

			FSoftSkinVertex DestVertex;
			DestVertex.Position = 100.0f * FVector(-SourceVertex->z, SourceVertex->x, SourceVertex->y);
			DestVertex.Color = UOvrAvatar::GetColorFromVertex(*SourceVertex);

			BoundBox += DestVertex.Position;

			FVector n = FVector(-SourceVertex->nz, SourceVertex->nx, SourceVertex->ny);
			FVector t = FVector(-SourceVertex->tz, SourceVertex->tx, SourceVertex->ty);
			FVector bt = FVector::CrossProduct(t, n) * FMath::Sign(SourceVertex->tw);
			DestVertex.TangentX = t;
			DestVertex.TangentY = bt;
			DestVertex.TangentZ = n;
			DestVertex.UVs[0] = FVector2D(SourceVertex->u, SourceVertex->v);

			uint32 RecomputeIndex = -1;
			uint32 RecomputeIndexWeight = 0;

			for (uint32_t BlendIndex = 0; BlendIndex < MAX_TOTAL_INFLUENCES; BlendIndex++)
			{
				DestVertex.InfluenceWeights[BlendIndex] = BlendIndex < NumBlendWeights ? (uint8_t)(255.9999f*SourceVertex->blendWeights[BlendIndex]) : 0;
				DestVertex.InfluenceBones[BlendIndex] = BlendIndex < NumBlendWeights ? SourceVertex->blendIndices[BlendIndex] : 0;

				uint32 Weight = DestVertex.InfluenceWeights[BlendIndex];
				if (Weight > RecomputeIndexWeight)
				{
					RecomputeIndexWeight = Weight;
					RecomputeIndex = BlendIndex;
				}
			}

			uint32 SumExceptRecompute = 0;
			for (uint32_t BlendIndex = 0; BlendIndex < NumBlendWeights; BlendIndex++)
			{
				if (BlendIndex != RecomputeIndex)
				{
					SumExceptRecompute += DestVertex.InfluenceWeights[BlendIndex];
				}
			}

			ensure(SumExceptRecompute >= 0 && SumExceptRecompute <= 255);
			DestVertex.InfluenceWeights[RecomputeIndex] = 255 - SumExceptRecompute;

			MeshSection.SoftVertices.Add(DestVertex);
		}

		UE_LOG(LogAvatars, Display, TEXT("[Mesh][MeshSecion] Loading Bone Map"));

		for (uint32 BoneIndex = 0; BoneIndex < data->skinnedBindPose.jointCount; BoneIndex++)
		{
			MeshSection.BoneMap.Add(BoneIndex);
		}
	}

	uint32_t TotalEyeMeshVertCount = SubmeshCount > 1 ? LodModel->Sections[1].NumVertices : 0;

	LodModel->NumVertices = data->vertexCount + TotalEyeMeshVertCount;
	LodModel->NumTexCoords = 1;

	for (uint32_t index = 0; index < data->indexCount; index++)
	{
		const auto IndexValue = index >= submeshFirstIndex ? data->indexBuffer[index] + MeshVertexCountAfterSubmeshes + TotalEyeMeshVertCount : data->indexBuffer[index];
		LodModel->IndexBuffer.Add(IndexValue);
	}

	const uint32_t BlendShapeCount = ovrAvatarAsset_GetMeshBlendShapeCount(asset);
	const ovrAvatarBlendVertex* blendVerts = ovrAvatarAsset_GetMeshBlendShapeVertices(asset);
	int CurrentBlendVert = 0;

	for (uint32_t BlendIndex = 0; BlendIndex < BlendShapeCount; BlendIndex++)
	{
		FName BlendName = FName(ovrAvatarAsset_GetMeshBlendShapeName(asset, BlendIndex));
		UMorphTarget* MorphTarget = NewObject<UMorphTarget>(SkeletalMesh, BlendName);
		FMorphTargetLODModel MorphLODModel;
		MorphLODModel.NumBaseMeshVerts = data->vertexCount + TotalEyeMeshVertCount;
		MorphLODModel.SectionIndices.Add(0);

		const uint32_t BlendShapeSetOffset = BlendIndex * data->vertexCount;

		for (uint32_t VertIndex = 0; VertIndex < data->vertexCount; VertIndex++)
		{
			FMorphTargetDelta NewVertData;
			CurrentBlendVert = BlendShapeSetOffset + VertIndex;
			NewVertData.PositionDelta = 100.0f * FVector(-blendVerts[CurrentBlendVert].z, blendVerts[CurrentBlendVert].x, blendVerts[CurrentBlendVert].y);
			NewVertData.TangentZDelta = FVector(-blendVerts[CurrentBlendVert].nz, blendVerts[CurrentBlendVert].nx, blendVerts[CurrentBlendVert].ny);
			NewVertData.SourceIdx = VertIndex;
			MorphLODModel.Vertices.Add(NewVertData);
		}

		// Copy the dead eye submesh verts to match vert count
		for (uint32_t VertIndex = data->vertexCount; VertIndex < data->vertexCount + TotalEyeMeshVertCount; VertIndex++)
		{
			FMorphTargetDelta NewVertData;
			NewVertData.PositionDelta = 100.0f * FVector(0.f, 0.f, 0.f);
			NewVertData.TangentZDelta = FVector(0.f, 0.f, 0.f);
			NewVertData.SourceIdx = VertIndex;
			MorphLODModel.Vertices.Add(NewVertData);
		}

		MorphTarget->MorphLODModels.Add(MorphLODModel);
		SkeletalMesh->RegisterMorphTarget(MorphTarget, false);
	}

	FBoxSphereBounds Bounds(BoundBox);
	Bounds = Bounds.ExpandBy(100000.0f);
	SkeletalMesh->SetImportedBounds(Bounds);
	SkeletalMesh->PostEditChange();

	SkeletalMesh->Skeleton = NewObject<USkeleton>();
	SkeletalMesh->Skeleton->MergeAllBonesToBoneTree(SkeletalMesh);
	SkeletalMesh->PostLoad();
#else
	UE_LOG(LogAvatars, Display, TEXT("[Avatars] Loaded Mesh."));

	FSkeletalMeshLODRenderData* LodRenderData = new FSkeletalMeshLODRenderData();
	SkeletalMesh->AllocateResourceForRendering();
	SkeletalMesh->GetResourceForRendering()->LODRenderData.Add(LodRenderData);

	FSkeletalMeshLODInfo& LodInfo = SkeletalMesh->AddLODInfo();
	LodInfo.ScreenSize = 0.3f;
	LodInfo.LODHysteresis = 0.2f;
	LodInfo.BuildSettings.bUseFullPrecisionUVs = true;

	SkeletalMesh->RefSkeleton.Empty(data->skinnedBindPose.jointCount);
	SkeletalMesh->bHasBeenSimplified = false;
	SkeletalMesh->bHasVertexColors = true;

	for (uint32 BoneIndex = 0; BoneIndex < data->skinnedBindPose.jointCount; BoneIndex++)
	{
		LodRenderData->RequiredBones.Add(BoneIndex);
		LodRenderData->ActiveBoneIndices.Add(BoneIndex);
		FString BoneString = data->skinnedBindPose.jointNames[BoneIndex];

		// Not allowed to duplicate bone names...
		static FString RootBoneName = FString(TEXT("root"));
		if (BoneString == RootBoneName)
		{
			BoneString += FString::Printf(TEXT("_%u"), BoneIndex);
		}

		FName BoneName = FName(*BoneString);

		FTransform Transform = FTransform::Identity;
		OvrAvatarHelpers::OvrAvatarTransformToFTransfrom(data->skinnedBindPose.jointTransform[BoneIndex], Transform);

		FReferenceSkeletonModifier Modifier = FReferenceSkeletonModifier(SkeletalMesh->RefSkeleton, nullptr);
		int32 ParentIndex = BoneIndex > 0 && data->skinnedBindPose.jointParents[BoneIndex] < 0 ? 0 : data->skinnedBindPose.jointParents[BoneIndex];
		Modifier.Add(FMeshBoneInfo(BoneName, BoneString, ParentIndex), Transform);
	}

	check(data->indexCount % 3 == 0);
	check(data->vertexCount > 0);

	FBox BoundBox = FBox();
	BoundBox.Init();

	const auto SubmeshCount = ovrAvatarAsset_GetSubmeshCount(asset);
	uint32_t BaseVertexIndex = 0;
	const uint32_t NumBlendWeights = 4;

	if (SubmeshCount > 1)
	{
		UE_LOG(LogAvatars, Display, TEXT("Loading more than 1 submesh"));
	}

	TArray<TSet<int16_t>> uniqueVertIndicesArray;

	for (uint32_t submeshCounter = 0; submeshCounter < SubmeshCount; submeshCounter++)
	{
		const auto submeshEndIndex = ovrAvatarAsset_GetSubmeshLastIndex(asset, submeshCounter);
		const auto submeshStartIndex = submeshCounter == 0 ? 0 : ovrAvatarAsset_GetSubmeshLastIndex(asset, submeshCounter - 1);

		TSet<int16_t> uniqueVertIndices;
		for (uint32_t index = submeshStartIndex; index < submeshEndIndex; index++)
		{
			const int16_t VertIndex = data->indexBuffer[index];

			if (uniqueVertIndices.Contains(VertIndex))
				continue;

			uniqueVertIndices.Add(VertIndex);
		}

		uniqueVertIndicesArray.Add(uniqueVertIndices);
	}

	const uint32_t TotalVertCount = SubmeshCount > 1 ? data->vertexCount + uniqueVertIndicesArray[1].Num() : data->vertexCount;
	LodRenderData->RenderSections.SetNumUninitialized(SubmeshCount);
	LodRenderData->StaticVertexBuffers.PositionVertexBuffer.Init(TotalVertCount);
	LodRenderData->StaticVertexBuffers.ColorVertexBuffer.Init(TotalVertCount);
	LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.Init(TotalVertCount, 1);

	UE_LOG(LogAvatars, Display, TEXT("[Mesh] - Vertex Count: %d"), data->vertexCount);
	UE_LOG(LogAvatars, Display, TEXT("[Mesh] - Index Count: %d"), data->indexCount);
	UE_LOG(LogAvatars, Display, TEXT("[Mesh] - Submesh Count: %d"), SubmeshCount);

	TArray<FColor> ColorArray;
	TArray<TSkinWeightInfo<true>> InWeights;
	InWeights.AddUninitialized(TotalVertCount);
	TMap<int32, TArray<int32>> OverlappingVertices;
	bool SumMeshVertexOffsetFlag = false;
	uint32_t MeshVertexCountAfterSubmeshes = 0;
	uint32_t submeshFirstIndex = 0;
	uint32_t CurrentVertexBufferCount = 0;

	for (uint32_t submeshCounter = 0; submeshCounter < SubmeshCount; submeshCounter++)
	{
		const auto submeshLastIndex = ovrAvatarAsset_GetSubmeshLastIndex(asset, submeshCounter);
		submeshFirstIndex = submeshCounter == 0 ? 0 : ovrAvatarAsset_GetSubmeshLastIndex(asset, submeshCounter - 1);

		UE_LOG(LogAvatars, Display, TEXT("[Mesh] - Submesh index: %d First Index: %d"), submeshCounter, submeshFirstIndex);
		UE_LOG(LogAvatars, Display, TEXT("[Mesh] - Submesh index: %d Last Index: %d"), submeshCounter, submeshLastIndex);

		LodInfo.LODMaterialMap.Add(submeshCounter);

		UMaterialInterface* submeshMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
		if (AssetToMaterialStringsMap.Contains(assetID))
		{
			auto MaterialArray = AssetToMaterialStringsMap[assetID];

			if (submeshCounter < (uint32_t)MaterialArray.Num())
			{
				auto Material = (UMaterialInterface*)StaticLoadObject(UMaterial::StaticClass(), NULL, *MaterialArray[submeshCounter]);
				submeshMaterial = UMaterialInstanceDynamic::Create(Material, GetTransientPackage());

				UE_LOG(LogAvatars, Display, TEXT("[Mesh] - Submesh index: %d Material: %s"), submeshCounter, *MaterialArray[submeshCounter]);
			}
		}

		SkeletalMesh->Materials.Add(submeshMaterial);
		SkeletalMesh->Materials[submeshCounter].UVChannelData.bInitialized = true;

		new(&LodRenderData->RenderSections[submeshCounter]) FSkelMeshRenderSection();
		auto& MeshSection = LodRenderData->RenderSections[submeshCounter];

		for (uint32 BoneIndex = 0; BoneIndex < data->skinnedBindPose.jointCount; BoneIndex++)
		{
			MeshSection.BoneMap.Add(BoneIndex);
		}

		MeshSection.MaterialIndex = submeshCounter;
		MeshSection.BaseIndex = submeshFirstIndex;
		MeshSection.NumTriangles = (submeshLastIndex - submeshFirstIndex) / 3;
		MeshSection.BaseVertexIndex = BaseVertexIndex;
		MeshSection.MaxBoneInfluences = NumBlendWeights;
		check((submeshLastIndex - submeshFirstIndex) % 3 == 0);

		TSet<int16_t>& uniqueVertIndices = uniqueVertIndicesArray[submeshCounter];
		const uint32_t TotalSubmeshVertCount = uniqueVertIndices.Num();
		MeshSection.NumVertices = submeshCounter == 0 ? data->vertexCount : uniqueVertIndices.Num();
		MeshSection.DuplicatedVerticesBuffer.Init(MeshSection.NumVertices, OverlappingVertices);
		BaseVertexIndex += MeshSection.NumVertices;

		for (uint32_t VertIndex = 0; VertIndex < data->vertexCount; VertIndex++)
		{
			if (!uniqueVertIndices.Contains(VertIndex))
			{
				if (!SumMeshVertexOffsetFlag)
				{
					MeshVertexCountAfterSubmeshes = TotalSubmeshVertCount - VertIndex;
					SumMeshVertexOffsetFlag = true;
				}

				if (submeshCounter > 0)
				{
					continue;
				}
			}

			const ovrAvatarMeshVertexV2* SourceVertex = &data->vertexBuffer[VertIndex];

			FModelVertex ModelVertex;
			ModelVertex.Position = 100.0f * FVector(-SourceVertex->z, SourceVertex->x, SourceVertex->y);
			BoundBox += ModelVertex.Position;

			ColorArray.Add(UOvrAvatar::GetColorFromVertex(*SourceVertex));

			FVector n = FVector(-SourceVertex->nz, SourceVertex->nx, SourceVertex->ny);
			FVector t = FVector(-SourceVertex->tz, SourceVertex->tx, SourceVertex->ty);
			ModelVertex.TangentX = t;
			ModelVertex.TangentZ = n;
			ModelVertex.TexCoord = FVector2D(SourceVertex->u, SourceVertex->v);

			LodRenderData->StaticVertexBuffers.PositionVertexBuffer.VertexPosition(CurrentVertexBufferCount) = ModelVertex.Position;
			LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(CurrentVertexBufferCount, ModelVertex.TangentX, ModelVertex.GetTangentY(), ModelVertex.TangentZ);
			LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(CurrentVertexBufferCount, 0, ModelVertex.TexCoord);

			uint32 RecomputeIndex = -1;
			uint32 RecomputeIndexWeight = 0;

			for (uint32_t BlendIndex = 0; BlendIndex < MAX_TOTAL_INFLUENCES; BlendIndex++)
			{
				InWeights[CurrentVertexBufferCount].InfluenceWeights[BlendIndex] = BlendIndex < NumBlendWeights ? (uint8_t)(255.9999f*SourceVertex->blendWeights[BlendIndex]) : 0;
				InWeights[CurrentVertexBufferCount].InfluenceBones[BlendIndex] = BlendIndex < NumBlendWeights ? SourceVertex->blendIndices[BlendIndex] : 0;

				uint32 Weight = InWeights[CurrentVertexBufferCount].InfluenceWeights[BlendIndex];
				if (Weight > RecomputeIndexWeight)
				{
					RecomputeIndexWeight = Weight;
					RecomputeIndex = BlendIndex;
				}
			}

			uint32 SumExceptRecompute = 0;
			for (uint32_t BlendIndex = 0; BlendIndex < NumBlendWeights; BlendIndex++)
			{
				if (BlendIndex != RecomputeIndex)
				{
					SumExceptRecompute += InWeights[CurrentVertexBufferCount].InfluenceWeights[BlendIndex];
				}
			}

			ensure(SumExceptRecompute >= 0 && SumExceptRecompute <= 255);
			InWeights[CurrentVertexBufferCount].InfluenceWeights[RecomputeIndex] = 255 - SumExceptRecompute;

			CurrentVertexBufferCount++;
		}
	}

	UE_LOG(LogAvatars, Display, TEXT("[Mesh] Loading Indicies"));

	LodRenderData->StaticVertexBuffers.ColorVertexBuffer.InitFromColorArray(ColorArray);
	LodRenderData->SkinWeightVertexBuffer.SetHasExtraBoneInfluences(true);
	LodRenderData->SkinWeightVertexBuffer = InWeights;
	LodRenderData->MultiSizeIndexContainer.CreateIndexBuffer(sizeof(uint16_t));

	uint32_t TotalEyeMeshVertCount = SubmeshCount > 1 ? LodRenderData->RenderSections[1].NumVertices : 0;

	for (uint32_t index = 0; index < data->indexCount; index++)
	{
		if (index >= submeshFirstIndex)
		{
			const auto IndexValue = data->indexBuffer[index] + MeshVertexCountAfterSubmeshes + TotalEyeMeshVertCount;
			LodRenderData->MultiSizeIndexContainer.GetIndexBuffer()->AddItem(IndexValue);
		}
		else
		{
			LodRenderData->MultiSizeIndexContainer.GetIndexBuffer()->AddItem(data->indexBuffer[index]);
		}
	}

	const uint32_t BlendShapeCount = ovrAvatarAsset_GetMeshBlendShapeCount(asset);
	const ovrAvatarBlendVertex* blendVerts = ovrAvatarAsset_GetMeshBlendShapeVertices(asset);
	int CurrentBlendVert = 0;

	UE_LOG(LogAvatars, Display, TEXT("[Mesh] Loading BlendShapes - %d"), BlendShapeCount);

	for (uint32_t BlendIndex = 0; BlendIndex < BlendShapeCount; BlendIndex++)
	{
		FName BlendName = FName(ovrAvatarAsset_GetMeshBlendShapeName(asset, BlendIndex));

		UE_LOG(LogAvatars, Display, TEXT("[Mesh] Blend Name %s"), *BlendName.ToString());

		UMorphTarget* MorphTarget = NewObject<UMorphTarget>(SkeletalMesh, BlendName);
		FMorphTargetLODModel MorphLODModel;
		MorphLODModel.NumBaseMeshVerts = TotalVertCount;
		MorphLODModel.SectionIndices.Add(0);

		for (uint32_t VertIndex = 0; VertIndex < data->vertexCount; VertIndex++)
		{
			FMorphTargetDelta NewVertData;
			const ovrAvatarBlendVertex CurrentVert = blendVerts[CurrentBlendVert++];
			NewVertData.PositionDelta = 100.0f * FVector(-CurrentVert.z, CurrentVert.x, CurrentVert.y);
			NewVertData.TangentZDelta = FVector(-CurrentVert.nz, CurrentVert.nx, CurrentVert.ny);
			NewVertData.SourceIdx = VertIndex;
			MorphLODModel.Vertices.Add(NewVertData);
		}

		// Copy the dead eye submesh verts to match vert count
		for (uint32_t VertIndex = data->vertexCount; VertIndex < TotalVertCount; VertIndex++)
		{
			FMorphTargetDelta NewVertData;
			NewVertData.PositionDelta = 100.0f * FVector(0.f, 0.f, 0.f);
			NewVertData.TangentZDelta = FVector(0.f, 0.f, 0.f);
			NewVertData.SourceIdx = VertIndex;
			MorphLODModel.Vertices.Add(NewVertData);
		}

		MorphTarget->MorphLODModels.Add(MorphLODModel);
		SkeletalMesh->RegisterMorphTarget(MorphTarget, false);
	}

	FBoxSphereBounds Bounds(BoundBox);
	Bounds = Bounds.ExpandBy(100000.0f);
	SkeletalMesh->SetImportedBounds(Bounds);

	SkeletalMesh->Skeleton = NewObject<USkeleton>();
	SkeletalMesh->Skeleton->MergeAllBonesToBoneTree(SkeletalMesh);
	SkeletalMesh->PostLoad();

	UE_LOG(LogAvatars, Display, TEXT("[Mesh] LoadCombinedMeshEnd"));
#endif
}

void UOvrAvatar::Root3DofControllers()
{
	if (!b3DofHardware)
		return;

	if (OVRP_SUCCESS(ovrp_GetDominantHand(&DominantHand)))
	{
		HandType Hand = DominantHand == ovrpHandedness_RightHanded ? HandType_Right : HandType_Left;

		UE_LOG(LogAvatars, Display, TEXT("ovrp_GetDominantHand - %s"), *ControllerNames[Hand]);

		if (auto ScenePtr = RootAvatarComponents.Find(ControllerNames[Hand]))
		{
			if (auto HandScene = ScenePtr->Get())
			{
				if (UPoseableMeshComponent* GearMeshComp = GetMeshComponent(GearVRControllerMeshID))
				{
					GearMeshComp->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
					GearMeshComp->AttachToComponent(HandScene, FAttachmentTransformRules::SnapToTargetIncludingScale);
					GearMeshComp->RegisterComponent();
				}

				if (UPoseableMeshComponent* GoMeshComp = GetMeshComponent(GoControllerMeshID))
				{
					GoMeshComp->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
					GoMeshComp->AttachToComponent(HandScene, FAttachmentTransformRules::SnapToTargetIncludingScale);
					GoMeshComp->RegisterComponent();
				}
			}
		}
	}
	else
	{
		UE_LOG(LogAvatars, Display, TEXT("ovrp_GetDominantHand Failed"));
	}
}