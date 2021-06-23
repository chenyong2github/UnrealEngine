// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenXRAssetManager.h"
#include "OpenXRHMD.h"
#include "OpenXRCore.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/SoftObjectPath.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "OpenXRAssetDirectory.h"
#include "UObject/GCObject.h"
#include "UObject/SoftObjectPtr.h"

/* FOpenXRAssetDirectory
 *****************************************************************************/

FSoftObjectPath FOpenXRAssetDirectory::GoogleDaydream             = FString(TEXT("/OpenXR/Devices/GoogleDaydream/GoogleDaydreamController.GoogleDaydreamController"));
FSoftObjectPath FOpenXRAssetDirectory::HPMixedRealityLeft         = FString(TEXT("/OpenXR/Devices/HPMixedReality/Left/left_HPMixedRealityController.left_HPMixedRealityController"));
FSoftObjectPath FOpenXRAssetDirectory::HPMixedRealityRight        = FString(TEXT("/OpenXR/Devices/HPMixedReality/Right/right_HPMixedRealityController.right_HPMixedRealityController"));
FSoftObjectPath FOpenXRAssetDirectory::HTCVive                    = FString(TEXT("/OpenXR/Devices/HTCVive/HTCViveController.HTCViveController"));
FSoftObjectPath FOpenXRAssetDirectory::HTCViveCosmosLeft          = FString(TEXT("/OpenXR/Devices/HTCViveCosmos/Left/left_HTCViveCosmosController.left_HTCViveCosmosController"));
FSoftObjectPath FOpenXRAssetDirectory::HTCViveCosmosRight         = FString(TEXT("/OpenXR/Devices/HTCViveCosmos/Right/right_HTCViveCosmosController.right_HTCViveCosmosController"));
FSoftObjectPath FOpenXRAssetDirectory::HTCViveFocus               = FString(TEXT("/OpenXR/Devices/HTCViveFocus/HTCViveFocusController.HTCViveFocusController"));
FSoftObjectPath FOpenXRAssetDirectory::HTCViveFocusPlus           = FString(TEXT("/OpenXR/Devices/HTCViveFocusPlus/HTCViveFocusPlusController.HTCViveFocusPlusController"));
FSoftObjectPath FOpenXRAssetDirectory::MagicLeapOne               = FString(TEXT("/OpenXR/Devices/MagicLeapOne/MagicLeapOneController.MagicLeapOneController"));
FSoftObjectPath FOpenXRAssetDirectory::MicrosoftMixedRealityLeft  = FString(TEXT("/OpenXR/Devices/MicrosoftMixedReality/Left/left_MicrosoftMixedRealityController.left_MicrosoftMixedRealityController"));
FSoftObjectPath FOpenXRAssetDirectory::MicrosoftMixedRealityRight = FString(TEXT("/OpenXR/Devices/MicrosoftMixedReality/Right/right_MicrosoftMixedRealityController.right_MicrosoftMixedRealityController"));
FSoftObjectPath FOpenXRAssetDirectory::OculusGo                   = FString(TEXT("/OpenXR/Devices/OculusGo/OculusGoController.OculusGoController"));
FSoftObjectPath FOpenXRAssetDirectory::OculusTouchLeft            = FString(TEXT("/OpenXR/Devices/OculusTouch/Left/left_OculusTouchController.left_OculusTouchController"));
FSoftObjectPath FOpenXRAssetDirectory::OculusTouchRight           = FString(TEXT("/OpenXR/Devices/OculusTouch/Right/right_OculusTouchController.right_OculusTouchController"));
FSoftObjectPath FOpenXRAssetDirectory::OculusTouchV2Left          = FString(TEXT("/OpenXR/Devices/OculusTouch_v2/Left/left_OculusTouch_v2Controller.left_OculusTouch_v2Controller"));
FSoftObjectPath FOpenXRAssetDirectory::OculusTouchV2Right         = FString(TEXT("/OpenXR/Devices/OculusTouch_v2/Right/right_OculusTouch_v2Controller.right_OculusTouch_v2Controller"));
FSoftObjectPath FOpenXRAssetDirectory::OculusTouchV3Left          = FString(TEXT("/OpenXR/Devices/OculusTouch_v3/Left/left_OculusTouch_v3Controller.left_OculusTouch_v3Controller"));
FSoftObjectPath FOpenXRAssetDirectory::OculusTouchV3Right         = FString(TEXT("/OpenXR/Devices/OculusTouch_v3/Right/right_OculusTouch_v3Controller.right_OculusTouch_v3Controller"));
FSoftObjectPath FOpenXRAssetDirectory::PicoG2                     = FString(TEXT("/OpenXR/Devices/PicoG2/PicoG2Controller.PicoG2Controller"));
FSoftObjectPath FOpenXRAssetDirectory::PicoNeo2Left               = FString(TEXT("/OpenXR/Devices/PicoNeo2/Left/left_PicoNeo2Controller.left_PicoNeo2Controller"));
FSoftObjectPath FOpenXRAssetDirectory::PicoNeo2Right              = FString(TEXT("/OpenXR/Devices/PicoNeo2/Right/right_PicoNeo2Controller.right_PicoNeo2Controller"));
FSoftObjectPath FOpenXRAssetDirectory::SamsungGearVR              = FString(TEXT("/OpenXR/Devices/SamsungGearVR/SamsungGearVRController.SamsungGearVRController"));
FSoftObjectPath FOpenXRAssetDirectory::SamsungOdysseyLeft         = FString(TEXT("/OpenXR/Devices/SamsungOdyssey/Left/left_SamsungOdysseyController.left_SamsungOdysseyController"));
FSoftObjectPath FOpenXRAssetDirectory::SamsungOdysseyRight        = FString(TEXT("/OpenXR/Devices/SamsungOdyssey/Right/right_SamsungOdysseyController.right_SamsungOdysseyController"));
FSoftObjectPath FOpenXRAssetDirectory::ValveIndexLeft             = FString(TEXT("/OpenXR/Devices/ValveIndex/Left/left_ValveIndexController.left_ValveIndexController"));
FSoftObjectPath FOpenXRAssetDirectory::ValveIndexRight            = FString(TEXT("/OpenXR/Devices/ValveIndex/Right/right_ValveIndexController.right_ValveIndexController"));

#if WITH_EDITORONLY_DATA
class FOpenXRAssetRepo : public FGCObject, public TArray<UObject*>
{
public:
	// made an on-demand singleton rather than a static global, to avoid issues with FGCObject initialization
	static FOpenXRAssetRepo& Get()
	{
		static FOpenXRAssetRepo AssetRepository;
		return AssetRepository;
	}

	UObject* LoadAndAdd(const FSoftObjectPath& AssetPath)
	{
		UObject* AssetObj = AssetPath.TryLoad();
		if (AssetObj != nullptr)
		{
			AddUnique(AssetObj);
		}
		return AssetObj;
	}

public:
	//~ FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		Collector.AddReferencedObjects(*this);
	}
};

void FOpenXRAssetDirectory::LoadForCook()
{
	FOpenXRAssetRepo& AssetRepro = FOpenXRAssetRepo::Get();
}

void FOpenXRAssetDirectory::ReleaseAll()
{
	FOpenXRAssetRepo::Get().Empty();
}
#endif // WITH_EDITORONLY_DATA


/* FOpenXRAssetManager
*****************************************************************************/

FOpenXRAssetManager::FOpenXRAssetManager(XrInstance Instance, FOpenXRHMD* InHMD)
	: OpenXRHMD(InHMD)
{
	IModularFeatures::Get().RegisterModularFeature(IXRSystemAssets::GetModularFeatureName(), this);

	XR_ENSURE(xrStringToPath(Instance, "/user/hand/left", &LeftHand));
	XR_ENSURE(xrStringToPath(Instance, "/user/hand/right", &RightHand));

	XrPath Profile;
	XR_ENSURE(xrStringToPath(Instance, "/interaction_profiles/google/daydream_controller", &Profile));
	DeviceMeshes.Add(TPair<XrPath, XrPath>(Profile, LeftHand), FOpenXRAssetDirectory::GoogleDaydream);
	DeviceMeshes.Add(TPair<XrPath, XrPath>(Profile, RightHand), FOpenXRAssetDirectory::GoogleDaydream);

	XR_ENSURE(xrStringToPath(Instance, "/interaction_profiles/htc/vive_controller", &Profile));
	DeviceMeshes.Add(TPair<XrPath, XrPath>(Profile, LeftHand), FOpenXRAssetDirectory::HTCVive);
	DeviceMeshes.Add(TPair<XrPath, XrPath>(Profile, RightHand), FOpenXRAssetDirectory::HTCVive);

	XR_ENSURE(xrStringToPath(Instance, "/interaction_profiles/microsoft/motion_controller", &Profile));
	DeviceMeshes.Add(TPair<XrPath, XrPath>(Profile, LeftHand), FOpenXRAssetDirectory::MicrosoftMixedRealityLeft);
	DeviceMeshes.Add(TPair<XrPath, XrPath>(Profile, RightHand), FOpenXRAssetDirectory::MicrosoftMixedRealityRight);

	XR_ENSURE(xrStringToPath(Instance, "/interaction_profiles/oculus/go_controller", &Profile));
	DeviceMeshes.Add(TPair<XrPath, XrPath>(Profile, LeftHand), FOpenXRAssetDirectory::OculusGo);
	DeviceMeshes.Add(TPair<XrPath, XrPath>(Profile, RightHand), FOpenXRAssetDirectory::OculusGo);

	XR_ENSURE(xrStringToPath(Instance, "/interaction_profiles/oculus/touch_controller", &Profile));
	DeviceMeshes.Add(TPair<XrPath, XrPath>(Profile, LeftHand), FOpenXRAssetDirectory::OculusTouchLeft);
	DeviceMeshes.Add(TPair<XrPath, XrPath>(Profile, RightHand), FOpenXRAssetDirectory::OculusTouchRight);

	XR_ENSURE(xrStringToPath(Instance, "/interaction_profiles/valve/index_controller", &Profile));
	DeviceMeshes.Add(TPair<XrPath, XrPath>(Profile, LeftHand), FOpenXRAssetDirectory::ValveIndexLeft);
	DeviceMeshes.Add(TPair<XrPath, XrPath>(Profile, RightHand), FOpenXRAssetDirectory::ValveIndexRight);
}

FOpenXRAssetManager::~FOpenXRAssetManager()
{
	IModularFeatures::Get().UnregisterModularFeature(IXRSystemAssets::GetModularFeatureName(), this);
}

bool FOpenXRAssetManager::EnumerateRenderableDevices(TArray<int32>& DeviceListOut)
{
	return OpenXRHMD->EnumerateTrackedDevices(DeviceListOut, EXRTrackedDeviceType::Controller);
}

int32 FOpenXRAssetManager::GetDeviceId(EControllerHand ControllerHand)
{
	XrPath Target = (ControllerHand == EControllerHand::Right) ? RightHand : LeftHand;

	TArray<int32> DeviceList;
	if (OpenXRHMD->EnumerateTrackedDevices(DeviceList, EXRTrackedDeviceType::Controller))
	{
		if (ControllerHand == EControllerHand::AnyHand)
		{
			return DeviceList[0];
		}

		for (int32 i = 0; i < DeviceList.Num(); i++)
		{
			if (OpenXRHMD->GetTrackedDevicePath(i) == Target)
			{
				return i;
			}
		}
	}
	return INDEX_NONE;
}

UPrimitiveComponent* FOpenXRAssetManager::CreateRenderComponent(const int32 DeviceId, AActor* Owner, EObjectFlags Flags, const bool /*bForceSynchronous*/, const FXRComponentLoadComplete& OnLoadComplete)
{
	UPrimitiveComponent* NewRenderComponent = nullptr;
	XrSession Session = OpenXRHMD->GetSession();
	XrPath DevicePath = OpenXRHMD->GetTrackedDevicePath(DeviceId);
	if (Session && DevicePath && OpenXRHMD->IsRunning())
	{
		XrInteractionProfileState Profile;
		Profile.type = XR_TYPE_INTERACTION_PROFILE_STATE;
		Profile.next = nullptr;
		if (!XR_ENSURE(xrGetCurrentInteractionProfile(Session, DevicePath, &Profile)))
		{
			return nullptr;
		}

		TPair<XrPath, XrPath> Key(Profile.interactionProfile, DevicePath);
		FSoftObjectPath* DeviceMeshPtr = DeviceMeshes.Find(Key);
		if (!DeviceMeshPtr)
		{
			return nullptr;
		}

		if (UObject* DeviceMesh = DeviceMeshPtr->TryLoad())
		{
			if (UStaticMesh* AsStaticMesh = Cast<UStaticMesh>(DeviceMesh))
			{
				const FName ComponentName = MakeUniqueObjectName(Owner, UStaticMeshComponent::StaticClass(), *FString::Printf(TEXT("%s_Device%d"), TEXT("Oculus"), DeviceId));
				UStaticMeshComponent* MeshComponent = NewObject<UStaticMeshComponent>(Owner, ComponentName, Flags);

				MeshComponent->SetStaticMesh(AsStaticMesh);
				NewRenderComponent = MeshComponent;
			}
		}
	}

	OnLoadComplete.ExecuteIfBound(NewRenderComponent);
	return NewRenderComponent;
}

