// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "OpenXRHMD.h"
#include "OpenXRHMDPrivate.h"
#include "OpenXRHMDPrivateRHI.h"
#include "OpenXRHMD_Swapchain.h"
#include "OpenXRHMD_RenderBridge.h"

#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "IOpenXRHMDPlugin.h"
#include "SceneRendering.h"
#include "PostProcess/PostProcessHMD.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/CString.h"
#include "ClearQuad.h"
#include "XRThreadUtils.h"
#include "RenderUtils.h"
#include "PipelineStateCache.h"
#include "Slate/SceneViewport.h"
#include "Engine/GameEngine.h"
#include "BuildSettings.h"

#if WITH_EDITOR
#include "Editor/UnrealEd/Classes/Editor/EditorEngine.h"
#endif

namespace {
	/** Helper function for acquiring the appropriate FSceneViewport */
	FSceneViewport* FindSceneViewport()
	{
		if (!GIsEditor)
		{
			UGameEngine* GameEngine = Cast<UGameEngine>(GEngine);
			return GameEngine->SceneViewport.Get();
		}
	#if WITH_EDITOR
		else
		{
			UEditorEngine* EditorEngine = CastChecked<UEditorEngine>(GEngine);
			FSceneViewport* PIEViewport = (FSceneViewport*)EditorEngine->GetPIEViewport();
			if (PIEViewport != nullptr && PIEViewport->IsStereoRenderingAllowed())
			{
				// PIE is setup for stereo rendering
				return PIEViewport;
			}
			else
			{
				// Check to see if the active editor viewport is drawing in stereo mode
				// @todo vreditor: Should work with even non-active viewport!
				FSceneViewport* EditorViewport = (FSceneViewport*)EditorEngine->GetActiveViewport();
				if (EditorViewport != nullptr && EditorViewport->IsStereoRenderingAllowed())
				{
					return EditorViewport;
				}
			}
		}
	#endif
		return nullptr;
	}
}

//---------------------------------------------------
// OpenXRHMD Plugin Implementation
//---------------------------------------------------

class FOpenXRHMDPlugin : public IOpenXRHMDPlugin
{
public:
	FOpenXRHMDPlugin()
		: LoaderHandle(nullptr)
		, Instance(XR_NULL_HANDLE)
		, System(XR_NULL_SYSTEM_ID)
		, RenderBridge(nullptr)
	{ }

	~FOpenXRHMDPlugin()
	{
		if (Instance)
		{
			XR_ENSURE(xrDestroyInstance(Instance));
		}
	}

	/** IHeadMountedDisplayModule implementation */
	virtual TSharedPtr< class IXRTrackingSystem, ESPMode::ThreadSafe > CreateTrackingSystem() override;
	virtual TSharedPtr< IHeadMountedDisplayVulkanExtensions, ESPMode::ThreadSafe > GetVulkanExtensions() override;
	virtual uint64 GetGraphicsAdapterLuid() override;
	virtual bool PreInit() override;

	FString GetModuleKeyName() const override
	{
		return FString(TEXT("OpenXRHMD"));
	}

	void GetModuleAliases(TArray<FString>& AliasesOut) const override
	{
		AliasesOut.Add(TEXT("OpenXR"));
	}

	void ShutdownModule() override
	{
		if (LoaderHandle)
		{
			FPlatformProcess::FreeDllHandle(LoaderHandle);
			LoaderHandle = nullptr;
		}
	}

	virtual bool IsHMDConnected() override { return true; }

	bool HasExtension(const char* Name) const { return AvailableExtensions.Contains(Name); }

private:
	void *LoaderHandle;
	XrInstance Instance;
	XrSystemId System;
	TSet<FString> AvailableExtensions;
	TRefCountPtr<FOpenXRRenderBridge> RenderBridge;
	TSharedPtr< IHeadMountedDisplayVulkanExtensions, ESPMode::ThreadSafe > VulkanExtensions;

	bool EnumerateExtensions();
	bool InitRenderBridge();
};

IMPLEMENT_MODULE( FOpenXRHMDPlugin, OpenXRHMD )

TSharedPtr< class IXRTrackingSystem, ESPMode::ThreadSafe > FOpenXRHMDPlugin::CreateTrackingSystem()
{
	if (!RenderBridge)
	{
		if (!InitRenderBridge())
		{
			return nullptr;
		}
	}

	auto OpenXRHMD = FSceneViewExtensions::NewExtension<FOpenXRHMD>(Instance, System, RenderBridge, HasExtension(XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME));
	if( OpenXRHMD->IsInitialized() )
	{
		return OpenXRHMD;
	}

	return nullptr;
}

uint64 FOpenXRHMDPlugin::GetGraphicsAdapterLuid()
{
	if (!RenderBridge)
	{
		if (!InitRenderBridge())
		{
			return false;
		}
	}
	return RenderBridge->GetGraphicsAdapterLuid();
}

TSharedPtr< IHeadMountedDisplayVulkanExtensions, ESPMode::ThreadSafe > FOpenXRHMDPlugin::GetVulkanExtensions()
{
#if XR_USE_GRAPHICS_API_VULKAN
	if (PreInit() && HasExtension(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME))
	{
		if (!VulkanExtensions.IsValid())
		{
			VulkanExtensions = MakeShareable(new FOpenXRHMD::FVulkanExtensions(Instance, System));
		}
		return VulkanExtensions;
	}
#endif//XR_USE_GRAPHICS_API_VULKAN
	return nullptr;
}

bool FOpenXRHMDPlugin::EnumerateExtensions()
{
	uint32_t ExtensionsCount = 0;
	if (XR_FAILED(xrEnumerateInstanceExtensionProperties(nullptr, 0, &ExtensionsCount, nullptr)))
	{
		// If it fails this early that means there's no runtime installed
		return false;
	}

	TArray<XrExtensionProperties> Properties;
	Properties.SetNum(ExtensionsCount);
	for (auto& Prop : Properties)
	{
		Prop = XrExtensionProperties{ XR_TYPE_EXTENSION_PROPERTIES };
	}

	if (XR_ENSURE(xrEnumerateInstanceExtensionProperties(nullptr, ExtensionsCount, &ExtensionsCount, Properties.GetData())))
	{
		for (const XrExtensionProperties& Prop : Properties)
		{
			AvailableExtensions.Add(Prop.extensionName);
		}
		return true;
	}
	return false;
}

bool FOpenXRHMDPlugin::InitRenderBridge()
{
	FString RHIString = FApp::GetGraphicsRHI();
	if (RHIString.IsEmpty())
	{
		return false;
	}

#ifdef XR_USE_GRAPHICS_API_D3D11
	if (RHIString == TEXT("DirectX 11") && HasExtension(XR_KHR_D3D11_ENABLE_EXTENSION_NAME))
	{
		RenderBridge = CreateRenderBridge_D3D11(Instance, System);
	}
	else
#endif
#ifdef XR_USE_GRAPHICS_API_D3D12
	if (RHIString == TEXT("DirectX 12") && HasExtension(XR_KHR_D3D12_ENABLE_EXTENSION_NAME))
	{
		RenderBridge = CreateRenderBridge_D3D12(Instance, System);
	}
	else
#endif
#ifdef XR_USE_GRAPHICS_API_OPENGL
	if (RHIString == TEXT("OpenGL") && HasExtension(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME))
	{
		RenderBridge = CreateRenderBridge_OpenGL(Instance, System);
	}
	else
#endif
#ifdef XR_USE_GRAPHICS_API_VULKAN
	if (RHIString == TEXT("Vulkan") && HasExtension(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME))
	{
		RenderBridge = CreateRenderBridge_Vulkan(Instance, System);
	}
	else
#endif
	{
		UE_LOG(LogHMD, Warning, TEXT("%s is not currently supported by the OpenXR runtime"), *RHIString);
		return false;
	}
	return true;
}

bool FOpenXRHMDPlugin::PreInit()
{
	if (Instance)
		return true;

#if PLATFORM_WINDOWS
#if PLATFORM_64BITS
	FString BinariesPath = FPaths::EngineDir() / FString(TEXT("Binaries/ThirdParty/OpenXR/win64"));
#else
	FString BinariesPath = FPaths::EngineDir() / FString(TEXT("Binaries/ThirdParty/OpenXR/win32"));
#endif

	FString LoaderName = FString::Printf(TEXT("openxr_loader-%d_%d.dll"), XR_VERSION_MAJOR(XR_CURRENT_API_VERSION), XR_VERSION_MINOR(XR_CURRENT_API_VERSION));
	FPlatformProcess::PushDllDirectory(*BinariesPath);
	LoaderHandle = FPlatformProcess::GetDllHandle(*(BinariesPath / LoaderName));
	FPlatformProcess::PopDllDirectory(*BinariesPath);
#endif

	if (!LoaderHandle)
	{
		UE_LOG(LogHMD, Log, TEXT("Failed to load openxr_loader-%d_%d.dll"), XR_VERSION_MAJOR(XR_CURRENT_API_VERSION), XR_VERSION_MINOR(XR_CURRENT_API_VERSION));
		return false;
	}

	TArray<const char*> Extensions;
	if (!EnumerateExtensions())
	{
		UE_LOG(LogHMD, Log, TEXT("Failed to enumerate extensions. Please check if you have an OpenXR runtime installed."));
		return false;
	}

#ifdef XR_USE_GRAPHICS_API_D3D11
	if (HasExtension(XR_KHR_D3D11_ENABLE_EXTENSION_NAME))
	{
		Extensions.Add(XR_KHR_D3D11_ENABLE_EXTENSION_NAME);
	}
#endif
#ifdef XR_USE_GRAPHICS_API_D3D12
	if (HasExtension(XR_KHR_D3D12_ENABLE_EXTENSION_NAME))
	{
		Extensions.Add(XR_KHR_D3D12_ENABLE_EXTENSION_NAME);
	}
#endif
#ifdef XR_USE_GRAPHICS_API_OPENGL
	if (HasExtension(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME))
	{
		Extensions.Add(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
	}
#endif
#ifdef XR_USE_GRAPHICS_API_VULKAN
	if (HasExtension(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME))
	{
		Extensions.Add(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);
	}
#endif

	if (HasExtension(XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME))
	{
		Extensions.Add(XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME);
	}

	if (HasExtension(XR_VARJO_QUAD_VIEWS_EXTENSION_NAME))
	{
		Extensions.Add(XR_VARJO_QUAD_VIEWS_EXTENSION_NAME);
	}

	// Engine registration can be disabled via console var.
	auto* CVarDisableEngineAndAppRegistration = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DisableEngineAndAppRegistration"));
	bool bDisableEngineRegistration = (CVarDisableEngineAndAppRegistration && CVarDisableEngineAndAppRegistration->GetValueOnAnyThread() != 0);

	FText ProjectName = FText();
	GConfig->GetText(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectName"), ProjectName, GGameIni);

	FText ProjectVersion = FText();
	GConfig->GetText(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectVersion"), ProjectVersion, GGameIni);

	// EngineName will be of the form "UnrealEngine4.21", with the minor version ("21" in this example)
	// updated with every quarterly release
	FString EngineName = bDisableEngineRegistration ? FString("") : FApp::GetEpicProductIdentifier() + FEngineVersion::Current().ToString(EVersionComponent::Minor);
	FString AppName = bDisableEngineRegistration ? TEXT("") : ProjectName.ToString() + ProjectVersion.ToString();

	XrInstanceCreateInfo Info;
	Info.type = XR_TYPE_INSTANCE_CREATE_INFO;
	Info.next = nullptr;
	Info.createFlags = 0;
	FTCHARToUTF8_Convert::Convert(Info.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE, *AppName, AppName.Len() + 1);
	Info.applicationInfo.applicationVersion = static_cast<uint32>(BuildSettings::GetCurrentChangelist()) | (BuildSettings::IsLicenseeVersion() ? 0x80000000 : 0);
	FTCHARToUTF8_Convert::Convert(Info.applicationInfo.engineName, XR_MAX_ENGINE_NAME_SIZE, *EngineName, EngineName.Len() + 1);
	Info.applicationInfo.engineVersion = (uint32)(FEngineVersion::Current().GetMinor() << 16 | FEngineVersion::Current().GetPatch());
	Info.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
	Info.enabledApiLayerCount = 0;
	Info.enabledApiLayerNames = nullptr;
	Info.enabledExtensionCount = Extensions.Num();
	Info.enabledExtensionNames = Extensions.GetData();
	XrResult rs = xrCreateInstance(&Info, &Instance);
	if (XR_FAILED(rs))
	{
		char error[XR_MAX_RESULT_STRING_SIZE] = { '\0' };
		xrResultToString(XR_NULL_HANDLE, rs, error);
		UE_LOG(LogHMD, Log, TEXT("Failed to create an OpenXR instance, result is %s. Please check if you have an OpenXR runtime installed."), error);
		return false;
	}

	XrSystemGetInfo SystemInfo;
	SystemInfo.type = XR_TYPE_SYSTEM_GET_INFO;
	SystemInfo.next = nullptr;
	SystemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	rs = xrGetSystem(Instance, &SystemInfo, &System);
	if (XR_FAILED(rs))
	{
		char error[XR_MAX_RESULT_STRING_SIZE] = { '\0' };
		xrResultToString(XR_NULL_HANDLE, rs, error);
		UE_LOG(LogHMD, Log, TEXT("Failed to get an OpenXR system, result is %s. Please check that your runtime supports VR headsets."), error);
		return false;
	}

	return true;
}

//---------------------------------------------------
// OpenXRHMD IHeadMountedDisplay Implementation
//---------------------------------------------------

bool FOpenXRHMD::FVulkanExtensions::GetVulkanInstanceExtensionsRequired(TArray<const ANSICHAR*>& Out)
{
#ifdef XR_USE_GRAPHICS_API_VULKAN
	TArray<VkExtensionProperties> Properties;
	{
		uint32_t PropertyCount;
		VulkanRHI::vkEnumerateInstanceExtensionProperties(nullptr, &PropertyCount, nullptr);
		Properties.SetNum(PropertyCount);
		VulkanRHI::vkEnumerateInstanceExtensionProperties(nullptr, &PropertyCount, Properties.GetData());
	}

	{
		uint32 ExtensionCount = 0;
		XR_ENSURE(xrGetVulkanInstanceExtensionsKHR(Instance, System, 0, &ExtensionCount, nullptr));
		Extensions.SetNum(ExtensionCount);
		XR_ENSURE(xrGetVulkanInstanceExtensionsKHR(Instance, System, ExtensionCount, &ExtensionCount, Extensions.GetData()));
	}

	ANSICHAR* Context = nullptr;
	for (ANSICHAR* Tok = FCStringAnsi::Strtok(Extensions.GetData(), " ", &Context); Tok != nullptr; Tok = FCStringAnsi::Strtok(nullptr, " ", &Context))
	{
		bool ExtensionFound = false;
		for (int32 PropertyIndex = 0; PropertyIndex < Properties.Num(); PropertyIndex++)
		{
			const char* PropertyExtensionName = Properties[PropertyIndex].extensionName;

			if (!FCStringAnsi::Strcmp(PropertyExtensionName, Tok))
			{
				Out.Add(Tok);
				ExtensionFound = true;
				break;
			}
		}

		if (!ExtensionFound)
		{
			UE_LOG(LogHMD, Log, TEXT("Missing required Vulkan instance extension %s."), Tok);
			return false;
		}
	}
#endif
	return true;
}

bool FOpenXRHMD::FVulkanExtensions::GetVulkanDeviceExtensionsRequired(VkPhysicalDevice_T *pPhysicalDevice, TArray<const ANSICHAR*>& Out)
{
#ifdef XR_USE_GRAPHICS_API_VULKAN
	TArray<VkExtensionProperties> Properties;
	{
		uint32_t PropertyCount;
		VulkanRHI::vkEnumerateDeviceExtensionProperties((VkPhysicalDevice)pPhysicalDevice, nullptr, &PropertyCount, nullptr);
		Properties.SetNum(PropertyCount);
		VulkanRHI::vkEnumerateDeviceExtensionProperties((VkPhysicalDevice)pPhysicalDevice, nullptr, &PropertyCount, Properties.GetData());
	}

	{
		uint32 ExtensionCount = 0;
		XR_ENSURE(xrGetVulkanDeviceExtensionsKHR(Instance, System, 0, &ExtensionCount, nullptr));
		DeviceExtensions.SetNum(ExtensionCount);
		XR_ENSURE(xrGetVulkanDeviceExtensionsKHR(Instance, System, ExtensionCount, &ExtensionCount, DeviceExtensions.GetData()));
	}

	ANSICHAR* Context = nullptr;
	for (ANSICHAR* Tok = FCStringAnsi::Strtok(DeviceExtensions.GetData(), " ", &Context); Tok != nullptr; Tok = FCStringAnsi::Strtok(nullptr, " ", &Context))
	{
		bool ExtensionFound = false;
		for (int32 PropertyIndex = 0; PropertyIndex < Properties.Num(); PropertyIndex++)
		{
			const char* PropertyExtensionName = Properties[PropertyIndex].extensionName;

			if (!FCStringAnsi::Strcmp(PropertyExtensionName, Tok))
			{
				Out.Add(Tok);
				ExtensionFound = true;
				break;
			}
		}

		if (!ExtensionFound)
		{
			UE_LOG(LogHMD, Log, TEXT("Missing required Vulkan device extension %s."), Tok);
			return false;
		}
	}
#endif
	return true;
}

float FOpenXRHMD::GetWorldToMetersScale() const
{
	return 100.0f;
}

bool FOpenXRHMD::IsHMDEnabled() const
{
	return true;
}

void FOpenXRHMD::EnableHMD(bool enable)
{
}

bool FOpenXRHMD::GetHMDMonitorInfo(MonitorInfo& MonitorDesc)
{
	MonitorDesc.MonitorName = "";
	MonitorDesc.MonitorId = 0;
	MonitorDesc.DesktopX = MonitorDesc.DesktopY = MonitorDesc.ResolutionX = MonitorDesc.ResolutionY = 0;
	return false;
}

void FOpenXRHMD::GetFieldOfView(float& OutHFOVInDegrees, float& OutVFOVInDegrees) const
{
	OutHFOVInDegrees = 0.0f;
	OutVFOVInDegrees = 0.0f;
}

bool FOpenXRHMD::EnumerateTrackedDevices(TArray<int32>& OutDevices, EXRTrackedDeviceType Type)
{
	if (Type == EXRTrackedDeviceType::Any || Type == EXRTrackedDeviceType::HeadMountedDisplay)
	{
		OutDevices.Add(IXRTrackingSystem::HMDDeviceId);
	}
	if (Type == EXRTrackedDeviceType::Any || Type == EXRTrackedDeviceType::Controller)
	{
		for (int32 i = 0; i < ActionSpaces.Num(); i++)
		{
			OutDevices.Add(i);
		}
	}
	return OutDevices.Num() > 0;
}

void FOpenXRHMD::SetInterpupillaryDistance(float NewInterpupillaryDistance)
{
}

float FOpenXRHMD::GetInterpupillaryDistance() const
{
	return 0.064f;
}

bool FOpenXRHMD::GetCurrentPose(int32 DeviceId, FQuat& CurrentOrientation, FVector& CurrentPosition)
{
	CurrentOrientation = FQuat::Identity;
	CurrentPosition = FVector::ZeroVector;

	if (!ActionSpaces.IsValidIndex(DeviceId) || !ActionSpaces[DeviceId].Space || FrameState.predictedDisplayTime <= 0)
	{
		return false;
	}

	XrSpaceLocation Location = {};
	Location.type = XR_TYPE_SPACE_LOCATION;
	XrResult Result = xrLocateSpace(ActionSpaces[DeviceId].Space, GetTrackingSpace(), FrameState.predictedDisplayTime, &Location);
	if (!XR_ENSURE(Result))
	{
		return false;
	}

	if (Location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)
	{
		CurrentOrientation = ToFQuat(Location.pose.orientation);
	}

	if (Location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
	{
		CurrentPosition = ToFVector(Location.pose.position, GetWorldToMetersScale());
	}

	return true;
}

bool FOpenXRHMD::IsChromaAbCorrectionEnabled() const
{
	return false;
}

void FOpenXRHMD::ResetOrientationAndPosition(float yaw)
{
	ResetOrientation(yaw);
	ResetPosition();
}

void FOpenXRHMD::ResetOrientation(float Yaw)
{
}

void FOpenXRHMD::ResetPosition()
{
}

void FOpenXRHMD::SetBaseRotation(const FRotator& BaseRot)
{
}

FRotator FOpenXRHMD::GetBaseRotation() const
{
	return FRotator::ZeroRotator;
}

void FOpenXRHMD::SetBaseOrientation(const FQuat& BaseOrient)
{
}

FQuat FOpenXRHMD::GetBaseOrientation() const
{
	return FQuat::Identity;
}

bool FOpenXRHMD::IsStereoEnabled() const
{
	return bStereoEnabled;
}

bool FOpenXRHMD::EnableStereo(bool stereo)
{
	if (stereo == bStereoEnabled)
	{
		return true;
	}

	bStereoEnabled = stereo;
	if (stereo)
	{
		GEngine->bForceDisableFrameRateSmoothing = true;
		return OnStereoStartup();
	}
	else
	{
		GEngine->bForceDisableFrameRateSmoothing = false;
		return OnStereoTeardown();
	}
}

void FOpenXRHMD::AdjustViewRect(EStereoscopicPass StereoPass, int32& X, int32& Y, uint32& SizeX, uint32& SizeY) const
{
	const uint32 ViewIndex = GetViewIndexForPass(StereoPass);

	const XrViewConfigurationView& Config = Configs[ViewIndex];

	for (uint32 i = 0; i < ViewIndex; ++i)
	{
		X += Configs[i].recommendedImageRectWidth;
	}

	SizeX = Config.recommendedImageRectWidth;
	SizeY = Config.recommendedImageRectHeight;
}

EStereoscopicPass FOpenXRHMD::GetViewPassForIndex(bool bStereoRequested, uint32 ViewIndex) const
{
	if (!bStereoRequested)
		return EStereoscopicPass::eSSP_FULL;

	return static_cast<EStereoscopicPass>(eSSP_LEFT_EYE + ViewIndex);
}

uint32 FOpenXRHMD::GetViewIndexForPass(EStereoscopicPass StereoPassType) const
{
	switch (StereoPassType)
	{
	case eSSP_LEFT_EYE:
	case eSSP_FULL:
		return 0;

	case eSSP_RIGHT_EYE:
		return 1;

	default:
		return StereoPassType - eSSP_LEFT_EYE;
	}
}

int32 FOpenXRHMD::GetDesiredNumberOfViews(bool bStereoRequested) const
{
	return bStereoRequested ? Views.Num() : 1; // FIXME: Monoscopic actually needs 2 views for quad vr
}

bool FOpenXRHMD::GetRelativeEyePose(int32 InDeviceId, EStereoscopicPass InEye, FQuat& OutOrientation, FVector& OutPosition)
{
	if (InDeviceId != IXRTrackingSystem::HMDDeviceId)
	{
		return false;
	}

	const uint32 ViewIndex = GetViewIndexForPass(InEye);
	const XrView& View = Views[ViewIndex];
	OutOrientation = ToFQuat(View.pose.orientation);
	OutPosition = ToFVector(View.pose.position, GetWorldToMetersScale());
	return true;
}

FMatrix FOpenXRHMD::GetStereoProjectionMatrix(const enum EStereoscopicPass StereoPassType) const
{
	const uint32 ViewIndex = GetViewIndexForPass(StereoPassType);

	XrFovf Fov = Views[ViewIndex].fov;

	float ZNear = GNearClippingPlane;

	Fov.angleUp = tan(Fov.angleUp);
	Fov.angleDown = tan(Fov.angleDown);
	Fov.angleLeft = tan(Fov.angleLeft);
	Fov.angleRight = tan(Fov.angleRight);

	float SumRL = (Fov.angleRight + Fov.angleLeft);
	float SumTB = (Fov.angleUp + Fov.angleDown);
	float InvRL = (1.0f / (Fov.angleRight - Fov.angleLeft));
	float InvTB = (1.0f / (Fov.angleUp - Fov.angleDown));

	FMatrix Mat = FMatrix(
		FPlane((2.0f * InvRL), 0.0f, 0.0f, 0.0f),
		FPlane(0.0f, (2.0f * InvTB), 0.0f, 0.0f),
		FPlane((SumRL * -InvRL), (SumTB * -InvTB), 0.0f, 1.0f),
		FPlane(0.0f, 0.0f, ZNear, 0.0f)
	);

	return Mat;
}

void FOpenXRHMD::GetEyeRenderParams_RenderThread(const FRenderingCompositePassContext& Context, FVector2D& EyeToSrcUVScaleValue, FVector2D& EyeToSrcUVOffsetValue) const
{
	EyeToSrcUVOffsetValue = FVector2D::ZeroVector;
	EyeToSrcUVScaleValue = FVector2D(1.0f, 1.0f);
}


void FOpenXRHMD::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
	InViewFamily.EngineShowFlags.MotionBlur = 0;
	InViewFamily.EngineShowFlags.HMDDistortion = false;
	InViewFamily.EngineShowFlags.StereoRendering = IsStereoEnabled();

	// TODO: Handle dynamic resolution in the driver, so the runtime
	// can take advantage of the extra resolution in the distortion process.
	InViewFamily.EngineShowFlags.ScreenPercentage = 0;

	if (Configs.Num() > 2)
	{
		InViewFamily.EngineShowFlags.Vignette = 0;
		InViewFamily.EngineShowFlags.Bloom = 0;
	}
}

void FOpenXRHMD::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
{
}

void FOpenXRHMD::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
}

void FOpenXRHMD::PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView)
{
	check(IsInRenderingThread());
}

void FOpenXRHMD::PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& ViewFamily)
{
	check(IsInRenderingThread());

	if (SpectatorScreenController)
	{
		SpectatorScreenController->UpdateSpectatorScreenMode_RenderThread();
	}
}

bool FOpenXRHMD::IsActiveThisFrame(class FViewport* InViewport) const
{
	return GEngine && GEngine->IsStereoscopic3D(InViewport);
}

FOpenXRHMD::FOpenXRHMD(const FAutoRegister& AutoRegister, XrInstance InInstance, XrSystemId InSystem, TRefCountPtr<FOpenXRRenderBridge>& InRenderBridge, bool InDepthExtensionSupported)
	: FHeadMountedDisplayBase(nullptr)
	, FSceneViewExtensionBase(AutoRegister)
	, bStereoEnabled(false)
	, bIsRunning(false)
	, bIsReady(false)
	, bIsRendering(false)
	, bRunRequested(false)
	, bDepthExtensionSupported(InDepthExtensionSupported)
	, bNeedReAllocatedDepth(InDepthExtensionSupported)
	, CurrentSessionState(XR_SESSION_STATE_UNKNOWN)
	, Instance(InInstance)
	, System(InSystem)
	, Session(XR_NULL_HANDLE)
	, LocalSpace(XR_NULL_HANDLE)
	, StageSpace(XR_NULL_HANDLE)
	, TrackingSpaceType(XR_REFERENCE_SPACE_TYPE_STAGE)
	, SelectedViewConfigurationType(XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
	, RenderBridge(InRenderBridge)
	, RendererModule(nullptr)
{
	FrameState.type = XR_TYPE_FRAME_STATE;
	FrameState.next = nullptr;
	FrameState.predictedDisplayPeriod = FrameState.predictedDisplayTime = 0;
	FrameStateRHI = FrameState;

	ViewState.type = XR_TYPE_VIEW_STATE;
	ViewState.next = nullptr;
	ViewState.viewStateFlags = 0;

	{
		// Enumerate the viewport configurations
		uint32 ConfigurationCount;
		TArray<XrViewConfigurationType> Types;
		XR_ENSURE(xrEnumerateViewConfigurations(Instance, System, 0, &ConfigurationCount, nullptr));
		Types.SetNum(ConfigurationCount);
		XR_ENSURE(xrEnumerateViewConfigurations(Instance, System, ConfigurationCount, &ConfigurationCount, Types.GetData()));

		// Ensure the configuration type we want is provided
		if (Types.Contains(XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO))
		{
			SelectedViewConfigurationType = static_cast<XrViewConfigurationType>(XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO);
		}
		ensure(Types.Contains(SelectedViewConfigurationType));

		// Enumerate the viewport view configurations
		uint32 ViewCount;
		XR_ENSURE(xrEnumerateViewConfigurationViews(Instance, System, SelectedViewConfigurationType, 0, &ViewCount, nullptr));
		Configs.SetNum(ViewCount);
		for (XrViewConfigurationView& View : Configs)
		{
			View.type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
			View.next = nullptr;
		}
		XR_ENSURE(xrEnumerateViewConfigurationViews(Instance, System, SelectedViewConfigurationType, ViewCount, &ViewCount, Configs.GetData()));
	}

	// Ensure the views have sane values before we locate them
	Views.SetNum(Configs.Num());
	for (XrView& View : Views)
	{
		View.type = XR_TYPE_VIEW;
		View.next = nullptr;
		View.fov = XrFovf{ -PI / 4.0f, PI / 4.0f, PI / 4.0f, -PI / 4.0f };
		View.pose = ToXrPose(FTransform::Identity);
	}

	// The HMD device does not have an action associated with it
	ensure(ActionSpaces.Emplace(XR_NULL_HANDLE) == HMDDeviceId);
}

bool FOpenXRHMD::OnStereoStartup()
{
	FOpenXRHMD* Self = this;
	ENQUEUE_RENDER_COMMAND(OpenXRCreateSession)([Self](FRHICommandListImmediate& RHICmdList)
	{
		XrSessionCreateInfo SessionInfo;
		SessionInfo.type = XR_TYPE_SESSION_CREATE_INFO;
		SessionInfo.next = Self->RenderBridge->GetGraphicsBinding_RenderThread();
		SessionInfo.createFlags = 0;
		SessionInfo.systemId = Self->System;
		XR_ENSURE(xrCreateSession(Self->Instance, &SessionInfo, &Self->Session));
	});

	FlushRenderingCommands();

	uint32_t referenceSpacesCount;
	XR_ENSURE(xrEnumerateReferenceSpaces(Session, 0, &referenceSpacesCount, nullptr));

	TArray<XrReferenceSpaceType> spaces;
	spaces.SetNum(referenceSpacesCount);
	XR_ENSURE(xrEnumerateReferenceSpaces(Session, (uint32_t)spaces.Num(), &referenceSpacesCount, spaces.GetData()));
	ensure(referenceSpacesCount == spaces.Num());

	XrSpace HmdSpace = XR_NULL_HANDLE;
	XrReferenceSpaceCreateInfo SpaceInfo;

	ensure(spaces.Contains(XR_REFERENCE_SPACE_TYPE_VIEW));
	SpaceInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
	SpaceInfo.next = nullptr;
	SpaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	SpaceInfo.poseInReferenceSpace = ToXrPose(FTransform::Identity);
	XR_ENSURE(xrCreateReferenceSpace(Session, &SpaceInfo, &HmdSpace));
	ActionSpaces[HMDDeviceId].Space = HmdSpace;

	ensure(spaces.Contains(XR_REFERENCE_SPACE_TYPE_LOCAL));
	SpaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	XR_ENSURE(xrCreateReferenceSpace(Session, &SpaceInfo, &LocalSpace));

	// Prefer a stage space over a local space
	if (spaces.Contains(XR_REFERENCE_SPACE_TYPE_STAGE))
	{
		TrackingSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
		SpaceInfo.referenceSpaceType = TrackingSpaceType;
		XR_ENSURE(xrCreateReferenceSpace(Session, &SpaceInfo, &StageSpace));
	}
	else
	{
		TrackingSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	}

	// Create action spaces for all devices
	for (auto& ActionSpace : ActionSpaces)
	{
		ActionSpace.CreateSpace(Session);
	}

	RenderBridge->SetOpenXRHMD(this);

	// grab a pointer to the renderer module for displaying our mirror window
	static const FName RendererModuleName("Renderer");
	RendererModule = FModuleManager::GetModulePtr<IRendererModule>(RendererModuleName);

	SpectatorScreenController = MakeUnique<FDefaultSpectatorScreenController>(this);

	StartSession();

	return true;
}

bool FOpenXRHMD::OnStereoTeardown()
{
	if (Session != XR_NULL_HANDLE)
	{
		xrRequestExitSession(Session);
	}

	return true;
}

FOpenXRHMD::~FOpenXRHMD()
{
	if (Session)
	{
		XR_ENSURE(xrDestroySession(Session));
	}
}

int32 FOpenXRHMD::AddActionDevice(XrAction Action)
{
	int32 DeviceId = ActionSpaces.Emplace(Action);
	if (Session)
	{
		ActionSpaces[DeviceId].CreateSpace(Session);
	}

	return DeviceId;
}

bool FOpenXRHMD::IsInitialized() const
{
	return Instance != XR_NULL_HANDLE;
}

bool FOpenXRHMD::IsRunning() const
{
	return bIsRunning;
}

bool FOpenXRHMD::IsFocused() const
{
	return CurrentSessionState == XR_SESSION_STATE_FOCUSED;
}

bool FOpenXRHMD::StartSession()
{
	// If the session is already running, or is not yet ready, 
	if (!bIsReady || bIsRunning)
	{
		if (!bIsRunning)
			bRunRequested = true;

		return false;
	}

	XrSessionBeginInfo Begin = { XR_TYPE_SESSION_BEGIN_INFO, nullptr, SelectedViewConfigurationType };
	bIsRunning = XR_ENSURE(xrBeginSession(Session, &Begin));

	// Unflag a request, if we're running.
	if (bIsRunning)
		bRunRequested = false;

	return bIsRunning;
}

void FOpenXRHMD::OnBeginPlay(FWorldContext& InWorldContext)
{
}

void FOpenXRHMD::OnEndPlay(FWorldContext& InWorldContext)
{
}

IStereoRenderTargetManager* FOpenXRHMD::GetRenderTargetManager()
{
	return this;
}

bool FOpenXRHMD::AllocateRenderTargetTexture(uint32 Index, uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 Flags, uint32 TargetableTextureFlags, FTexture2DRHIRef& OutTargetableTexture, FTexture2DRHIRef& OutShaderResourceTexture, uint32 NumSamples)
{
	check(IsInRenderingThread());

	Swapchain = RenderBridge->CreateSwapchain(Session, Format, SizeX, SizeY, NumMips, NumSamples, Flags, TargetableTextureFlags);
	if (!Swapchain)
	{
		return false;
	}
	
	// Grab the presentation texture out of the swapchain.
	OutTargetableTexture = OutShaderResourceTexture = (FTexture2DRHIRef&)Swapchain->GetTextureRef();

	if (bDepthExtensionSupported)
	{
		// Allocate the depth buffer swapchain while we're here.
		DepthSwapchain = RenderBridge->CreateSwapchain(Session, PF_DepthStencil, SizeX, SizeY, NumMips, NumSamples, 0, TexCreate_DepthStencilTargetable);
		if (!DepthSwapchain)
		{
			return false;
		}
		bNeedReAllocatedDepth = false;
	}

	return true;
}

bool FOpenXRHMD::AllocateDepthTexture(uint32 Index, uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 InTexFlags, uint32 TargetableTextureFlags, FTexture2DRHIRef& OutTargetableTexture, FTexture2DRHIRef& OutShaderResourceTexture, uint32 NumSamples)
{
	if (!DepthSwapchain.IsValid())
	{
		return false;
	}

	OutTargetableTexture = OutShaderResourceTexture = (FTexture2DRHIRef&)DepthSwapchain->GetTextureRef();

	return true;
}

void FOpenXRHMD::OnBeginRendering_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& ViewFamily)
{
	if (bIsReady)
	{
		XrFrameBeginInfo BeginInfo;
		BeginInfo.type = XR_TYPE_FRAME_BEGIN_INFO;
		BeginInfo.next = nullptr;
		xrBeginFrame(Session, &BeginInfo);
		bIsRendering = true;

		Swapchain->IncrementSwapChainIndex_RHIThread(FrameStateRHI.predictedDisplayPeriod);
		if (bDepthExtensionSupported)
		{
			ensure(DepthSwapchain != nullptr);
			DepthSwapchain->IncrementSwapChainIndex_RHIThread(FrameStateRHI.predictedDisplayPeriod);
		}
	}

	FrameStateRHI = FrameState;

	const FSceneView* MainView = ViewFamily.Views[0];
	check(MainView);
	BaseTransform = FTransform(MainView->BaseHmdOrientation, MainView->BaseHmdLocation);

	ViewsRHI.SetNum(Views.Num());
	DepthLayersRHI.SetNum(Views.Num());

	int32 OffsetX = 0;

	const float WorldScale = GetWorldToMetersScale() * (1.0 / 100.0f); // physical scale is 100 UUs/meter
	float NearZ = GNearClippingPlane * WorldScale;

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const XrView& View = Views[ViewIndex];
		const XrViewConfigurationView& Config = Configs[ViewIndex];
		FTransform ViewTransform = ToFTransform(View.pose, GetWorldToMetersScale());

		XrCompositionLayerProjectionView& Projection = ViewsRHI[ViewIndex];
		XrCompositionLayerDepthInfoKHR& DepthLayer = DepthLayersRHI[ViewIndex];

		Projection.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
		Projection.next = nullptr; // &DepthLayer;
		Projection.fov = View.fov;
		Projection.pose = ToXrPose(ViewTransform * BaseTransform, GetWorldToMetersScale());
		Projection.subImage.swapchain = static_cast<FOpenXRSwapchain*>(GetSwapchain())->GetHandle();
		Projection.subImage.imageArrayIndex = 0;
		Projection.subImage.imageRect = {
			{ OffsetX, 0 },
			{
				(int32)Config.recommendedImageRectWidth,
				(int32)Config.recommendedImageRectHeight
			}
		};

		if (bDepthExtensionSupported)
		{
			DepthLayer.type = XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR;
			DepthLayer.next = nullptr;
			DepthLayer.subImage.swapchain = static_cast<FOpenXRSwapchain*>(GetDepthSwapchain())->GetHandle();
			DepthLayer.subImage.imageArrayIndex = 0;
			DepthLayer.subImage.imageRect = Projection.subImage.imageRect;
			DepthLayer.minDepth = 1.0f;
			DepthLayer.maxDepth = 0.0f;
			DepthLayer.nearZ = NearZ;
			DepthLayer.farZ = FLT_MAX;
		}

		OffsetX += Config.recommendedImageRectWidth;
	}

	// Give the RHI thread its own copy of the frame state and tracking space
	TrackingSpaceRHI = GetTrackingSpace();
}

void FOpenXRHMD::OnLateUpdateApplied_RenderThread(const FTransform& NewRelativeTransform)
{
	FHeadMountedDisplayBase::OnLateUpdateApplied_RenderThread(NewRelativeTransform);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		XrCompositionLayerProjectionView& Projection = ViewsRHI[ViewIndex];
		FTransform ViewTransform = ToFTransform(Projection.pose, GetWorldToMetersScale()) * BaseTransform.Inverse();
		Projection.pose = ToXrPose(ViewTransform * NewRelativeTransform, GetWorldToMetersScale());
	}
}

void FOpenXRHMD::OnBeginRendering_GameThread()
{
	if (!bIsReady)
	{
		// @todo: Sleep here?
		return;
	}

	XrFrameWaitInfo WaitInfo;
	WaitInfo.type = XR_TYPE_FRAME_WAIT_INFO;
	WaitInfo.next = nullptr;

	// Pass in a clean FrameState (some implementations may fail if this is just uninitialized).
	FrameState.type = XR_TYPE_FRAME_STATE;
	FrameState.next = nullptr;
	FrameState.predictedDisplayPeriod = 0;

	XR_ENSURE(xrWaitFrame(Session, &WaitInfo, &FrameState));

	uint32_t ViewCount = 0;
	XrViewLocateInfo ViewInfo;
	ViewInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
	ViewInfo.next = nullptr;
	ViewInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	ViewInfo.space = ActionSpaces[HMDDeviceId].Space;
	ViewInfo.displayTime = FrameState.predictedDisplayTime;
	XR_ENSURE(xrLocateViews(Session, &ViewInfo, &ViewState, 0, &ViewCount, nullptr));
	Views.SetNum(ViewCount);
	XR_ENSURE(xrLocateViews(Session, &ViewInfo, &ViewState, Views.Num(), &ViewCount, Views.GetData()));
}

bool FOpenXRHMD::ReadNextEvent(XrEventDataBuffer* buffer)
{
	// It is sufficient to clear just the XrEventDataBuffer header to XR_TYPE_EVENT_DATA_BUFFER
	XrEventDataBaseHeader* baseHeader = reinterpret_cast<XrEventDataBaseHeader*>(buffer);
	*baseHeader = { XR_TYPE_EVENT_DATA_BUFFER };
	const XrResult xr = xrPollEvent(Instance, buffer);
	XR_ENSURE(xr);
	return xr == XR_SUCCESS;
}

bool FOpenXRHMD::OnStartGameFrame(FWorldContext& WorldContext)
{
	// Process all pending messages.
	XrEventDataBuffer event;
	while (ReadNextEvent(&event))
	{
		switch (event.type) 
		{
		case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
		{
			const XrEventDataSessionStateChanged& SessionState =
				reinterpret_cast<XrEventDataSessionStateChanged&>(event);

			CurrentSessionState = SessionState.state;

#if 0
			static const TCHAR* StateTextMap[] =
			{
				TEXT("XR_SESSION_STATE_UNKNOWN"),
				TEXT("XR_SESSION_STATE_IDLE"),
				TEXT("XR_SESSION_STATE_READY"),
				TEXT("XR_SESSION_STATE_SYNCHRONIZED"),
				TEXT("XR_SESSION_STATE_VISIBLE"),
				TEXT("XR_SESSION_STATE_FOCUSED"),
				TEXT("XR_SESSION_STATE_STOPPING"),
				TEXT("XR_SESSION_STATE_LOSS_PENDING"),
				TEXT("XR_SESSION_STATE_EXITING"),
			};
			TCHAR * StateText = ((int)CurrentSessionState <= (int)XR_SESSION_STATE_EXITING) ? StateTextMap[(int)CurrentSessionState] : TEXT("");
			UE_LOG(LogHMD, Log, TEXT("Session state switching to %s"), StateText);
#endif

			if (SessionState.state == XR_SESSION_STATE_READY)
			{
				bIsReady = true;
				if (bRunRequested)
				{
					StartSession();
				}
				break;
			}

			if (SessionState.state != XR_SESSION_STATE_STOPPING && SessionState.state != XR_SESSION_STATE_EXITING)
			{
				break;
			}
		}
		// Intentional fall-through
		case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
		{
#if WITH_EDITOR
			if (GIsEditor)
			{
				FSceneViewport* SceneVP = FindSceneViewport();
				if (SceneVP && SceneVP->IsStereoRenderingAllowed())
				{
					TSharedPtr<SWindow> Window = SceneVP->FindWindow();
					Window->RequestDestroyWindow();
				}
			}
			else
#endif//WITH_EDITOR
			{
				// ApplicationWillTerminateDelegate will fire from inside of the RequestExit
				FPlatformMisc::RequestExit(false);
			}

			if (Session != XR_NULL_HANDLE)
			{
				// Clear up action spaces
				for (auto& ActionSpace : ActionSpaces)
				{
					ActionSpace.DestroySpace();
				}

				// Close the session now we're allowed to.
				ENQUEUE_RENDER_COMMAND(OpenXRDestroySession)([this](FRHICommandListImmediate& RHICmdList)
				{
					if (bIsRunning)
					{
						XR_ENSURE(xrEndSession(Session));
					}

					XR_ENSURE(xrDestroySession(Session));

					Session = XR_NULL_HANDLE;
				});

				FlushRenderingCommands();

				bIsRendering = false;
				bIsReady = false;
				bIsRunning = false;
				bRunRequested = false;
			}

			break;
		}
		case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
		{
			const XrEventDataReferenceSpaceChangePending& SpaceChange =
				reinterpret_cast<XrEventDataReferenceSpaceChangePending&>(event);
			if (SpaceChange.referenceSpaceType == TrackingSpaceType)
			{
				OnTrackingOriginChanged();
			}
			break;
		}
		}
	}

	return true;
}

void FOpenXRHMD::FinishRendering()
{
	XrCompositionLayerProjection Layer = {};
	Layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
	Layer.next = nullptr;
	Layer.space = TrackingSpaceRHI;
	Layer.viewCount = ViewsRHI.Num();
	Layer.views = ViewsRHI.GetData();

	if (bIsRendering)
	{
		Swapchain->ReleaseCurrentImage_RHIThread();

		if (bDepthExtensionSupported)
		{
			DepthSwapchain->ReleaseCurrentImage_RHIThread();
		}

		XrFrameEndInfo EndInfo;
		XrCompositionLayerBaseHeader* Headers[1] = { reinterpret_cast<XrCompositionLayerBaseHeader*>(&Layer) };
		EndInfo.type = XR_TYPE_FRAME_END_INFO;
		EndInfo.next = nullptr;
		EndInfo.displayTime = FrameStateRHI.predictedDisplayTime;
		EndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
		EndInfo.layerCount = 1;
		EndInfo.layers = reinterpret_cast<XrCompositionLayerBaseHeader**>(Headers);
		XrResult Result = xrEndFrame(Session, &EndInfo);

		// Ignore invalid call order for now, we will recover on the next frame
		ensure(XR_SUCCEEDED(Result) || Result == XR_ERROR_CALL_ORDER_INVALID);
	}
}

FXRRenderBridge* FOpenXRHMD::GetActiveRenderBridge_GameThread(bool /* bUseSeparateRenderTarget */)
{
	return RenderBridge;
}

FIntPoint FOpenXRHMD::GetIdealRenderTargetSize() const
{
	FIntPoint Size(EForceInit::ForceInitToZero);
	for (XrViewConfigurationView Config : Configs)
	{
		Size.X += (int)Config.recommendedImageRectWidth;
		Size.Y = FMath::Max(Size.Y, (int)Config.recommendedImageRectHeight);
	}

	// We always prefer the nearest multiple of 4 for our buffer sizes. Make sure we round up here,
	// so we're consistent with the rest of the engine in creating our buffers.
	QuantizeSceneBufferSize(Size, Size);

	return Size;
}

FIntRect FOpenXRHMD::GetFullFlatEyeRect_RenderThread(FTexture2DRHIRef EyeTexture) const
{
	FVector2D SrcNormRectMin(0.05f, 0.2f);
	FVector2D SrcNormRectMax(0.45f, 0.8f);
	if (Configs.Num() > 2)
	{
		SrcNormRectMin.X /= 2;
		SrcNormRectMax.X /= 2;
	}

	return FIntRect(EyeTexture->GetSizeX() * SrcNormRectMin.X, EyeTexture->GetSizeY() * SrcNormRectMin.Y, EyeTexture->GetSizeX() * SrcNormRectMax.X, EyeTexture->GetSizeY() * SrcNormRectMax.Y);
}

void FOpenXRHMD::CopyTexture_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, FIntRect SrcRect, FRHITexture2D* DstTexture, FIntRect DstRect, bool bClearBlack, bool bNoAlpha) const
{
	check(IsInRenderingThread());

	const uint32 ViewportWidth = DstRect.Width();
	const uint32 ViewportHeight = DstRect.Height();
	const FIntPoint TargetSize(ViewportWidth, ViewportHeight);

	const float SrcTextureWidth = SrcTexture->GetSizeX();
	const float SrcTextureHeight = SrcTexture->GetSizeY();
	float U = 0.f, V = 0.f, USize = 1.f, VSize = 1.f;
	if (!SrcRect.IsEmpty())
	{
		U = SrcRect.Min.X / SrcTextureWidth;
		V = SrcRect.Min.Y / SrcTextureHeight;
		USize = SrcRect.Width() / SrcTextureWidth;
		VSize = SrcRect.Height() / SrcTextureHeight;
	}

	FRHITexture * ColorRT = DstTexture->GetTexture2D();
	FRHIRenderPassInfo RenderPassInfo(ColorRT, ERenderTargetActions::DontLoad_Store);
	RHICmdList.BeginRenderPass(RenderPassInfo, TEXT("OpenXRHMD_CopyTexture"));
	{
		if (bClearBlack)
		{
			const FIntRect ClearRect(0, 0, DstTexture->GetSizeX(), DstTexture->GetSizeY());
			RHICmdList.SetViewport(ClearRect.Min.X, ClearRect.Min.Y, 0, ClearRect.Max.X, ClearRect.Max.Y, 1.0f);
			DrawClearQuad(RHICmdList, FLinearColor::Black);
		}

		RHICmdList.SetViewport(DstRect.Min.X, DstRect.Min.Y, 0, DstRect.Max.X, DstRect.Max.Y, 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = bNoAlpha ? TStaticBlendState<>::GetRHI() : TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		const auto FeatureLevel = GMaxRHIFeatureLevel;
		auto ShaderMap = GetGlobalShaderMap(FeatureLevel);

		TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
		TShaderMapRef<FScreenPS> PixelShader(ShaderMap);

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		const bool bSameSize = DstRect.Size() == SrcRect.Size();
		if (bSameSize)
		{
			PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Point>::GetRHI(), SrcTexture);
		}
		else
		{
			PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Bilinear>::GetRHI(), SrcTexture);
		}

		RendererModule->DrawRectangle(
			RHICmdList,
			0, 0,
			ViewportWidth, ViewportHeight,
			U, V,
			USize, VSize,
			TargetSize,
			FIntPoint(1, 1),
			*VertexShader,
			EDRF_Default);
	}
	RHICmdList.EndRenderPass();
}

void FOpenXRHMD::RenderTexture_RenderThread(class FRHICommandListImmediate& RHICmdList, class FRHITexture2D* BackBuffer, class FRHITexture2D* SrcTexture, FVector2D WindowSize) const
{
	if (SpectatorScreenController)
	{
		SpectatorScreenController->RenderSpectatorScreen_RenderThread(RHICmdList, BackBuffer, SrcTexture, WindowSize);
	}
}

void FOpenXRHMD::DrawHiddenAreaMesh_RenderThread(class FRHICommandList& RHICmdList, EStereoscopicPass StereoPass) const
{
	check(IsInRenderingThread());
	check(StereoPass != eSSP_FULL);

#if 0
	const uint32 ViewIndex = GetViewIndexForPass(StereoPass);
	const FHMDViewMesh& Mesh = HiddenAreaMeshes[ViewIndex];
	check(Mesh.IsValid());

	RHICmdList.SetStreamSource(0, Mesh.VertexBufferRHI, 0);
	RHICmdList.DrawIndexedPrimitive(Mesh.IndexBufferRHI, 0, 0, Mesh.NumVertices, 0, Mesh.NumTriangles, 1);
#endif
}

//---------------------------------------------------
// OpenXR Action Space Implementation
//---------------------------------------------------

FOpenXRHMD::FActionSpace::FActionSpace(XrAction InAction)
	: Action(InAction)
	, Space(XR_NULL_HANDLE)
{
}

bool FOpenXRHMD::FActionSpace::CreateSpace(XrSession InSession)
{
	if (Action == XR_NULL_HANDLE || Space != XR_NULL_HANDLE)
	{
		return false;
	}

	XrActionSpaceCreateInfo ActionSpaceInfo;
	ActionSpaceInfo.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
	ActionSpaceInfo.next = nullptr;
	ActionSpaceInfo.subactionPath = XR_NULL_PATH;
	ActionSpaceInfo.poseInActionSpace = ToXrPose(FTransform::Identity);
	ActionSpaceInfo.action = Action;
	return XR_ENSURE(xrCreateActionSpace(InSession, &ActionSpaceInfo, &Space));
}

void FOpenXRHMD::FActionSpace::DestroySpace()
{
	if (Space)
	{
		XR_ENSURE(xrDestroySpace(Space));
	}
	Space = XR_NULL_HANDLE;
}
