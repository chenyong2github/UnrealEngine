// Copyright Epic Games, Inc. All Rights Reserved.

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

static TAutoConsoleVariable<int32> CVarEnableOpenXRValidationLayer(
	TEXT("xr.EnableOpenXRValidationLayer"),
	0,
	TEXT("If true, enables the OpenXR validation layer, which will provide extended validation of\nOpenXR API calls. This should only be used for debugging purposes.\n")
	TEXT("Changes will only take effect in new game/editor instances - can't be changed at runtime.\n"),
	ECVF_Default);		// @todo: Should we specify ECVF_Cheat here so this doesn't show up in release builds?


namespace {
	static TSet<XrEnvironmentBlendMode> SupportedBlendModes{ XR_ENVIRONMENT_BLEND_MODE_ADDITIVE, XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND, XR_ENVIRONMENT_BLEND_MODE_OPAQUE };
	static TSet<XrViewConfigurationType> SupportedViewConfigurations{ XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO };

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

private:
	bool HasLayer(const char* Name) const { return AvailableLayers.Contains(Name); }
	bool IsExtensionEnabled(const FString& Name) const { return EnabledExtensions.Contains(Name); }

	void *LoaderHandle;
	XrInstance Instance;
	XrSystemId System;
	TSet<FString> AvailableExtensions;
	TSet<FString> AvailableLayers;
	TArray<const char*> EnabledExtensions;
	TRefCountPtr<FOpenXRRenderBridge> RenderBridge;
	TSharedPtr< IHeadMountedDisplayVulkanExtensions, ESPMode::ThreadSafe > VulkanExtensions;

	bool EnumerateExtensions();
	bool EnumerateLayers();
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

	auto OpenXRHMD = FSceneViewExtensions::NewExtension<FOpenXRHMD>(Instance, System, RenderBridge, EnabledExtensions);
	if (OpenXRHMD->IsInitialized())
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
			return 0;
		}
	}
	return RenderBridge->GetGraphicsAdapterLuid();
}

TSharedPtr< IHeadMountedDisplayVulkanExtensions, ESPMode::ThreadSafe > FOpenXRHMDPlugin::GetVulkanExtensions()
{
#if XR_USE_GRAPHICS_API_VULKAN
	if (PreInit() && IsExtensionEnabled(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME))
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

bool FOpenXRHMDPlugin::EnumerateLayers()
{
	uint32 LayerPropertyCount = 0;
	if (XR_FAILED(xrEnumerateApiLayerProperties(0, &LayerPropertyCount, nullptr)))
	{
		// As per EnumerateExtensions - a failure here means no runtime installed.
		return false;
	}

	if (!LayerPropertyCount)
	{
		// It's still legit if we have no layers, so early out here (and return success) if so.
		return true;
	}

	TArray<XrApiLayerProperties> LayerProperties;
	LayerProperties.SetNum(LayerPropertyCount);
	for (auto& Prop : LayerProperties)
	{
		Prop = XrApiLayerProperties{ XR_TYPE_API_LAYER_PROPERTIES };
	}

	if (XR_ENSURE(xrEnumerateApiLayerProperties(LayerPropertyCount, &LayerPropertyCount, LayerProperties.GetData())))
	{
		for (const auto& Prop : LayerProperties)
		{
			AvailableLayers.Add(Prop.layerName);
		}
		return true;
	}

	return false;
}

bool FOpenXRHMDPlugin::InitRenderBridge()
{
#if PLATFORM_HOLOLENS
	FString RHIString = TEXT("DirectX 11");
#else
	FString RHIString = FApp::GetGraphicsRHI();
	if (RHIString.IsEmpty())
	{
		return false;
	}
#endif

#ifdef XR_USE_GRAPHICS_API_D3D11
	if (RHIString == TEXT("DirectX 11") && IsExtensionEnabled(XR_KHR_D3D11_ENABLE_EXTENSION_NAME))
	{
		RenderBridge = CreateRenderBridge_D3D11(Instance, System);
	}
	else
#endif
#ifdef XR_USE_GRAPHICS_API_D3D12
	if (RHIString == TEXT("DirectX 12") && IsExtensionEnabled(XR_KHR_D3D12_ENABLE_EXTENSION_NAME))
	{
		RenderBridge = CreateRenderBridge_D3D12(Instance, System);
	}
	else
#endif
#ifdef XR_USE_GRAPHICS_API_OPENGL
	if (RHIString == TEXT("OpenGL") && IsExtensionEnabled(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME))
	{
		RenderBridge = CreateRenderBridge_OpenGL(Instance, System);
	}
	else
#endif
#ifdef XR_USE_GRAPHICS_API_VULKAN
	if (RHIString == TEXT("Vulkan") && IsExtensionEnabled(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME))
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
#if !PLATFORM_CPU_X86_FAMILY
#error Windows platform does not currently support this CPU family. A OpenXR loader binary for this CPU family is needed.
#endif

#if PLATFORM_64BITS
	FString BinariesPath = FPaths::EngineDir() / FString(TEXT("Binaries/ThirdParty/OpenXR/win64"));
#else
	FString BinariesPath = FPaths::EngineDir() / FString(TEXT("Binaries/ThirdParty/OpenXR/win32"));
#endif

	FString LoaderName = FString::Printf(TEXT("openxr_loader-%d_%d.dll"), XR_VERSION_MAJOR(XR_CURRENT_API_VERSION), XR_VERSION_MINOR(XR_CURRENT_API_VERSION));
	FPlatformProcess::PushDllDirectory(*BinariesPath);
	LoaderHandle = FPlatformProcess::GetDllHandle(*LoaderName);
	FPlatformProcess::PopDllDirectory(*BinariesPath);
#elif PLATFORM_HOLOLENS
#ifndef PLATFORM_64BITS
#error HoloLens platform does not currently support 32-bit. 32-bit OpenXR loader binaries are needed.
#endif

#if PLATFORM_CPU_ARM_FAMILY
	FString BinariesPath = FPaths::EngineDir() / FString(TEXT("Binaries/ThirdParty/OpenXR/hololens/arm64"));
#elif PLATFORM_CPU_X86_FAMILY
	FString BinariesPath = FPaths::EngineDir() / FString(TEXT("Binaries/ThirdParty/OpenXR/hololens/x64"));
#else
#error Unsupported CPU family for the HoloLens platform.
#endif

	LoaderHandle = FPlatformProcess::GetDllHandle(*(BinariesPath / "openxr_loader.dll"));
#endif

	if (!LoaderHandle)
	{
		UE_LOG(LogHMD, Log, TEXT("Failed to load openxr_loader-%d_%d.dll"), XR_VERSION_MAJOR(XR_CURRENT_API_VERSION), XR_VERSION_MINOR(XR_CURRENT_API_VERSION));
		return false;
	}

	EnabledExtensions.Reset();

	if (!EnumerateExtensions())
	{
		UE_LOG(LogHMD, Log, TEXT("Failed to enumerate extensions. Please check that you have a valid OpenXR runtime installed."));
		return false;
	}

	if (!EnumerateLayers())
	{
		UE_LOG(LogHMD, Log, TEXT("Failed to enumerate API layers. Please check that you have a valid OpenXR runtime installed."));
		return false;
	}

#ifdef XR_USE_GRAPHICS_API_D3D11
	if (AvailableExtensions.Contains(XR_KHR_D3D11_ENABLE_EXTENSION_NAME))
	{
		EnabledExtensions.Add(XR_KHR_D3D11_ENABLE_EXTENSION_NAME);
	}
#endif
#ifdef XR_USE_GRAPHICS_API_D3D12
	if (AvailableExtensions.Contains(XR_KHR_D3D12_ENABLE_EXTENSION_NAME))
	{
		EnabledExtensions.Add(XR_KHR_D3D12_ENABLE_EXTENSION_NAME);
	}
#endif
#ifdef XR_USE_GRAPHICS_API_OPENGL
	if (AvailableExtensions.Contains(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME))
	{
		EnabledExtensions.Add(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
	}
#endif
#ifdef XR_USE_GRAPHICS_API_VULKAN
	if (AvailableExtensions.Contains(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME))
	{
		EnabledExtensions.Add(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);
	}
#endif

	if (AvailableExtensions.Contains(XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME))
	{
		EnabledExtensions.Add(XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME);
	}

	if (AvailableExtensions.Contains(XR_VARJO_QUAD_VIEWS_EXTENSION_NAME))
	{
		EnabledExtensions.Add(XR_VARJO_QUAD_VIEWS_EXTENSION_NAME);
	}

	if (AvailableExtensions.Contains(XR_KHR_VISIBILITY_MASK_EXTENSION_NAME))
	{
		EnabledExtensions.Add(XR_KHR_VISIBILITY_MASK_EXTENSION_NAME);
	}

	// Switch to constant once out of preview and there is a header available.
	if (AvailableExtensions.Contains("XR_MSFT_hand_interaction"))
	{
		EnabledExtensions.Add("XR_MSFT_hand_interaction");
	}

	// Enable layers, if specified by CVar.
	// Note: For the validation layer to work on Windows (as of latest OpenXR runtime, August 2019), the following are required:
	//   1. Download and build the OpenXR SDK from https://github.com/KhronosGroup/OpenXR-SDK-Source (follow instructions at https://github.com/KhronosGroup/OpenXR-SDK-Source/blob/master/BUILDING.md)
	//	 2. Add a registry key under HKEY_LOCAL_MACHINE\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Explicit, containing the path to the manifest file
	//      (e.g. C:\OpenXR-SDK-Source-master\build\win64\src\api_layers\XrApiLayer_core_validation.json) <-- this file is downloaded as part of the SDK source, above
	//   3. Copy the DLL from the build target at, for example, C:\OpenXR-SDK-Source-master\build\win64\src\api_layers\XrApiLayer_core_validation.dll to
	//      somewhere in your system path (e.g. c:\windows\system32); the OpenXR loader currently doesn't use the path the json file is in (this is a bug)

	const bool bEnableOpenXRValidationLayer = CVarEnableOpenXRValidationLayer.GetValueOnAnyThread() != 0;
	TArray<const char*> Layers;
	if (bEnableOpenXRValidationLayer && HasLayer("XR_APILAYER_LUNARG_core_validation"))
	{
		Layers.Add("XR_APILAYER_LUNARG_core_validation");
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

	Info.enabledApiLayerCount = Layers.Num();
	Info.enabledApiLayerNames = Layers.GetData();

	Info.enabledExtensionCount = EnabledExtensions.Num();
	Info.enabledExtensionNames = EnabledExtensions.GetData();

	XrResult rs = xrCreateInstance(&Info, &Instance);
	if (XR_FAILED(rs))
	{
		UE_LOG(LogHMD, Log, TEXT("Failed to create an OpenXR instance, result is %s. Please check if you have an OpenXR runtime installed."), OpenXRResultToString(rs));
		return false;
	}

	XrSystemGetInfo SystemInfo;
	SystemInfo.type = XR_TYPE_SYSTEM_GET_INFO;
	SystemInfo.next = nullptr;
	SystemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	rs = xrGetSystem(Instance, &SystemInfo, &System);
	if (XR_FAILED(rs))
	{
		UE_LOG(LogHMD, Log, TEXT("Failed to get an OpenXR system, result is %s. Please check that your runtime supports VR headsets."), OpenXRResultToString(rs));
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
		for (int32 i = 0; i < DeviceSpaces.Num(); i++)
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

	// This function is called from both the game and rendering thread and each thread maintains separate pose
	// snapshots to prevent inconsistent poses (tearing) on the same frame.
	const FPipelinedFrameState& PipelineState = GetPipelinedFrameStateForThread();

	if (!PipelineState.DeviceLocations.IsValidIndex(DeviceId))
	{
		return false;
	}

	const XrSpaceLocation& Location = PipelineState.DeviceLocations[DeviceId];
	if (Location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT &&
		Location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
	{
		CurrentOrientation = ToFQuat(Location.pose.orientation);
		CurrentPosition = ToFVector(Location.pose.position, GetWorldToMetersScale());
		return true;
	}

	return false;
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
	FIntPoint ViewRectMin(EForceInit::ForceInitToZero);
	if (!bIsMobileMultiViewEnabled)
	{
		for (uint32 i = 0; i < ViewIndex; ++i)
		{
			ViewRectMin.X += Configs[i].recommendedImageRectWidth;
		}
	}
	QuantizeSceneBufferSize(ViewRectMin, ViewRectMin);

	X = ViewRectMin.X;
	Y = ViewRectMin.Y;
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

uint32 FOpenXRHMD::DeviceGetLODViewIndex() const
{
	if (SelectedViewConfigurationType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO)
	{
		return GetViewIndexForPass(eSSP_LEFT_EYE_SIDE);
	}
	return IStereoRendering::DeviceGetLODViewIndex();
}

int32 FOpenXRHMD::GetDesiredNumberOfViews(bool bStereoRequested) const
{
	return bStereoRequested ? Configs.Num() : 1; // FIXME: Monoscopic actually needs 2 views for quad vr
}

bool FOpenXRHMD::GetRelativeEyePose(int32 InDeviceId, EStereoscopicPass InEye, FQuat& OutOrientation, FVector& OutPosition)
{
	if (InDeviceId != IXRTrackingSystem::HMDDeviceId)
	{
		return false;
	}

	const FPipelinedFrameState& FrameState = GetPipelinedFrameStateForThread();

	if (FrameState.ViewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT &&
		FrameState.ViewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT)
	{
		const uint32 ViewIndex = GetViewIndexForPass(InEye);
		OutOrientation = ToFQuat(FrameState.Views[ViewIndex].pose.orientation);
		OutPosition = ToFVector(FrameState.Views[ViewIndex].pose.position, GetWorldToMetersScale());
		return true;
	}

	return false;
}

FMatrix FOpenXRHMD::GetStereoProjectionMatrix(const enum EStereoscopicPass StereoPassType) const
{
	const uint32 ViewIndex = GetViewIndexForPass(StereoPassType);

	const FPipelinedFrameState& FrameState = GetPipelinedFrameStateForThread();
	XrFovf Fov = FrameState.Views[ViewIndex].fov;

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

FOpenXRHMD::FOpenXRHMD(const FAutoRegister& AutoRegister, XrInstance InInstance, XrSystemId InSystem, TRefCountPtr<FOpenXRRenderBridge>& InRenderBridge, TArray<const char*> InEnabledExtensions)
	: FHeadMountedDisplayBase(nullptr)
	, FSceneViewExtensionBase(AutoRegister)
	, bStereoEnabled(false)
	, bIsRunning(false)
	, bIsReady(false)
	, bIsRendering(false)
	, bRunRequested(false)
	, bNeedReAllocatedDepth(false)
	, CurrentSessionState(XR_SESSION_STATE_UNKNOWN)
	, EnabledExtensions(std::move(InEnabledExtensions))
	, Instance(InInstance)
	, System(InSystem)
	, Session(XR_NULL_HANDLE)
	, LocalSpace(XR_NULL_HANDLE)
	, StageSpace(XR_NULL_HANDLE)
	, TrackingSpaceType(XR_REFERENCE_SPACE_TYPE_STAGE)
	, SelectedViewConfigurationType(XR_VIEW_CONFIGURATION_TYPE_MAX_ENUM)
	, SelectedEnvironmentBlendMode(XR_ENVIRONMENT_BLEND_MODE_MAX_ENUM)
	, RenderBridge(InRenderBridge)
	, RendererModule(nullptr)
	, LastRequestedSwapchainFormat(0)
	, LastRequestedDepthSwapchainFormat(0)
{
#if PLATFORM_HOLOLENS
	bDepthExtensionSupported = bHiddenAreaMaskSupported = false;

	static const auto CVarMobileMultiView = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("vr.MobileMultiView"));
	static const auto CVarMobileHDR = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileHDR"));
	const bool bMobileHDR = (CVarMobileHDR && CVarMobileHDR->GetValueOnAnyThread() != 0);
	bIsMobileMultiViewEnabled = (GRHISupportsArrayIndexFromAnyShader && !bMobileHDR) && (CVarMobileMultiView && CVarMobileMultiView->GetValueOnAnyThread() != 0);
#else
	bDepthExtensionSupported = IsExtensionEnabled(XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME);
	bHiddenAreaMaskSupported = false;
	bIsMobileMultiViewEnabled = false;
#endif

	{
		// Enumerate the viewport configurations
		uint32 ConfigurationCount;
		TArray<XrViewConfigurationType> ViewConfigTypes;
		XR_ENSURE(xrEnumerateViewConfigurations(Instance, System, 0, &ConfigurationCount, nullptr));
		ViewConfigTypes.SetNum(ConfigurationCount);
		// Fill the initial array with valid enum types (this will fail in the validation layer otherwise).
		for (auto & TypeIter : ViewConfigTypes)
			TypeIter = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO;
		XR_ENSURE(xrEnumerateViewConfigurations(Instance, System, ConfigurationCount, &ConfigurationCount, ViewConfigTypes.GetData()));

		// Select the first view configuration returned by the runtime that is supported.
		// This is the view configuration preferred by the runtime.
		for (XrViewConfigurationType ViewConfigType : ViewConfigTypes)
		{
			if (SupportedViewConfigurations.Contains(ViewConfigType))
			{
				SelectedViewConfigurationType = ViewConfigType;
				break;
			}
		}

		// If there is no supported view configuration type, use the first option as a last resort.
		if (!ensure(SelectedViewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_MAX_ENUM))
		{
			SelectedViewConfigurationType = ViewConfigTypes[0];
		}

		// Enumerate the viewport configuration views
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
	PipelinedFrameStateGame.Views.SetNum(Configs.Num());
	for (XrView& View : PipelinedFrameStateGame.Views)
	{
		View.type = XR_TYPE_VIEW;
		View.next = nullptr;
		View.fov = XrFovf{ -PI / 4.0f, PI / 4.0f, PI / 4.0f, -PI / 4.0f };
		View.pose = ToXrPose(FTransform::Identity);
	}

	// Enumerate environment blend modes and select the best one.
	{
		uint32 BlendModeCount;
		TArray<XrEnvironmentBlendMode> BlendModes;
		XR_ENSURE(xrEnumerateEnvironmentBlendModes(Instance, System, SelectedViewConfigurationType, 0, &BlendModeCount, nullptr));
		// Fill the initial array with valid enum types (this will fail in the validation layer otherwise).
		for (auto& TypeIter : BlendModes)
			TypeIter = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
		BlendModes.SetNum(BlendModeCount);
		XR_ENSURE(xrEnumerateEnvironmentBlendModes(Instance, System, SelectedViewConfigurationType, BlendModeCount, &BlendModeCount, BlendModes.GetData()));

		// Select the first blend mode returned by the runtime that is supported.
		// This is the environment blend mode preferred by the runtime.
		for (XrEnvironmentBlendMode BlendMode : BlendModes)
		{
			if (SupportedBlendModes.Contains(BlendMode))
			{
				SelectedEnvironmentBlendMode = BlendMode;
				break;
			}
		}

		// If there is no supported environment blend mode, use the first option as a last resort.
		if (!ensure(SelectedEnvironmentBlendMode != XR_ENVIRONMENT_BLEND_MODE_MAX_ENUM))
		{
			SelectedEnvironmentBlendMode = BlendModes[0];
		}
	}

	// Add a device space for the HMD without an action handle and ensure it has the correct index
	ensure(DeviceSpaces.Emplace(XR_NULL_HANDLE) == HMDDeviceId);

	// Give the RHI frame state the same initial values.
	PipelinedFrameStateRHI = PipelinedFrameStateGame;
}

FOpenXRHMD::~FOpenXRHMD()
{
	if (Session)
	{
		XR_ENSURE(xrDestroySession(Session));
	}
}


const FOpenXRHMD::FPipelinedFrameState& FOpenXRHMD::GetPipelinedFrameStateForThread() const
{
	if (IsInRenderingThread())
	{
		return PipelinedFrameStateRHI;
	}
	else
	{
		ensure(IsInGameThread());
		return PipelinedFrameStateGame;
	}
}

FOpenXRHMD::FPipelinedFrameState& FOpenXRHMD::GetPipelinedFrameStateForThread()
{
	if (IsInRenderingThread())
	{
		return PipelinedFrameStateRHI;
	}
	else
	{
		ensure(IsInGameThread());
		return PipelinedFrameStateGame;
	}
}

void FOpenXRHMD::UpdateDeviceLocations()
{
	FPipelinedFrameState& PipelineState = GetPipelinedFrameStateForThread();

	PipelineState.DeviceLocations.SetNum(DeviceSpaces.Num());
	for (int32 DeviceIndex = 0; DeviceIndex < PipelineState.DeviceLocations.Num(); DeviceIndex++)
	{
		const FDeviceSpace& DeviceSpace = DeviceSpaces[DeviceIndex];

		XrSpaceLocation& DeviceLocation = PipelineState.DeviceLocations[DeviceIndex];
		DeviceLocation.type = XR_TYPE_SPACE_LOCATION;
		DeviceLocation.next = nullptr;
		XR_ENSURE(xrLocateSpace(DeviceSpace.Space, PipelineState.TrackingSpace, PipelineState.FrameState.predictedDisplayTime, &DeviceLocation));
	}
}

#if !PLATFORM_HOLOLENS
void FOpenXRHMD::BuildOcclusionMeshes()
{
	HiddenAreaMeshes.SetNum(Configs.Num());
	VisibleAreaMeshes.SetNum(Configs.Num());

	bool bSucceeded = true;

	for (int View = 0; View < Configs.Num(); ++View)
	{
		if (!BuildOcclusionMesh(XR_VISIBILITY_MASK_TYPE_VISIBLE_TRIANGLE_MESH_KHR, View, VisibleAreaMeshes[View]) ||
			!BuildOcclusionMesh(XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR, View, HiddenAreaMeshes[View]))
		{
			bSucceeded = false;
			break;
		}
	}

	if (!bSucceeded)
	{
		UE_LOG(LogHMD, Error, TEXT("Failed to create all visibility mask meshes for device/views. Abandoning visibility mask."));

		HiddenAreaMeshes.Empty();
		VisibleAreaMeshes.Empty();
	}
}

bool FOpenXRHMD::BuildOcclusionMesh(XrVisibilityMaskTypeKHR Type, int View, FHMDViewMesh& Mesh)
{
	XrVisibilityMaskKHR VisibilityMask = { XR_TYPE_VISIBILITY_MASK_KHR };
	XR_ENSURE(xrGetVisibilityMaskKHR(Session, SelectedViewConfigurationType, View, Type, &VisibilityMask));

	if (!VisibilityMask.indexCountOutput || (VisibilityMask.indexCountOutput % 3) != 0 || VisibilityMask.vertexCountOutput == 0)
	{
		UE_LOG(LogHMD, Error, TEXT("Visibility Mask Mesh returned from runtime is invalid."));
		return false;
	}

	FRHIResourceCreateInfo CreateInfo;
	Mesh.VertexBufferRHI = RHICreateVertexBuffer(sizeof(FFilterVertex) * VisibilityMask.vertexCountOutput, BUF_Static, CreateInfo);
	void* VertexBufferPtr = RHILockVertexBuffer(Mesh.VertexBufferRHI, 0, sizeof(FFilterVertex) * VisibilityMask.vertexCountOutput, RLM_WriteOnly);
	FFilterVertex* Vertices = reinterpret_cast<FFilterVertex*>(VertexBufferPtr);

	Mesh.IndexBufferRHI = RHICreateIndexBuffer(sizeof(uint32), sizeof(uint32) * VisibilityMask.indexCountOutput, BUF_Static, CreateInfo);
	void* IndexBufferPtr = RHILockIndexBuffer(Mesh.IndexBufferRHI, 0, sizeof(uint32) * VisibilityMask.indexCountOutput, RLM_WriteOnly);

	uint32* OutIndices = reinterpret_cast<uint32*>(IndexBufferPtr);
	TUniquePtr<XrVector2f[]> const OutVertices = MakeUnique<XrVector2f[]>(VisibilityMask.vertexCountOutput);

	VisibilityMask.vertexCapacityInput = VisibilityMask.vertexCountOutput;
	VisibilityMask.indexCapacityInput = VisibilityMask.indexCountOutput;
	VisibilityMask.indices = OutIndices;
	VisibilityMask.vertices = OutVertices.Get();

	xrGetVisibilityMaskKHR(Session, SelectedViewConfigurationType, View, Type, &VisibilityMask);

	ensure(VisibilityMask.vertexCapacityInput == VisibilityMask.vertexCountOutput);
	ensure(VisibilityMask.indexCapacityInput == VisibilityMask.indexCountOutput);

	if (Type == XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR)
	{
		// For the hidden-area mesh, bias z to the near plane so we occlude everything, and re-bias vertices to the -0.5 to 0.5 range in x and y.
		for (uint32 VertexIndex = 0; VertexIndex < VisibilityMask.vertexCountOutput; ++VertexIndex)
		{
			FFilterVertex& Vertex = Vertices[VertexIndex];
			const XrVector2f& Position = OutVertices[VertexIndex];

			Vertex.Position.X = (Position.x * 2.0f) - 1.0f;
			Vertex.Position.Y = (Position.y * 2.0f) - 1.0f;
			Vertex.Position.Z = 1.0f;
			Vertex.Position.W = 1.0f;
			Vertex.UV.X = Vertex.UV.Y = 0.0f;
		}
	}
	else if (Type == XR_VISIBILITY_MASK_TYPE_VISIBLE_TRIANGLE_MESH_KHR)
	{
		// For the visible-area mesh, this will be consumed by the post-process pipeline, so set up coordinates in the space they expect
		// (x and y range from 0-1, y reversed, z at the far plane).
		for (uint32 VertexIndex = 0; VertexIndex < VisibilityMask.vertexCountOutput; ++VertexIndex)
		{
			FFilterVertex& Vertex = Vertices[VertexIndex];
			const XrVector2f& Position = OutVertices[VertexIndex];

			Vertex.Position.X = Position.x;
			Vertex.Position.Y = 1.0f - Position.y;
			Vertex.Position.Z = 0.0f;
			Vertex.Position.W = 1.0f;

			Vertex.UV.X = Position.x;
			Vertex.UV.Y = 1.0f - Position.y;
		}
	}
	else
	{
		ensureMsgf(false, TEXT("FOpenXRHMD::BuildOcclusionMesh called with unsupported visibility mask type."));
	}

	Mesh.NumIndices = VisibilityMask.indexCountOutput;
	Mesh.NumVertices = VisibilityMask.vertexCountOutput;
	Mesh.NumTriangles = Mesh.NumIndices / 3;

	RHIUnlockVertexBuffer(Mesh.VertexBufferRHI);
	RHIUnlockIndexBuffer(Mesh.IndexBufferRHI);

	return true;
}
#endif

bool FOpenXRHMD::OnStereoStartup()
{
	XrSessionCreateInfo SessionInfo;
	SessionInfo.type = XR_TYPE_SESSION_CREATE_INFO;
	SessionInfo.next = RenderBridge->GetGraphicsBinding();
	SessionInfo.createFlags = 0;
	SessionInfo.systemId = System;
	XR_ENSURE(xrCreateSession(Instance, &SessionInfo, &Session));

#if !PLATFORM_HOLOLENS
	if (bHiddenAreaMaskSupported)
	{
		FOpenXRHMD* Self = this;
		ENQUEUE_RENDER_COMMAND(OpenXRCreateSession)([Self](FRHICommandListImmediate& RHICmdList)
			{
				Self->BuildOcclusionMeshes();
			});

		FlushRenderingCommands();
	}
#endif

	uint32_t ReferenceSpacesCount;
	XR_ENSURE(xrEnumerateReferenceSpaces(Session, 0, &ReferenceSpacesCount, nullptr));

	TArray<XrReferenceSpaceType> Spaces;
	Spaces.SetNum(ReferenceSpacesCount);
	// Initialize spaces array with valid enum values (avoid triggering validation error).
	for (auto & SpaceIter : Spaces)
		SpaceIter = XR_REFERENCE_SPACE_TYPE_VIEW;
	XR_ENSURE(xrEnumerateReferenceSpaces(Session, (uint32_t)Spaces.Num(), &ReferenceSpacesCount, Spaces.GetData()));
	ensure(ReferenceSpacesCount == Spaces.Num());

	XrSpace HmdSpace = XR_NULL_HANDLE;
	XrReferenceSpaceCreateInfo SpaceInfo;

	ensure(Spaces.Contains(XR_REFERENCE_SPACE_TYPE_VIEW));
	SpaceInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
	SpaceInfo.next = nullptr;
	SpaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	SpaceInfo.poseInReferenceSpace = ToXrPose(FTransform::Identity);
	XR_ENSURE(xrCreateReferenceSpace(Session, &SpaceInfo, &HmdSpace));
	DeviceSpaces[HMDDeviceId].Space = HmdSpace;

	ensure(Spaces.Contains(XR_REFERENCE_SPACE_TYPE_LOCAL));
	SpaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	XR_ENSURE(xrCreateReferenceSpace(Session, &SpaceInfo, &LocalSpace));

	// Prefer a stage space over a local space
	if (Spaces.Contains(XR_REFERENCE_SPACE_TYPE_STAGE))
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
	for (FDeviceSpace& DeviceSpace : DeviceSpaces)
	{
		DeviceSpace.CreateSpace(Session);
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
		XrResult Result = xrRequestExitSession(Session);
		if (Result == XR_ERROR_SESSION_NOT_RUNNING)
		{
			// Session was never running - most likely PIE without putting the headset on.
			CloseSession();
		}
		else
		{
			XR_ENSURE(Result);
		}
	}

	return true;
}

void FOpenXRHMD::CloseSession()
{
	if (Session != XR_NULL_HANDLE)
	{
		Swapchain.Reset();
		DepthSwapchain.Reset();

		// Clear up device spaces
		for (FDeviceSpace& DeviceSpace : DeviceSpaces)
		{
			DeviceSpace.DestroySpace();
		}

		// Close the session now we're allowed to.
		XR_ENSURE(xrDestroySession(Session));
		Session = XR_NULL_HANDLE;

		FlushRenderingCommands();

		bIsRendering = false;
		bIsReady = false;
		bIsRunning = false;
		bRunRequested = false;
		bNeedReAllocatedDepth = true;
	}
}

int32 FOpenXRHMD::AddActionDevice(XrAction Action)
{
	int32 DeviceId = DeviceSpaces.Emplace(Action);
	if (Session)
	{
		DeviceSpaces[DeviceId].CreateSpace(Session);
	}

	return DeviceId;
}

void FOpenXRHMD::ResetActionDevices()
{
	// Index 0 is HMDDeviceId and is preserved. The remaining are action devices.
	if (DeviceSpaces.Num() > 0)
	{
		DeviceSpaces.RemoveAt(HMDDeviceId + 1, DeviceSpaces.Num() - 1);
	}
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

	// We need to ensure we can sample from the texture in CopyTexture
	Flags |= TexCreate_ShaderResource;

	if (Swapchain == nullptr || Format != LastRequestedSwapchainFormat || Swapchain->GetTexture2D()->GetSizeX() != SizeX || Swapchain->GetTexture2D()->GetSizeY() != SizeY)
	{
		Swapchain = RenderBridge->CreateSwapchain(Session, Format, SizeX, SizeY, bIsMobileMultiViewEnabled ? 2 : 1, NumMips, NumSamples, Flags, TargetableTextureFlags);
		if (!Swapchain)
		{
			return false;
		}
	}

	// Grab the presentation texture out of the swapchain.
	OutTargetableTexture = OutShaderResourceTexture = (FTexture2DRHIRef&)Swapchain->GetTextureRef();
	LastRequestedSwapchainFormat = Format;

	bNeedReAllocatedDepth = bDepthExtensionSupported;

	return true;
}

bool FOpenXRHMD::AllocateDepthTexture(uint32 Index, uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 InTexFlags, uint32 TargetableTextureFlags, FTexture2DRHIRef& OutTargetableTexture, FTexture2DRHIRef& OutShaderResourceTexture, uint32 NumSamples)
{
	// FIXME: UE4 constantly calls this function even when there is no reason to reallocate the depth texture
	if (!bDepthExtensionSupported || !bNeedReAllocatedDepth)
	{
		return false;
	}

	if (DepthSwapchain == nullptr || Format != LastRequestedDepthSwapchainFormat || DepthSwapchain->GetTexture2D()->GetSizeX() != SizeX || DepthSwapchain->GetTexture2D()->GetSizeY() != SizeY)
	{
		DepthSwapchain = RenderBridge->CreateSwapchain(Session, PF_DepthStencil, SizeX, SizeY, bIsMobileMultiViewEnabled ? 2 : 1, FMath::Max(NumMips, 1u), NumSamples, 0, TexCreate_DepthStencilTargetable);
		if (!DepthSwapchain)
		{
			return false;
		}
	}

	bNeedReAllocatedDepth = false;

	OutTargetableTexture = OutShaderResourceTexture = (FTexture2DRHIRef&)DepthSwapchain->GetTextureRef();
	LastRequestedDepthSwapchainFormat = Format;

	return true;
}

void FOpenXRHMD::OnBeginRendering_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& ViewFamily)
{
	PipelinedFrameStateRHI = PipelinedFrameStateGame;

	ensure(IsInRenderingThread());
	FPipelinedFrameState& PipelineState = GetPipelinedFrameStateForThread();

	const FSceneView* MainView = ViewFamily.Views[0];
	check(MainView);
	const FTransform BaseTransform = FTransform(MainView->BaseHmdOrientation, MainView->BaseHmdLocation);

	ProjectionViewsRHI.SetNum(PipelineState.Views.Num());
	DepthLayersRHI.SetNum(PipelineState.Views.Num());

	FIntPoint ViewRectMin(EForceInit::ForceInitToZero);
	float NearZ = GNearClippingPlane / GetWorldToMetersScale();

	for (int32 ViewIndex = 0; ViewIndex < PipelineState.Views.Num(); ViewIndex++)
	{
		const XrView& View = PipelineState.Views[ViewIndex];
		const XrViewConfigurationView& Config = Configs[ViewIndex];

		FIntRect ViewRect;
		QuantizeSceneBufferSize(ViewRectMin, ViewRect.Min);
		ViewRect.Max = ViewRect.Min + FIntPoint(Config.recommendedImageRectWidth, Config.recommendedImageRectHeight);
		FTransform ViewTransform = ToFTransform(View.pose, GetWorldToMetersScale());

		XrCompositionLayerProjectionView& Projection = ProjectionViewsRHI[ViewIndex];
		XrCompositionLayerDepthInfoKHR& DepthLayer = DepthLayersRHI[ViewIndex];

		Projection.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
		Projection.next = nullptr;
		Projection.fov = View.fov;
		Projection.pose = ToXrPose(ViewTransform * BaseTransform, GetWorldToMetersScale());
		Projection.subImage.swapchain = static_cast<FOpenXRSwapchain*>(GetSwapchain())->GetHandle();
		Projection.subImage.imageArrayIndex = bIsMobileMultiViewEnabled ? ViewIndex : 0;
		Projection.subImage.imageRect = {
			{ ViewRect.Min.X, ViewRect.Min.Y },
			{ ViewRect.Width(), ViewRect.Height() }
		};

		if (bDepthExtensionSupported && !bNeedReAllocatedDepth)
		{
			DepthLayer.type = XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR;
			DepthLayer.next = nullptr;
			DepthLayer.subImage.swapchain = static_cast<FOpenXRSwapchain*>(GetDepthSwapchain())->GetHandle();
			DepthLayer.subImage.imageArrayIndex = bIsMobileMultiViewEnabled ? ViewIndex : 0;
			DepthLayer.subImage.imageRect = bIsMobileMultiViewEnabled ? ProjectionViewsRHI[0].subImage.imageRect : Projection.subImage.imageRect;
			DepthLayer.minDepth = 0.0f;
			DepthLayer.maxDepth = 1.0f;
			DepthLayer.nearZ = FLT_MAX;
			DepthLayer.farZ = NearZ;

			Projection.next = &DepthLayer;
		}

		ViewRectMin.X += bIsMobileMultiViewEnabled ? 0 : Config.recommendedImageRectWidth;
	}

	// xrBeginFrame is called _AFTER_ data the above data is cloned for RHI because xrBeginFrame will unblock xrWaitFrame in the game thread
	// and that could otherwise lead to data races.
	if (bIsReady)
	{
		XrFrameBeginInfo BeginInfo;
		BeginInfo.type = XR_TYPE_FRAME_BEGIN_INFO;
		BeginInfo.next = nullptr;
		XrResult Result = xrBeginFrame(Session, &BeginInfo);
		if (XR_SUCCEEDED(Result))
		{
			bIsRendering = true;

			Swapchain->IncrementSwapChainIndex_RHIThread(PipelineState.FrameState.predictedDisplayPeriod);
			if (bDepthExtensionSupported && !bNeedReAllocatedDepth)
			{
				ensure(DepthSwapchain != nullptr);
				DepthSwapchain->IncrementSwapChainIndex_RHIThread(PipelineState.FrameState.predictedDisplayPeriod);
			}
		}
		else
		{
			static bool bLoggedBeginFrameFailure = false;
			if (!bLoggedBeginFrameFailure)
			{
				UE_LOG(LogHMD, Error, TEXT("Unexpected error on xrBeginFrame. Error code was %s."), OpenXRResultToString(Result));
				bLoggedBeginFrameFailure = true;
			}
		}
	}

	// Snapshot new poses for late update.
	UpdateDeviceLocations();
}

void FOpenXRHMD::OnLateUpdateApplied_RenderThread(const FTransform& NewRelativeTransform)
{
	FHeadMountedDisplayBase::OnLateUpdateApplied_RenderThread(NewRelativeTransform);

	ensure(IsInRenderingThread());
	const FPipelinedFrameState& PipelineState = GetPipelinedFrameStateForThread();

	for (int32 ViewIndex = 0; ViewIndex < PipelineState.Views.Num(); ViewIndex++)
	{
		const XrView& View = PipelineState.Views[ViewIndex];
		XrCompositionLayerProjectionView& Projection = ProjectionViewsRHI[ViewIndex];

		FTransform ViewTransform = ToFTransform(View.pose, GetWorldToMetersScale());
		Projection.pose = ToXrPose(ViewTransform * NewRelativeTransform, GetWorldToMetersScale());
	}
}

void FOpenXRHMD::OnBeginRendering_GameThread()
{
	if (!bIsReady || !bIsRunning)
	{
		// @todo: Sleep here?
		return;
	}

	ensure(IsInGameThread());

	XrFrameWaitInfo WaitInfo;
	WaitInfo.type = XR_TYPE_FRAME_WAIT_INFO;
	WaitInfo.next = nullptr;

	XrFrameState FrameState{XR_TYPE_FRAME_STATE};
	XR_ENSURE(xrWaitFrame(Session, &WaitInfo, &FrameState));

	// The pipeline state on the game thread can only be safely modified after xrWaitFrame which will be unblocked by
	// the runtime when xrBeginFrame is called. The rendering thread will clone the game pipeline state before calling
	// xrBeginFrame so the game pipeline state can safely be modified after xrWaitFrame returns.
	FPipelinedFrameState& PipelineState = GetPipelinedFrameStateForThread();
	PipelineState.FrameState = FrameState;
	PipelineState.TrackingSpace = GetTrackingSpace();

	uint32_t ViewCount = 0;
	XrViewLocateInfo ViewInfo;
	ViewInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
	ViewInfo.next = nullptr;
	ViewInfo.viewConfigurationType = SelectedViewConfigurationType;
	ViewInfo.space = DeviceSpaces[HMDDeviceId].Space;
	ViewInfo.displayTime = PipelineState.FrameState.predictedDisplayTime;
	XR_ENSURE(xrLocateViews(Session, &ViewInfo, &PipelineState.ViewState, 0, &ViewCount, nullptr));
	PipelineState.Views.SetNum(ViewCount);
	XR_ENSURE(xrLocateViews(Session, &ViewInfo, &PipelineState.ViewState, PipelineState.Views.Num(), &ViewCount, PipelineState.Views.GetData()));

	UpdateDeviceLocations();
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

			CloseSession();

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
	ensure(IsInRenderingThread());
	const FPipelinedFrameState& PipelineState = GetPipelinedFrameStateForThread();

	XrCompositionLayerProjection Layer = {};
	Layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
	Layer.next = nullptr;
	Layer.space = PipelineState.TrackingSpace;
	Layer.viewCount = ProjectionViewsRHI.Num();
	Layer.views = ProjectionViewsRHI.GetData();

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
		EndInfo.displayTime = PipelineState.FrameState.predictedDisplayTime;
		EndInfo.environmentBlendMode = SelectedEnvironmentBlendMode;
		EndInfo.layerCount = 1;
		EndInfo.layers = reinterpret_cast<XrCompositionLayerBaseHeader**>(Headers);
		XrResult Result = xrEndFrame(Session, &EndInfo);

		// Ignore invalid call order for now, we will recover on the next frame
		ensure(XR_SUCCEEDED(Result) || Result == XR_ERROR_CALL_ORDER_INVALID);

		bIsRendering = false;
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
		Size.X = bIsMobileMultiViewEnabled ? FMath::Max(Size.X, (int)Config.recommendedImageRectWidth)
			: Size.X + (int)Config.recommendedImageRectWidth;
		Size.Y = FMath::Max(Size.Y, (int)Config.recommendedImageRectHeight);

		// We always prefer the nearest multiple of 4 for our buffer sizes. Make sure we round up here,
		// so we're consistent with the rest of the engine in creating our buffers.
		QuantizeSceneBufferSize(Size, Size);
	}

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
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

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
			VertexShader,
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

bool FOpenXRHMD::HasHiddenAreaMesh() const
{
	return HiddenAreaMeshes.Num() > 0;
}

bool FOpenXRHMD::HasVisibleAreaMesh() const
{
	return VisibleAreaMeshes.Num() > 0;
}

void FOpenXRHMD::DrawHiddenAreaMesh_RenderThread(class FRHICommandList& RHICmdList, EStereoscopicPass StereoPass) const
{
	check(IsInRenderingThread());
	check(StereoPass != eSSP_FULL);

	const uint32 ViewIndex = GetViewIndexForPass(StereoPass);
	check(ViewIndex < (uint32)HiddenAreaMeshes.Num());
	const FHMDViewMesh& Mesh = HiddenAreaMeshes[ViewIndex];
	check(Mesh.IsValid());

	RHICmdList.SetStreamSource(0, Mesh.VertexBufferRHI, 0);
	RHICmdList.DrawIndexedPrimitive(Mesh.IndexBufferRHI, 0, 0, Mesh.NumVertices, 0, Mesh.NumTriangles, 1);
}

void FOpenXRHMD::DrawVisibleAreaMesh_RenderThread(class FRHICommandList& RHICmdList, EStereoscopicPass StereoPass) const
{
	check(IsInRenderingThread());
	check(StereoPass != eSSP_FULL);

	const uint32 ViewIndex = GetViewIndexForPass(StereoPass);
	check(ViewIndex < (uint32)VisibleAreaMeshes.Num());
	const FHMDViewMesh& Mesh = VisibleAreaMeshes[ViewIndex];
	check(Mesh.IsValid());

	RHICmdList.SetStreamSource(0, Mesh.VertexBufferRHI, 0);
	RHICmdList.DrawIndexedPrimitive(Mesh.IndexBufferRHI, 0, 0, Mesh.NumVertices, 0, Mesh.NumTriangles, 1);
}

//---------------------------------------------------
// OpenXR Action Space Implementation
//---------------------------------------------------

FOpenXRHMD::FDeviceSpace::FDeviceSpace(XrAction InAction)
	: Action(InAction)
	, Space(XR_NULL_HANDLE)
{
}

bool FOpenXRHMD::FDeviceSpace::CreateSpace(XrSession InSession)
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

void FOpenXRHMD::FDeviceSpace::DestroySpace()
{
	if (Space)
	{
		XR_ENSURE(xrDestroySpace(Space));
	}
	Space = XR_NULL_HANDLE;
}
