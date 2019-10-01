// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OVR_Avatar.h"
#include "Containers/Set.h"
#include "Components/ActorComponent.h"
#include "OVR_Plugin.h"
#include "OVR_Plugin_Types.h"
#include "HAL/CriticalSection.h"

#include "OvrAvatar.generated.h"

class UPoseableMeshComponent;
class USkeletalMesh;
class UMeshComponent;
class UOvrAvatarGazeTarget;

UCLASS()
class OCULUSAVATAR_API UOvrAvatar : public UActorComponent
{
	GENERATED_BODY()

public:
	enum class ePlayerType
	{
		Local,
		Remote
	};

	enum HandType
	{
		HandType_Left,
		HandType_Right,
		HandType_Count
	};

	enum MaterialType
	{
		Opaque,
		Translucent,
		Masked
	};

	UOvrAvatar();

	virtual void BeginPlay() override;
	virtual void BeginDestroy() override;

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

	void RequestAvatar(
		uint64_t userID,
		ovrAvatarAssetLevelOfDetail lod,
		bool useCombinedBodyMesh);


	void HandleAvatarSpecification(const ovrAvatarMessage_AvatarSpecification* message);
	void HandleAssetLoaded(const ovrAvatarMessage_AssetLoaded* message);

	void SetVisibilityType(ovrAvatarVisibilityFlags flag) { VisibilityMask = flag; }
	void SetPlayerType(ePlayerType type) { PlayerType = type; }

	USceneComponent* DetachHand(HandType hand);
	void ReAttachHand(HandType hand);

	void SetRightHandPose(ovrAvatarHandGesture pose);
	void SetLeftHandPose(ovrAvatarHandGesture pose);
	void SetCustomGesture(HandType hand, ovrAvatarTransform* joints, uint32_t numJoints);
	void SetControllerVisibility(HandType hand, bool visible);

	void StartPacketRecording();
	ovrAvatarPacket* EndPacketRecording();
	void UpdateFromPacket(ovrAvatarPacket* packet, const float time);

	void SetVoiceVisualValue(float value) { VoiceVisualValue = FMath::Clamp(value, 0.f, 1.f); }

	void UpdateVisemeValues(const TArray<float>& visemes, const float laughterScore);

	void SetBodyCapability(bool Enable)	{ EnableBody = Enable; }
	void SetBaseCapability(bool Enable) { EnableBase = Enable; }
	void SetHandsCapability(bool Enable) { EnableHands = Enable; }
	void SetExpressiveCapability(bool Enable) { EnableExpressive = Enable; }
	void SetHandMaterial(MaterialType type);
	void SetBodyMaterial(MaterialType type);

protected:
	static bool Is3DOFHardware();
	static ovrAvatarControllerType GetControllerTypeByHardware();

	static FString HandNames[HandType_Count];
	static FString ControllerNames[HandType_Count];
	static FString BodyName;

	void InitializeMaterials();

	void UpdateV2VoiceOffsetParams();
	void UpdateVoiceVizOnMesh(UPoseableMeshComponent* Mesh);
	void UpdateTransforms(float DeltaTime);

	void DebugDrawSceneComponents();
	void DebugDrawBoneTransforms();

	void AddMeshComponent(ovrAvatarAssetID id, UPoseableMeshComponent* mesh);
	void AddDepthMeshComponent(ovrAvatarAssetID id, UPoseableMeshComponent* mesh);

	UPoseableMeshComponent* GetMeshComponent(ovrAvatarAssetID id) const;
	UPoseableMeshComponent* GetDepthMeshComponent(ovrAvatarAssetID id) const;

	void RemoveMeshComponent(ovrAvatarAssetID id);
	void RemoveDepthMeshComponent(ovrAvatarAssetID id);

	UPoseableMeshComponent* CreateMeshComponent(USceneComponent* parent, ovrAvatarAssetID assetID, const FName& name);
	UPoseableMeshComponent* CreateDepthMeshComponent(USceneComponent* parent, ovrAvatarAssetID assetID, const FName& name);

	void LoadMesh(USkeletalMesh* SkeletalMesh, const ovrAvatarMeshAssetData* data, ovrAvatarAsset* asset, const ovrAvatarAssetID& assetID);
	void LoadCombinedMesh(USkeletalMesh* SkeletalMesh, const ovrAvatarMeshAssetDataV2* data, ovrAvatarAsset* asset, const ovrAvatarAssetID& assetID);

	void UpdateSDK(float DeltaTime);
	void UpdatePostSDK();
	void UpdateHeadGazeTarget();

	void UpdateMeshComponent(USceneComponent& mesh, const ovrAvatarTransform& transform);
	void UpdateMaterial(UMeshComponent& mesh, const ovrAvatarMaterialState& material);
	void UpdateMaterialPBR(UPoseableMeshComponent& mesh, const ovrAvatarRenderPart_SkinnedMeshRenderPBS& data);
	void UpdateMaterialPBRV2(UPoseableMeshComponent& mesh, const ovrAvatarRenderPart_SkinnedMeshRenderPBS_V2& data, bool IsBodyMaterial);

	void UpdateSkeleton(UPoseableMeshComponent& mesh, const ovrAvatarSkinnedMeshPose& pose);
	void UpdateMorphTargets(UPoseableMeshComponent& mesh, const ovrAvatarRenderPart* renderPart);

	const FString& GetPBRV2BodyMaterial(bool);
	const FString& GetPBRV2HandMaterial();
	const FString& GetPBRV2Material();
	const FString& GetPBRV2EyeWearMaterial();
	const FString& GetPBRV2ControllerMaterial();



	static FColor GetColorFromVertex(const ovrAvatarMeshVertex& vertex);
	static FColor GetColorFromVertex(const ovrAvatarMeshVertexV2& vertex);

	void SetExpressiveMaterialParamters(class UMaterialInstanceDynamic* MaterialInstance, bool IsBodyMaterial);

	uint64_t OnlineUserID = 0;

	TSet<uint64> AssetIds;
	TMap<uint64, TWeakObjectPtr<UPoseableMeshComponent>> MeshComponents;
	TMap<uint64, TWeakObjectPtr<UPoseableMeshComponent>> DepthMeshComponents;
	TMap <uint64, TArray<FString>> AssetToMaterialStringsMap;	

	ovrAvatar* Avatar = nullptr;

	TMap<FString, TWeakObjectPtr<USceneComponent>> RootAvatarComponents;

	ovrAvatarHandInputState HandInputState[HandType_Count];
	ovrAvatarTransform BodyTransform;

	bool LeftControllerVisible = false;
	bool RightControllerVisible = false;
	ovrAvatarVisibilityFlags VisibilityMask = ovrAvatarVisibilityFlag_ThirdPerson;

	ePlayerType PlayerType = ePlayerType::Local;
	TWeakObjectPtr<USceneComponent> AvatarHands[HandType_Count];

	ovrAvatarLookAndFeelVersion LookAndFeel = ovrAvatarLookAndFeelVersion_Two;
	ovrAvatarAssetLevelOfDetail LevelOfDetail = ovrAvatarAssetLevelOfDetail_Five;

	bool UseV2VoiceVisualization = true;
	float VoiceVisualValue = 0.f;
	ovrAvatarAssetID BodyMeshID = 0;
	bool UseCombinedBodyMesh = false;
	bool AreMaterialsInitialized = false;
	ovrAvatarControllerType ControllerType = ovrAvatarControllerType_Touch;

	bool EnableExpressive = true;
	bool EnableBody = true;
	bool EnableBase = true;
	bool EnableHands = true;

	MaterialType HandMaterial = MaterialType::Masked;
	MaterialType BodyMaterial = MaterialType::Masked;

	TArray<FName> BodyBlendShapeNames;
#if PLATFORM_ANDROID	
	bool UseDepthMeshes = false;
#else
	bool UseDepthMeshes = true;
#endif

	ovrAvatarVisemes VisemeValues;

	ovrAvatarTransform WorldTransform;
	TWeakObjectPtr<UOvrAvatarGazeTarget> AvatarHeadTarget;
	TWeakObjectPtr<UOvrAvatarGazeTarget> AvatarLeftHandTarget;
	TWeakObjectPtr<UOvrAvatarGazeTarget> AvatarRightHandTarget;

	const ovrAvatarBodyComponent* NativeBodyComponent = nullptr;
	const ovrAvatarHandComponent* NativeLeftHandComponent = nullptr;
	const ovrAvatarHandComponent* NativeRightHandComponent = nullptr;
	const ovrAvatarControllerComponent* NativeRightControllerComponent = nullptr;
	const ovrAvatarControllerComponent* NativeLeftControllerComponent = nullptr;

	FCriticalSection VisemeMutex;

	bool b3DofHardware = false;

	static const uint64 GearVRControllerMeshID;
	static const uint64 GoControllerMeshID;

	void Root3DofControllers();
	ovrpHandedness DominantHand = ovrpHandedness_Unsupported;

public:
	// Material Strings
	static FString Single;
	static FString Combined;

	static FString ExpressiveMaskedBody;
	static FString ExpressiveAlphaBody;
	static FString ExpressiveOpaqueBody;

	static FString ExpressiveAlphaSimple;
	static FString ExpressiveMaskedSimple;
	static FString ExpressiveOpaqueSimple;

	static FString ExpressiveCombinedMasked;
	static FString ExpressiveCombinedOpaque;
	static FString ExpressiveCombinedAlpha;
	static FString ExpressiveEyeShell;
	static FString ExpressiveController;
};
