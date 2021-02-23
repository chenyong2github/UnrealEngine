// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DynamicRHI.cpp: Dynamically bound Render Hardware Interface implementation.
=============================================================================*/

#include "CoreMinimal.h"
#include "Misc/MessageDialog.h"
#include "Misc/OutputDeviceRedirector.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "RHI.h"
#include "Modules/ModuleManager.h"
#include "GenericPlatform/GenericPlatformDriver.h"
#include "GenericPlatform/GenericPlatformCrashContext.h"
#include "PipelineStateCache.h"

#if NV_GEFORCENOW
#include "GeForceNOWWrapper.h"
#endif

IMPLEMENT_TYPE_LAYOUT(FRayTracingGeometryInitializer);
IMPLEMENT_TYPE_LAYOUT(FRayTracingGeometrySegment);

#ifndef PLATFORM_ALLOW_NULL_RHI
	#define PLATFORM_ALLOW_NULL_RHI		0
#endif

// Globals.
FDynamicRHI* GDynamicRHI = NULL;

static TAutoConsoleVariable<int32> CVarWarnOfBadDrivers(
	TEXT("r.WarnOfBadDrivers"),
	1,
	TEXT("On engine startup we can check the current GPU driver and warn the user about issues and suggest a specific version\n")
	TEXT("The test is fast so this should not cost any performance.\n")
	TEXT(" 0: off\n")
	TEXT(" 1: a message on startup might appear (default)\n")
	TEXT(" 2: Simulating the system has a blacklisted NVIDIA driver (UI should appear)\n")
	TEXT(" 3: Simulating the system has a blacklisted AMD driver (UI should appear)\n")
	TEXT(" 4: Simulating the system has a not blacklisted AMD driver (no UI should appear)\n")
	TEXT(" 5: Simulating the system has a Intel driver (no UI should appear)"),
	ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarDisableDriverWarningPopupIfGFN(
	TEXT("r.DisableDriverWarningPopupIfGFN"),
	1,
	TEXT("If non-zero, disable driver version warning popup if running on a GFN cloud machine."),
	ECVF_RenderThreadSafe);

void InitNullRHI()
{
	// Use the null RHI if it was specified on the command line, or if a commandlet is running.
	IDynamicRHIModule* DynamicRHIModule = &FModuleManager::LoadModuleChecked<IDynamicRHIModule>(TEXT("NullDrv"));
	// Create the dynamic RHI.
	if ((DynamicRHIModule == 0) || !DynamicRHIModule->IsSupported())
	{
		FMessageDialog::Open( EAppMsgType::Ok, NSLOCTEXT("DynamicRHI", "NullDrvFailure", "NullDrv failure?"));
		FPlatformMisc::RequestExit(1);
	}

	GDynamicRHI = DynamicRHIModule->CreateRHI();
	GDynamicRHI->Init();

	// Command lists need the validation RHI context if enabled, so call the global scope version of RHIGetDefaultContext() and RHIGetDefaultAsyncComputeContext().
	GRHICommandList.GetImmediateCommandList().SetContext(::RHIGetDefaultContext());
	GRHICommandList.GetImmediateAsyncComputeCommandList().SetComputeContext(::RHIGetDefaultAsyncComputeContext());

	GUsingNullRHI = true;
	GRHISupportsTextureStreaming = false;

	// Update the crash context analytics
	FGenericCrashContext::SetEngineData(TEXT("RHI.RHIName"), TEXT("NullRHI"));
}

#if PLATFORM_WINDOWS
static void RHIDetectAndWarnOfBadDrivers(bool bHasEditorToken)
{
	int32 CVarValue = CVarWarnOfBadDrivers.GetValueOnGameThread();

	if(!GIsRHIInitialized || !CVarValue || GRHIVendorId == 0)
	{
		return;
	}

	FGPUDriverInfo DriverInfo;

	// later we should make the globals use the struct directly
	DriverInfo.VendorId = GRHIVendorId;
	DriverInfo.DeviceDescription = GRHIAdapterName;
	DriverInfo.ProviderName = TEXT("Unknown");
	DriverInfo.InternalDriverVersion = GRHIAdapterInternalDriverVersion;
	DriverInfo.UserDriverVersion = GRHIAdapterUserDriverVersion;
	DriverInfo.DriverDate = GRHIAdapterDriverDate;
	DriverInfo.RHIName = GDynamicRHI ? GDynamicRHI->GetName() : FString();

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// for testing
	if(CVarValue == 2)
	{
		DriverInfo.SetNVIDIA();
		DriverInfo.DeviceDescription = TEXT("Test NVIDIA (bad)");
		DriverInfo.UserDriverVersion = TEXT("346.43");
		DriverInfo.InternalDriverVersion = TEXT("9.18.134.643");
		DriverInfo.DriverDate = TEXT("01-01-1900");
	}
	else if(CVarValue == 3)
	{
		DriverInfo.SetAMD();
		DriverInfo.DeviceDescription = TEXT("Test AMD (bad)");
		DriverInfo.UserDriverVersion = TEXT("Test Catalyst Version");
		DriverInfo.InternalDriverVersion = TEXT("13.152.1.1000");
		DriverInfo.DriverDate = TEXT("09-10-13");
	}
	else if(CVarValue == 4)
	{
		DriverInfo.SetAMD();
		DriverInfo.DeviceDescription = TEXT("Test AMD (good)");
		DriverInfo.UserDriverVersion = TEXT("Test Catalyst Version");
		DriverInfo.InternalDriverVersion = TEXT("15.30.1025.1001");
		DriverInfo.DriverDate = TEXT("01-01-16");
	}
	else if(CVarValue == 5)
	{
		DriverInfo.SetIntel();
		DriverInfo.DeviceDescription = TEXT("Test Intel (good)");
		DriverInfo.UserDriverVersion = TEXT("Test Intel Version");
		DriverInfo.InternalDriverVersion = TEXT("8.15.10.2302");
		DriverInfo.DriverDate = TEXT("01-01-15");
	}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

	FGPUHardware DetectedGPUHardware(DriverInfo);

	// Pre-GCN GPUs usually don't support updating to latest driver
	// But it is unclear what is the lastest version supported as it varies from card to card
	// So just don't complain if pre-gcn
	if (DriverInfo.IsValid() && !GRHIDeviceIsAMDPreGCNArchitecture)
	{
		FBlackListEntry BlackListEntry = DetectedGPUHardware.FindDriverBlacklistEntry();

		if (BlackListEntry.IsValid())
		{
			bool bLatestBlacklisted = DetectedGPUHardware.IsLatestBlacklisted();

			// Note: we don't localize the vendor's name.
			FString VendorString = DriverInfo.ProviderName;
			if (DriverInfo.IsNVIDIA())
			{
				VendorString = TEXT("NVIDIA");
			}
			else if (DriverInfo.IsAMD())
			{
				VendorString = TEXT("AMD");
			}
			else if (DriverInfo.IsIntel())
			{
				VendorString = TEXT("Intel");
			}

			// format message box UI
			FFormatNamedArguments Args;
			Args.Add(TEXT("AdapterName"), FText::FromString(DriverInfo.DeviceDescription));
			Args.Add(TEXT("Vendor"), FText::FromString(VendorString));
			Args.Add(TEXT("RecommendedVer"), FText::FromString(DetectedGPUHardware.GetSuggestedDriverVersion(DriverInfo.RHIName)));
			Args.Add(TEXT("InstalledVer"), FText::FromString(DriverInfo.UserDriverVersion));

			// this message can be suppressed with r.WarnOfBadDrivers=0
			FText LocalizedMsg;
			if (bLatestBlacklisted)
			{
				LocalizedMsg = FText::Format(NSLOCTEXT("MessageDialog", "LatestVideoCardDriverIssueReport","The latest version of the {Vendor} graphics driver has known issues.\nPlease install the recommended driver version.\n\n{AdapterName}\nInstalled: {InstalledVer}\nRecommended: {RecommendedVer}"),Args);
			}
			else
			{
				LocalizedMsg = FText::Format(NSLOCTEXT("MessageDialog", "VideoCardDriverIssueReport","The installed version of the {Vendor} graphics driver has known issues.\nPlease update to the latest driver version.\n\n{AdapterName}\nInstalled: {InstalledVer}\nRecommended: {RecommendedVer}"),Args);
			}

			FPlatformMisc::MessageBoxExt(EAppMsgType::Ok,
				*LocalizedMsg.ToString(),
				*NSLOCTEXT("MessageDialog", "TitleVideoCardDriverIssue", "WARNING: Known issues with graphics driver").ToString());
		}
	}
}
#elif PLATFORM_MAC
static void RHIDetectAndWarnOfBadDrivers(bool bHasEditorToken)
{
	int32 CVarValue = CVarWarnOfBadDrivers.GetValueOnGameThread();

	if (!GIsRHIInitialized || !CVarValue || GRHIVendorId == 0 || bHasEditorToken || FApp::IsUnattended())
	{
		return;
	}

	if (FPlatformMisc::MacOSXVersionCompare(10,15,5) < 0)
	{
		// this message can be suppressed with r.WarnOfBadDrivers=0
		FPlatformMisc::MessageBoxExt(EAppMsgType::Ok,
									 *NSLOCTEXT("MessageDialog", "UpdateMacOSX_Body", "Please update to the latest version of macOS for best performance and stability.").ToString(),
									 *NSLOCTEXT("MessageDialog", "UpdateMacOSX_Title", "Update macOS").ToString());
	}
}
#endif // PLATFORM_WINDOWS

void RHIInit(bool bHasEditorToken)
{
	if (!GDynamicRHI)
	{
		// read in any data driven shader platform info structures we can find
		FGenericDataDrivenShaderPlatformInfo::Initialize();

		GRHICommandList.LatchBypass(); // read commandline for bypass flag

		if (!FApp::CanEverRender())
		{
			InitNullRHI();
		}
		else
		{
			LLM_SCOPE(ELLMTag::RHIMisc);

			GDynamicRHI = PlatformCreateDynamicRHI();
			if (GDynamicRHI)
			{
				GDynamicRHI->Init();

#if WITH_MGPU
				AFRUtils::StaticInitialize();
#endif

				// Validation of contexts.
				GRHICommandList.GetImmediateCommandList().GetContext();
				GRHICommandList.GetImmediateAsyncComputeCommandList().GetComputeContext();
				check(GIsRHIInitialized);

				// Set default GPU mask to all GPUs. This is necessary to ensure that any commands
				// that create and initialize resources are executed on all GPUs. Scene rendering
				// will restrict itself to a subset of GPUs as needed.
				GRHICommandList.GetImmediateCommandList().SetGPUMask(FRHIGPUMask::All());
				GRHICommandList.GetImmediateAsyncComputeCommandList().SetGPUMask(FRHIGPUMask::All());

				FString FeatureLevelString;
				GetFeatureLevelName(GMaxRHIFeatureLevel, FeatureLevelString);

				if (bHasEditorToken && GMaxRHIFeatureLevel < ERHIFeatureLevel::SM5)
				{
					FString ShaderPlatformString = LegacyShaderPlatformToShaderFormat(GetFeatureLevelShaderPlatform(GMaxRHIFeatureLevel)).ToString();
					FString Error = FString::Printf(TEXT("A Feature Level 5 video card is required to run the editor.\nAvailableFeatureLevel = %s, ShaderPlatform = %s"), *FeatureLevelString, *ShaderPlatformString);
					FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Error));
					FPlatformMisc::RequestExit(1);
				}

				// Update the crash context analytics
				FGenericCrashContext::SetEngineData(TEXT("RHI.RHIName"), GDynamicRHI ? (GMaxRHIFeatureLevel == ERHIFeatureLevel::ES3_1 ? FString(GDynamicRHI->GetName()) + TEXT("_ES31") : GDynamicRHI->GetName()) : TEXT("Unknown"));
				FGenericCrashContext::SetEngineData(TEXT("RHI.AdapterName"), GRHIAdapterName);
				FGenericCrashContext::SetEngineData(TEXT("RHI.UserDriverVersion"), GRHIAdapterUserDriverVersion);
				FGenericCrashContext::SetEngineData(TEXT("RHI.InternalDriverVersion"), GRHIAdapterInternalDriverVersion);
				FGenericCrashContext::SetEngineData(TEXT("RHI.DriverDate"), GRHIAdapterDriverDate);
				FGenericCrashContext::SetEngineData(TEXT("RHI.FeatureLevel"), FeatureLevelString);
			}
#if PLATFORM_ALLOW_NULL_RHI
			else
			{
				// If the platform supports doing so, fall back to the NULL RHI on failure
				InitNullRHI();
			}
#endif
		}

		check(GDynamicRHI);
	}

#if PLATFORM_WINDOWS || PLATFORM_MAC
#if NV_GEFORCENOW
	bool bDetectAndWarnBadDrivers = true;
	if (IsRHIDeviceNVIDIA() && !!CVarDisableDriverWarningPopupIfGFN.GetValueOnAnyThread())
	{
		const GfnRuntimeError GfnResult = GeForceNOWWrapper::Get().Initialize();
		const bool bGfnRuntimeSDKInitialized = GfnResult == gfnSuccess || GfnResult == gfnInitSuccessClientOnly;
		if (bGfnRuntimeSDKInitialized)
		{
			UE_LOG(LogRHI, Log, TEXT("GeForceNow SDK initialized: %d"), (int32)GfnResult);
		}
		else
		{
			UE_LOG(LogRHI, Log, TEXT("GeForceNow SDK initialization failed: %d"), (int32)GfnResult);
		}

		// Don't pop up a driver version warning window when running on a cloud machine
		bDetectAndWarnBadDrivers = !bGfnRuntimeSDKInitialized || !GeForceNOWWrapper::Get().IsRunningInCloud();
	}

	if (bDetectAndWarnBadDrivers)
	{
		RHIDetectAndWarnOfBadDrivers(bHasEditorToken);
	}
#else
	RHIDetectAndWarnOfBadDrivers(bHasEditorToken);
#endif
#endif
}

void RHIPostInit(const TArray<uint32>& InPixelFormatByteWidth)
{
	check(GDynamicRHI);
	GDynamicRHI->InitPixelFormatInfo(InPixelFormatByteWidth);
	GDynamicRHI->PostInit();
}

void RHIExit()
{
	if (!GUsingNullRHI && GDynamicRHI != NULL)
	{
		// Clean up all cached pipelines
		PipelineStateCache::Shutdown();

		// Destruct the dynamic RHI.
		GDynamicRHI->Shutdown();
		delete GDynamicRHI;
		GDynamicRHI = NULL;
	}
	else if (GUsingNullRHI)
	{
		// If we are using NullRHI flush the command list here in case somethings has been added to the command list during exit calls
		FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResourcesFlushDeferredDeletes);
	}
}


static void BaseRHISetGPUCaptureOptions(const TArray<FString>& Args, UWorld* World)
{
	if (Args.Num() > 0)
	{
		const bool bEnabled = Args[0].ToBool();
		GDynamicRHI->EnableIdealGPUCaptureOptions(bEnabled);
	}
	else
	{
		UE_LOG(LogRHI, Display, TEXT("Usage: r.RHISetGPUCaptureOptions 0 or r.RHISetGPUCaptureOptions 1"));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GBaseRHISetGPUCaptureOptions(
	TEXT("r.RHISetGPUCaptureOptions"),
	TEXT("Utility function to change multiple CVARs useful when profiling or debugging GPU rendering. Setting to 1 or 0 will guarantee all options are in the appropriate state.\n")
	TEXT("r.rhithread.enable, r.rhicmdbypass, r.showmaterialdrawevents, toggledrawevents\n")
	TEXT("Platform RHI's may implement more feature toggles."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BaseRHISetGPUCaptureOptions)
	);

void FDynamicRHI::RHIReadSurfaceFloatData(FRHITexture* Texture, FIntRect Rect, TArray<FFloat16Color>& OutData, FReadSurfaceDataFlags InFlags)
{
#if WITH_MGPU
	if (InFlags.GetGPUIndex() != 0)
	{
		unimplemented();
	}
	else
#endif
	{
		RHIReadSurfaceFloatData(Texture, Rect, OutData, InFlags.GetCubeFace(), InFlags.GetArrayIndex(), InFlags.GetMip());
	}
}

void FDynamicRHI::RHIRead3DSurfaceFloatData(FRHITexture* Texture, FIntRect Rect, FIntPoint ZMinMax, TArray<FFloat16Color>& OutData, FReadSurfaceDataFlags InFlags)
{
#if WITH_MGPU
	if (InFlags.GetGPUIndex() != 0)
	{
		unimplemented();
	}
	else
#endif
	{
		RHIRead3DSurfaceFloatData(Texture, Rect, ZMinMax, OutData);
	}
}

void FDynamicRHI::EnableIdealGPUCaptureOptions(bool bEnabled)
{
	static IConsoleVariable* RHICmdBypassVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.rhicmdbypass"));
	static IConsoleVariable* ShowMaterialDrawEventVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ShowMaterialDrawEvents"));	
	static IConsoleObject* RHIThreadEnableObj = IConsoleManager::Get().FindConsoleObject(TEXT("r.RHIThread.Enable"));
	static IConsoleCommand* RHIThreadEnableCommand = RHIThreadEnableObj ? RHIThreadEnableObj->AsCommand() : nullptr;

	const bool bShouldEnableDrawEvents = bEnabled;
	const bool bShouldEnableMaterialDrawEvents = bEnabled;
	const bool bShouldEnableRHIThread = !bEnabled;
	const bool bShouldRHICmdBypass = bEnabled;	

	const bool bDrawEvents = GetEmitDrawEvents() != 0;
	const bool bMaterialDrawEvents = ShowMaterialDrawEventVar ? ShowMaterialDrawEventVar->GetInt() != 0 : false;
	const bool bRHIThread = IsRunningRHIInSeparateThread();
	const bool bRHIBypass = RHICmdBypassVar ? RHICmdBypassVar->GetInt() != 0 : false;

	UE_LOG(LogRHI, Display, TEXT("Setting GPU Capture Options: %i"), bEnabled ? 1 : 0);
	if (bShouldEnableDrawEvents != bDrawEvents)
	{
		UE_LOG(LogRHI, Display, TEXT("Toggling draw events: %i"), bShouldEnableDrawEvents ? 1 : 0);
		SetEmitDrawEvents(bShouldEnableDrawEvents);
	}
	if (bShouldEnableMaterialDrawEvents != bMaterialDrawEvents && ShowMaterialDrawEventVar)
	{
		UE_LOG(LogRHI, Display, TEXT("Toggling showmaterialdrawevents: %i"), bShouldEnableDrawEvents ? 1 : 0);
		ShowMaterialDrawEventVar->Set(bShouldEnableDrawEvents ? -1 : 0);		
	}
	if (bRHIThread != bShouldEnableRHIThread && RHIThreadEnableCommand)
	{
		UE_LOG(LogRHI, Display, TEXT("Toggling rhi thread: %i"), bShouldEnableRHIThread ? 1 : 0);
		TArray<FString> Args;
		Args.Add(FString::Printf(TEXT("%i"), bShouldEnableRHIThread ? 1 : 0));
		RHIThreadEnableCommand->Execute(Args, nullptr, *GLog);
	}
	if (bRHIBypass != bShouldRHICmdBypass && RHICmdBypassVar)
	{
		UE_LOG(LogRHI, Display, TEXT("Toggling rhi bypass: %i"), bEnabled ? 1 : 0);
		RHICmdBypassVar->Set(bShouldRHICmdBypass ? 1 : 0, ECVF_SetByConsole);		
	}	
}

void FDynamicRHI::RHITransferIndexBufferUnderlyingResource(FRHIIndexBuffer* DestIndexBuffer, FRHIIndexBuffer* SrcIndexBuffer)
{
	UE_LOG(LogRHI, Fatal, TEXT("RHITransferIndexBufferUnderlyingResource isn't implemented for the current RHI"));
}

void FDynamicRHI::RHITransferVertexBufferUnderlyingResource(FRHIVertexBuffer* DestVertexBuffer, FRHIVertexBuffer* SrcVertexBuffer)
{
	UE_LOG(LogRHI, Fatal, TEXT("RHITransferVertexBufferUnderlyingResource isn't implemented for the current RHI"));
}

FUnorderedAccessViewRHIRef FDynamicRHI::RHICreateUnorderedAccessView(FRHITexture* Texture, uint32 MipLevel, uint8 Format)
{
	UE_LOG(LogRHI, Fatal, TEXT("RHICreateUnorderedAccessView with Format parameter isn't implemented for the current RHI"));
	return RHICreateUnorderedAccessView(Texture, MipLevel);
}

void FDynamicRHI::RHIUpdateShaderResourceView(FRHIShaderResourceView* SRV, FRHIVertexBuffer* VertexBuffer, uint32 Stride, uint8 Format)
{
	UE_LOG(LogRHI, Fatal, TEXT("RHIUpdateShaderResourceView isn't implemented for the current RHI"));
}

void FDynamicRHI::RHIUpdateShaderResourceView(FRHIShaderResourceView* SRV, FRHIIndexBuffer* IndexBuffer)
{
	UE_LOG(LogRHI, Fatal, TEXT("RHIUpdateShaderResourceView isn't implemented for the current RHI"));
}

uint64 FDynamicRHI::RHIGetMinimumAlignmentForBufferBackedSRV(EPixelFormat Format)
{
	return 1;
}

uint64 FDynamicRHI::RHICalcVMTexture2DPlatformSize(uint32 Mip0Width, uint32 Mip0Height, uint8 Format, uint32 NumMips, uint32 FirstMipIdx, uint32 NumSamples, ETextureCreateFlags Flags, uint32& OutAlign)
{
	UE_LOG(LogRHI, Fatal, TEXT("RHICalcVMTexture2DPlatformSize isn't implemented for the current RHI"));
	return -1;
}

FDefaultRHIRenderQueryPool::FDefaultRHIRenderQueryPool(ERenderQueryType InQueryType, FDynamicRHI* InDynamicRHI, uint32 InNumQueries)
	: DynamicRHI(InDynamicRHI)
	, QueryType(InQueryType)
	, NumQueries(InNumQueries)
{
	if (NumQueries != UINT32_MAX && (GSupportsTimestampRenderQueries || InQueryType != RQT_AbsoluteTime))
	{
		Queries.Reserve(NumQueries);
		for (uint32 i = 0; i < NumQueries; i++)
		{
			Queries.Push(DynamicRHI->RHICreateRenderQuery(QueryType));
			check(Queries.Last().IsValid());
			++AllocatedQueries;
		}
	}
}

FDefaultRHIRenderQueryPool::~FDefaultRHIRenderQueryPool()
{
	check(IsInRenderingThread());
	checkf(AllocatedQueries == Queries.Num(), TEXT("Querypool deleted before all Queries have been released"));
}

FRHIPooledRenderQuery FDefaultRHIRenderQueryPool::AllocateQuery()
{
	check(IsInRenderingThread());
	if (Queries.Num() > 0)
	{
		return FRHIPooledRenderQuery(this, Queries.Pop());
	}
	else
	{
		FRHIPooledRenderQuery Query = FRHIPooledRenderQuery(this, DynamicRHI->RHICreateRenderQuery(QueryType));
		if (Query.IsValid())
		{
			++AllocatedQueries;
		}
		ensure(AllocatedQueries <= NumQueries);
		return Query;
	}
}

void FDefaultRHIRenderQueryPool::ReleaseQuery(TRefCountPtr<FRHIRenderQuery>&& Query)
{
	if (QueryType == ERenderQueryType::RQT_Occlusion)
	{
		static int dbg = 0;
		dbg++;
	}
	check(IsInRenderingThread());
	//Hard to validate because of Resource resurrection, better to remove GetQueryRef entirely
	//checkf(Query.IsValid() && Query.GetRefCount() <= 2, TEXT("Query has been released but reference still held: use FRHIPooledRenderQuery::GetQueryRef() with extreme caution"));
	
	checkf(Query.IsValid(), TEXT("Only release valid queries"));
	checkf((uint32)Queries.Num() < NumQueries, TEXT("Pool contains more queries than it started with, double release somewhere?"));

	Queries.Push(MoveTemp(Query));
	check(!Query.IsValid());
}

FRenderQueryPoolRHIRef RHICreateRenderQueryPool(ERenderQueryType QueryType, uint32 NumQueries)
{
	return GDynamicRHI->RHICreateRenderQueryPool(QueryType, NumQueries);
}

EColorSpaceAndEOTF FDynamicRHI::RHIGetColorSpace(FRHIViewport* Viewport)
{
	return EColorSpaceAndEOTF::ERec709_sRGB;
}

void FDynamicRHI::RHICheckViewportHDRStatus(FRHIViewport* Viewport)
{
}


FShaderResourceViewInitializer::FShaderResourceViewInitializer(FRHIVertexBuffer* InVertexBuffer, EPixelFormat InFormat, uint32 InStartOffsetBytes, uint32 InNumElements)
	: VertexBufferInitializer({ InVertexBuffer, InStartOffsetBytes, InNumElements, InFormat }), Type(EType::VertexBufferSRV)
{
	check(InStartOffsetBytes % RHIGetMinimumAlignmentForBufferBackedSRV(InFormat) == 0);
	/*if (!VertexBufferInitializer.IsWholeResource())
	{
		const uint32 Stride = GPixelFormats[InFormat].BlockBytes;
		check((VertexBufferInitializer.NumElements * Stride + VertexBufferInitializer.StartOffsetBytes)  <= VertexBufferInitializer.VertexBuffer->GetSize());
	}*/
}

FShaderResourceViewInitializer::FShaderResourceViewInitializer(FRHIVertexBuffer* InVertexBuffer, EPixelFormat InFormat)
	: VertexBufferInitializer({ InVertexBuffer, 0, UINT32_MAX, InFormat }), Type(EType::VertexBufferSRV) 
{
}

FShaderResourceViewInitializer::FShaderResourceViewInitializer(FRHIStructuredBuffer* InStructuredBuffer, uint32 InStartOffsetBytes, uint32 InNumElements)
	: StructuredBufferInitializer(FStructuredBufferShaderResourceViewInitializer{ InStructuredBuffer, InStartOffsetBytes, InNumElements }), Type(EType::StructuredBufferSRV)
{
	check(InStartOffsetBytes % InStructuredBuffer->GetStride() == 0);
	if (!StructuredBufferInitializer.IsWholeResource())
	{
		const uint32 Stride = StructuredBufferInitializer.StructuredBuffer->GetStride();
		check((StructuredBufferInitializer.NumElements * Stride + StructuredBufferInitializer.StartOffsetBytes)  <= StructuredBufferInitializer.StructuredBuffer->GetSize());
	}
}

FShaderResourceViewInitializer::FShaderResourceViewInitializer(FRHIStructuredBuffer* InStructuredBuffer)
	: StructuredBufferInitializer(FStructuredBufferShaderResourceViewInitializer{ InStructuredBuffer, 0, UINT32_MAX }), Type(EType::StructuredBufferSRV) 
{
}

FShaderResourceViewInitializer::FShaderResourceViewInitializer(FRHIIndexBuffer* InIndexBuffer, uint32 InStartOffsetBytes, uint32 InNumElements)
	: IndexBufferInitializer(FIndexBufferShaderResourceViewInitializer{ InIndexBuffer, InStartOffsetBytes, InNumElements }), Type(EType::IndexBufferSRV)
{
	check(InStartOffsetBytes % RHIGetMinimumAlignmentForBufferBackedSRV(InIndexBuffer->GetStride() == 2 ? PF_R16_UINT : PF_R32_UINT) == 0);
	if (!IndexBufferInitializer.IsWholeResource())
	{
		const uint32 Stride = IndexBufferInitializer.IndexBuffer->GetStride();
		check((IndexBufferInitializer.NumElements * Stride + IndexBufferInitializer.StartOffsetBytes) <= IndexBufferInitializer.IndexBuffer->GetSize());
	}
}

FShaderResourceViewInitializer::FShaderResourceViewInitializer(FRHIIndexBuffer* InIndexBuffer)
	: IndexBufferInitializer(FIndexBufferShaderResourceViewInitializer{ InIndexBuffer, 0, UINT32_MAX }), Type(EType::IndexBufferSRV) 
{
}

