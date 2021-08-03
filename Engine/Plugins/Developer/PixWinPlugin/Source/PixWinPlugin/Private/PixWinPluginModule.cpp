// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixWinPluginModule.h"

#include "RenderingThread.h"
#include "RHI.h"

#define PIX_PLUGIN_ENABLED (PLATFORM_WINDOWS && !UE_BUILD_SHIPPING)

#if PIX_PLUGIN_ENABLED
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Microsoft/COMPointer.h"
#include <dxgi1_3.h>
#endif

DEFINE_LOG_CATEGORY_STATIC(PixWinPlugin, Log, All);

#define LOCTEXT_NAMESPACE "PixWinPlugin"

namespace Impl
{
#if PIX_PLUGIN_ENABLED
	// Dynamic load DXGIGetDebugInterface1 because it's not available on Windows 7.
	typedef HRESULT(WINAPI* FDXGIGetDebugInterface1)(UINT, REFIID, void**);

	// UUID hook into PIX for windows, temporary until we get the header dependency for <DXProgrammableCapture.h> which is included in the windows 10 SDK
	interface DECLSPEC_UUID("9f251514-9d4d-4902-9d60-18988ab7d4b5") DECLSPEC_NOVTABLE
	IDXGraphicsAnalysis : public IUnknown
	{
		STDMETHOD_(void, BeginCapture)() PURE;
		STDMETHOD_(void, EndCapture)() PURE;
	};
#endif

	/** Container for graphics analysis com interface. */
	class FPixGraphicsAnalysisInterface
	{
	public:
		FPixGraphicsAnalysisInterface()
		{
#if PIX_PLUGIN_ENABLED
			// DXGIGetDebugInterface1 is Windows 8 and above.
			if (FPlatformMisc::VerifyWindowsVersion(6, 2))
			{
				FDXGIGetDebugInterface1 DXGIGetDebugInterface1FnPtr = nullptr;

				HMODULE DxgiDLL = LoadLibraryA("dxgi.dll");
				if (DxgiDLL)
				{
#pragma warning(push)
#pragma warning(disable: 4191) // disable the "unsafe conversion from 'FARPROC' to 'blah'" warning
					DXGIGetDebugInterface1FnPtr = (FDXGIGetDebugInterface1)(GetProcAddress(DxgiDLL, "DXGIGetDebugInterface1"));
#pragma warning(pop)
					FreeLibrary(DxgiDLL);
				}

				if (DXGIGetDebugInterface1FnPtr)
				{
					DXGIGetDebugInterface1FnPtr(0, IID_PPV_ARGS(&GAPtr));
				}
			}
#endif // PIX_PLUGIN_ENABLED
		}

		bool IsValid()
		{
#if PIX_PLUGIN_ENABLED
			return GAPtr.Get() != nullptr;
#else
			return false;
#endif
		}

		void BeginCapture()
		{
#if PIX_PLUGIN_ENABLED
			check(IsValid());
			GAPtr->BeginCapture();
#endif
		}

		void EndCapture()
		{
#if PIX_PLUGIN_ENABLED
			check(IsValid());
			GAPtr->EndCapture();
#endif
		}

	private:
#if PIX_PLUGIN_ENABLED
		TComPtr<IDXGraphicsAnalysis> GAPtr;
#endif
	};


	/** Dummy input device that is used only to generate a Tick. */
	class FPixDummyInputDevice : public IInputDevice
	{
	public:
		FPixDummyInputDevice(FPixWinPluginModule* InModule)
			: Module(InModule)
		{ }

		virtual void Tick(float DeltaTime) override
		{
			if (ensure(Module != nullptr))
			{
				Module->Tick(DeltaTime);
			}
		}

		virtual void SendControllerEvents() override { }
		virtual void SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override { }
		virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override { return(false); }
		virtual void SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value) override { }
		virtual void SetChannelValues(int32 ControllerId, const FForceFeedbackValues& values) override { }

	private:
		FPixWinPluginModule* Module;
	};
}

void FPixWinPluginModule::StartupModule()
{
	PixGraphicsAnalysisInterface = new Impl::FPixGraphicsAnalysisInterface;
	if (PixGraphicsAnalysisInterface->IsValid())
	{
		// Register modular features.
		IModularFeatures::Get().RegisterModularFeature(IRenderCaptureProvider::GetModularFeatureName(), (IRenderCaptureProvider*)this);
		IModularFeatures::Get().RegisterModularFeature(IInputDeviceModule::GetModularFeatureName(), (IInputDeviceModule*)this);

		// Register console command.
		ConsoleCommandCaptureFrame = new FAutoConsoleCommand(
			TEXT("pix.GpuCaptureFrame"),
			TEXT("Captures the rendering commands of the next frame."),
			FConsoleCommandDelegate::CreateRaw(this, &FPixWinPluginModule::CaptureFrame));

		UE_LOG(PixWinPlugin, Log, TEXT("PIX capture plugin is ready!"));
	}
	else
	{
		UE_LOG(PixWinPlugin, Log, TEXT("PIX capture plugin failed to initialize! Check that the process is launched from PIX."));
	}
}

void FPixWinPluginModule::ShutdownModule()
{
	delete PixGraphicsAnalysisInterface;
	PixGraphicsAnalysisInterface = nullptr;
	delete ConsoleCommandCaptureFrame;
	ConsoleCommandCaptureFrame = nullptr;

	IModularFeatures::Get().UnregisterModularFeature(IRenderCaptureProvider::GetModularFeatureName(), (IRenderCaptureProvider*)this);
	IModularFeatures::Get().UnregisterModularFeature(IInputDeviceModule::GetModularFeatureName(), (IInputDeviceModule*)this);
}

TSharedPtr<class IInputDevice> FPixWinPluginModule::CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
{
	UE_LOG(PixWinPlugin, Log, TEXT("Creating dummy input device (for intercepting engine ticks)"));
	Impl::FPixDummyInputDevice* InputDev = new Impl::FPixDummyInputDevice(this);
	return MakeShareable((IInputDevice*)InputDev);
}

void FPixWinPluginModule::CaptureFrame(FViewport* InViewport, uint32 InFlags, FString const& InDestFileName)
{
	// Don't trigger a new capture if we are currently capturing.
	bBeginCaptureNextTick = !bEndCaptureNextTick;
}

void FPixWinPluginModule::BeginCapture(FRHICommandListImmediate* InRHICommandList, uint32 InFlags, FString const& InDestFileName)
{
	InRHICommandList->SubmitCommandsAndFlushGPU();
	InRHICommandList->EnqueueLambda([Pix = PixGraphicsAnalysisInterface](FRHICommandListImmediate& RHICommandList)
	{
		Pix->BeginCapture();
	});
}

void FPixWinPluginModule::EndCapture(FRHICommandListImmediate* InRHICommandList)
{
	InRHICommandList->SubmitCommandsAndFlushGPU();
	InRHICommandList->EnqueueLambda([Pix = PixGraphicsAnalysisInterface](FRHICommandListImmediate& RHICommandList)
	{
		Pix->EndCapture();
	});
}

void FPixWinPluginModule::Tick(float DeltaTime)
{
	if (bBeginCaptureNextTick)
	{
		// Start a capture.
		bBeginCaptureNextTick = false;
		bEndCaptureNextTick = true;

		ENQUEUE_RENDER_COMMAND(BeginCaptureCommand)([this](FRHICommandListImmediate& RHICommandList)
		{
			BeginCapture(&RHICommandList, 0, FString());
		});
	}
	else if (bEndCaptureNextTick)
	{
		// End a capture.
		bEndCaptureNextTick = false;

		ENQUEUE_RENDER_COMMAND(EndCaptureCommand)([this](FRHICommandListImmediate& RHICommandList)
		{
			EndCapture(&RHICommandList);
		});
	}
}

#undef LOCTEXT_NAMESPACE

#if PIX_PLUGIN_ENABLED
#include "Windows/HideWindowsPlatformTypes.h"
#endif

IMPLEMENT_MODULE(FPixWinPluginModule, PixWinPlugin)
