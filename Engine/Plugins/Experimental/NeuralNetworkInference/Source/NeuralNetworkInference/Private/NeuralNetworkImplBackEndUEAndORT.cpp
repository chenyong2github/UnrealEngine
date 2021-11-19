// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralNetworkImplBackEndUEAndORT.h"

#include "NeuralNetworkInferenceUtils.h"
#include "NeuralNetworkInferenceUtilsGPU.h"
#include "RedirectCoutAndCerrToUeLog.h"
#if WITH_EDITOR
#include "Misc/MessageDialog.h"
#endif //WITH_EDITOR

#if defined(WITH_UE_AND_ORT_SUPPORT) && defined(PLATFORM_WIN64)
	#include "HAL/CriticalSection.h"
	#include "RHI.h"
	#include "DynamicRHI.h"

	// Disable NOMINMAX & WIN32_LEAN_AND_MEAN defines to avoid compiler warnings
	#pragma push_macro("NOMINMAX")
	#pragma push_macro("WIN32_LEAN_AND_MEAN")
	#pragma push_macro("UE_MINIMAL_WINDOWS_INCLUDE")
	#undef NOMINMAX
	#undef WIN32_LEAN_AND_MEAN
	#define UE_MINIMAL_WINDOWS_INCLUDE // Avoids Win64 Clang warning
	#include "D3D12RHIPrivate.h"
	#pragma pop_macro("UE_MINIMAL_WINDOWS_INCLUDE")
	#pragma pop_macro("WIN32_LEAN_AND_MEAN")
	#pragma pop_macro("NOMINMAX")
#endif

//#define WITH_NNI_CPU_NOT_RECOMMENDED // Only for debugging purposes

NNI_THIRD_PARTY_INCLUDES_START
#undef check
#undef TEXT
#ifdef WITH_UE_AND_ORT_SUPPORT
	#ifdef PLATFORM_WIN64
	#include "core/providers/dml/dml_provider_factory.h"
	#endif
	#ifdef WITH_NNI_CPU_NOT_RECOMMENDED
	#include "core/providers/nni_cpu/nni_cpu_provider_factory.h"
	#endif //WITH_NNI_CPU_NOT_RECOMMENDED
#endif //WITH_UE_AND_ORT_SUPPORT
NNI_THIRD_PARTY_INCLUDES_END

#include "ShaderParameterUtils.h"
#include "ShaderParameterStruct.h"

#ifdef WITH_UE_AND_ORT_SUPPORT

#if defined(PLATFORM_WIN64)

#if WITH_EDITOR && !UE_BUILD_SHIPPING
	#include "pix3.h"
	#define NNIGPUProfileMarker(Name) FNNIGPUProfiler::Instance()->Marker(Name)
#else
	#define NNIGPUProfileMarker(Name)
#endif

#endif // PLATFORM_WIN64

// Helper class to utilize the PIX CPU/GPU debugger on Windows
class FNNIGPUProfiler
{
public:
	static FNNIGPUProfiler* Instance()
	{
		static FNNIGPUProfiler Inst;
		return &Inst;
	}

	class FScopedEvent
	{
	public:

		FScopedEvent(const FString& Name, FColor Color = FColor::Yellow)
		{
			FNNIGPUProfiler::Instance()->EventBegin(Name, Color);
		}

		~FScopedEvent()
		{
			FNNIGPUProfiler::Instance()->EventEnd();
		}
	};

private:
	FNNIGPUProfiler()
	{
#if defined(PLATFORM_WIN64) && defined(USE_PIX) && !defined(UE_BUILD_SHIPPING)
		bIsEnabled = FD3D12DynamicRHI::GetD3DRHI()->IsPixEventEnabled();
#else
		bIsEnabled = false;
#endif
	}

public:
	~FNNIGPUProfiler()
	{
	}

	void Marker(const FString& Name, FColor Color = FColor::Yellow)
	{
#if defined(PLATFORM_WIN64) && defined(USE_PIX) && !defined(UE_BUILD_SHIPPING)
		if (bIsEnabled)
		{
			PIXSetMarker(PIX_COLOR(Color.R, Color.G, Color.B), Name.GetCharArray().GetData());
		}
#endif
	}

	void EventBegin(const FString& Name, FColor Color = FColor::Yellow)
	{
#if defined(PLATFORM_WIN64) && defined(USE_PIX) && !defined(UE_BUILD_SHIPPING)
		if (bIsEnabled)
		{
			PIXBeginEvent(PIX_COLOR(Color.R, Color.G, Color.B), Name.GetCharArray().GetData());
		}
#endif
	}

	void EventEnd()
	{
#if defined(PLATFORM_WIN64) && defined(USE_PIX) && !defined(UE_BUILD_SHIPPING)
		if (bIsEnabled)
		{
			PIXEndEvent();
		}
#endif
	}

private:
	bool bIsEnabled;
};


#ifdef PLATFORM_WIN64

/* FPrivateImplBackEndUEAndORT auxiliary class
 *****************************************************************************/
class FPrivateImplBackEndUEAndORT
{
public:
	static IDMLDevice* GetDMLDeviceThreadSafe(ID3D12Device* Device);

private:
	/**
	 * Helper class that maintains a list of created DML Devices for given ID3D12Device
	 */
	class FDMLDeviceList
	{
	public:
		IDMLDevice* GetDMLDevice(ID3D12Device* Device);

	private:
		IDMLDevice* Add(ID3D12Device* Device);

		struct DMLDeviceEntry
		{
			ID3D12Device* Device;
			IDMLDevice* DmlDevice;
		};

		TArray<DMLDeviceEntry> Entries;
	};
};

IDMLDevice* FPrivateImplBackEndUEAndORT::GetDMLDeviceThreadSafe(ID3D12Device* Device)
{
	static FCriticalSection CriticalSection; /* Protects GetDMLDeviceThreadSafe from being called simultaneously from multiple threads. */
	static FDMLDeviceList DMLDeviceList;
	FScopeLock Lock(&CriticalSection);
	return DMLDeviceList.GetDMLDevice(Device);
}

IDMLDevice* FPrivateImplBackEndUEAndORT::FDMLDeviceList::GetDMLDevice(ID3D12Device* Device)
{
	for (size_t c = 0; c < Entries.Num(); ++c)
	{
		if (Entries[c].Device == Device)
		{
			return Entries[c].DmlDevice;
		}
	}

	return Add(Device);
}

IDMLDevice* FPrivateImplBackEndUEAndORT::FDMLDeviceList::Add(ID3D12Device* Device)
{
	// Create new DML Device
	IDMLDevice* DmlDevice = nullptr;

	DML_CREATE_DEVICE_FLAGS DmlCreateFlags = DML_CREATE_DEVICE_FLAG_NONE;

#if !UE_BUILD_SHIPPING
	if (D3D12RHI_ShouldCreateWithD3DDebug()
		|| FParse::Param(FCommandLine::Get(), TEXT("d3d12gpuvalidation")) || FParse::Param(FCommandLine::Get(), TEXT("gpuvalidation")))
	{
		DmlCreateFlags |= DML_CREATE_DEVICE_FLAG_DEBUG;
	}
#endif

	HRESULT res = DMLCreateDevice1(Device, DmlCreateFlags, DML_FEATURE_LEVEL_2_0, IID_PPV_ARGS(&DmlDevice));

	// Handle the case if Graphics Debug Tools are not installed
	if (res == DXGI_ERROR_SDK_COMPONENT_MISSING)
	{
		DmlCreateFlags &= ~DML_CREATE_DEVICE_FLAG_DEBUG;

		res = DMLCreateDevice1(Device, DmlCreateFlags, DML_FEATURE_LEVEL_2_0, IID_PPV_ARGS(&DmlDevice));
	}

	if (!DmlDevice)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("FDMLDeviceList::Add(): Failed to create DML device, res=%0x."), res);
		return nullptr;
	}

	Entries.Push(DMLDeviceEntry{ Device, DmlDevice });

	return DmlDevice;
}

#endif // PLATFORM_WIN64
#endif // WITH_UE_AND_ORT_SUPPORT



/* UNeuralNetwork public functions
 *****************************************************************************/

void UNeuralNetwork::FImplBackEndUEAndORT::WarnAndSetDeviceToCPUIfDX12NotEnabled(ENeuralDeviceType& InOutDeviceType, const bool bInShouldOpenMessageLog)
{
	if (InOutDeviceType != ENeuralDeviceType::CPU)
	{
		if (!IsGPUConfigCompatible())
		{
			InOutDeviceType = ENeuralDeviceType::CPU;

			const FString RHIName = GDynamicRHI->GetName();
			const FString ErrorMessage = TEXT("On Windows, only DirectX 12 rendering (\"D3D12\") is compatible with the UEAndORT back end of NeuralNetworkInference (NNI). Instead, \"")
				+ RHIName + TEXT("\" was used. You have the following options:\n\n"
					"\t1. (Recommended) Switch Unreal Engine to DX12. In order to do that:\n"
					"\t\t - Go to \"Project Settings\", \"Platforms\", \"Windows\", \"Default RHI\".\n"
					"\t\t - Select \"DirectX 12\".\n"
					"\t\t - Restart Unreal Engine.\n"
					"\t2. Alternatively, switch the network to CPU with UNeuralNetwork::SetDeviceType().\n\n"
					"Network set to CPU provisionally.");
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("FImplBackEndUEAndORT::WarnAndSetDeviceToCPUIfDX12NotEnabled(): %s"), *ErrorMessage);
#if WITH_EDITOR
			if (bInShouldOpenMessageLog)
			{
				FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ErrorMessage));
			}
#endif //WITH_EDITOR
		}
	}
}

bool UNeuralNetwork::FImplBackEndUEAndORT::IsGPUConfigCompatible()
{
#ifdef WITH_UE_AND_ORT_SUPPORT
#ifdef PLATFORM_WIN64
	// Return whether it is DX12
	const FString RHIName = GDynamicRHI->GetName();
	return (RHIName == TEXT("D3D12"));
#endif //PLATFORM_WIN64
#endif //WITH_UE_AND_ORT_SUPPORT

	// If not Windows and/or if WITH_UE_AND_ORT_SUPPORT not defined, then this should return true because GPU will always work
	return true;
}

bool UNeuralNetwork::FImplBackEndUEAndORT::Load(TSharedPtr<FImplBackEndUEAndORT>& InOutImplBackEndUEAndORT,
	FOnAsyncRunCompleted& InOutOnAsyncRunCompletedDelegate, std::atomic<bool>& bInOutIsBackgroundThreadRunning, FCriticalSection& InOutResoucesCriticalSection,
	TArray<bool>& OutAreInputTensorSizesVariable, const TArray<uint8>& InModelReadFromFileInBytes, const FString& InModelFullFilePath,
	const ENeuralDeviceType InDeviceType, const ENeuralDeviceType InInputDeviceType, const ENeuralDeviceType InOutputDeviceType)
{
#ifdef WITH_UE_AND_ORT_SUPPORT
#if WITH_EDITOR
	try
#endif //WITH_EDITOR
	{
		// Avoid multi-threaded crashes
		const FRedirectCoutAndCerrToUeLog RedirectCoutAndCerrToUeLog;

		if (InOutImplBackEndUEAndORT.IsValid())
		{
			InOutImplBackEndUEAndORT->IsAsyncTaskDone();
		}

		// Initialize and configure InOutImplBackEndUEAndORT
		if (!UNeuralNetwork::FImplBackEndUEAndORT::InitializedAndConfigureMembers(InOutImplBackEndUEAndORT, InOutOnAsyncRunCompletedDelegate,
				bInOutIsBackgroundThreadRunning, InOutResoucesCriticalSection, InModelFullFilePath, InDeviceType))
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("FImplBackEndUEAndORT::Load(): InitializedAndConfigureMembers failed."));
			return false;
		}

		// Create session from model saved in InModelReadFromFileInBytes (if not empty)
		if (InModelReadFromFileInBytes.Num() > 0)
		{
			// Read model from ModelReadFromFileInBytesVector
			InOutImplBackEndUEAndORT->Session = MakeUnique<Ort::Session>(*InOutImplBackEndUEAndORT->Environment, InModelReadFromFileInBytes.GetData(),
				InModelReadFromFileInBytes.Num(), *InOutImplBackEndUEAndORT->SessionOptions);

#ifdef PLATFORM_WIN64
			InOutImplBackEndUEAndORT->DmlGPUMemoryInfo = MakeUnique<Ort::MemoryInfo>(/*onnxruntime::DML*/ "DML", OrtAllocatorType::OrtDeviceAllocator, /*deviceId*/ 0, OrtMemType::OrtMemTypeDefault);
#endif
		}
		// Else
		else
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("FImplBackEndUEAndORT::Load(): InModelReadFromFileInBytes was empty."));
			return false;
		}
		
		// Sanity check if device type is CPU and to make sure that input and/or output is also on the CPU
		ENeuralDeviceType InputDeviceType = InInputDeviceType;
		ENeuralDeviceType OutputDeviceType = InOutputDeviceType;
		if (InDeviceType == ENeuralDeviceType::CPU && (InInputDeviceType == ENeuralDeviceType::GPU || InOutputDeviceType == ENeuralDeviceType::GPU))
		{
			UE_LOG(LogNeuralNetworkInference, Warning,
				TEXT("FImplBackEndUEAndORT::Load(): DeviceType is CPU but Input and/or Output is set to GPU, setting all to CPU."));
			InputDeviceType = ENeuralDeviceType::CPU;
			OutputDeviceType = ENeuralDeviceType::CPU;
		}

		if (!InOutImplBackEndUEAndORT->ConfigureTensors(InOutImplBackEndUEAndORT->InputTensors, &OutAreInputTensorSizesVariable, InDeviceType,
			InputDeviceType, OutputDeviceType))
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("FImplBackEndUEAndORT::Load(): Failed to configure input tensors."));
			return false;
		}

		if (!InOutImplBackEndUEAndORT->ConfigureTensors(InOutImplBackEndUEAndORT->OutputTensors, nullptr, InDeviceType, InputDeviceType, OutputDeviceType))
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("FImplBackEndUEAndORT::Load(): Failed to configure output tensors."));
			return false;
		}

		// Initializing AsyncTask
		InOutImplBackEndUEAndORT->NeuralNetworkAsyncTask = MakeUnique<FAsyncTask<FNeuralNetworkAsyncTask>>(InOutImplBackEndUEAndORT.Get());
		
		return true;
	}
#if WITH_EDITOR
	catch (const std::exception& Exception)
	{
		UE_LOG(LogNeuralNetworkInference, Error, TEXT("%s"), UTF8_TO_TCHAR(Exception.what()));
		return false;
	}
#endif //WITH_EDITOR

#else //WITH_UE_AND_ORT_SUPPORT
	UE_LOG(LogNeuralNetworkInference, Warning, TEXT("FImplBackEndUEAndORT::Load(): Platform or Operating System not suported yet for UEAndORT"
		" BackEnd. Set BackEnd to ENeuralBackEnd::Auto (recommended) or ENeuralBackEnd::UEOnly for this platform."));
	return false;
#endif //WITH_UE_AND_ORT_SUPPORT
}

#ifdef WITH_UE_AND_ORT_SUPPORT
void UNeuralNetwork::FImplBackEndUEAndORT::IsAsyncTaskDone() const
{
	if (NeuralNetworkAsyncTask && !NeuralNetworkAsyncTask->IsDone())
	{
		UE_LOG(LogNeuralNetworkInference, Warning,
			TEXT("FImplBackEndUEAndORT::Run(): Previous async run had not been completed. Blocking thread until it is completed."));
		NeuralNetworkAsyncTask->EnsureCompletion(/*bDoWorkOnThisThreadIfNotStarted*/true);
	}
}

void UNeuralNetwork::FImplBackEndUEAndORT::ClearResources()
{
#ifdef PLATFORM_WIN64
	if (DmlApi)
	{
		const int32 Num = DmlGPUResources.Num();
		for (int32 Index = 0; Index < Num; ++Index)
		{
			DmlApi->FreeGPUAllocation(DmlGPUResources[Index]);
		}

		DmlGPUResources.Reset(0);
		DmlApi = nullptr;
	}
#endif //PLATFORM_WIN64
}

UNeuralNetwork::FImplBackEndUEAndORT::FImplBackEndUEAndORT(FOnAsyncRunCompleted& InOutOnAsyncRunCompletedDelegate,
	std::atomic<bool>& bInIsBackgroundThreadRunning, FCriticalSection& InResoucesCriticalSection)
	: OnAsyncRunCompletedDelegate(InOutOnAsyncRunCompletedDelegate)
	, bIsBackgroundThreadRunning(bInIsBackgroundThreadRunning)
	, ResoucesCriticalSection(InResoucesCriticalSection)
{
}

#endif //WITH_UE_AND_ORT_SUPPORT

void UNeuralNetwork::FImplBackEndUEAndORT::Run(const ENeuralNetworkSynchronousMode InSynchronousMode, const ENeuralDeviceType InDeviceType,
	const ENeuralDeviceType InInputDeviceType, const ENeuralDeviceType InOutputDeviceType)
{
#ifdef WITH_UE_AND_ORT_SUPPORT
#if WITH_EDITOR
	try
#endif //WITH_EDITOR
	{
		const FRedirectCoutAndCerrToUeLog RedirectCoutAndCerrToUeLog;

		IsAsyncTaskDone();
		NeuralNetworkAsyncTask->GetTask().SetRunSessionArgs(InSynchronousMode, InDeviceType, InInputDeviceType, InOutputDeviceType);

		// Run UNeuralNetwork
		if (InSynchronousMode == ENeuralNetworkSynchronousMode::Synchronous)
		{
			NeuralNetworkAsyncTask->StartSynchronousTask();
		}
		else if (InSynchronousMode == ENeuralNetworkSynchronousMode::Asynchronous)
		{
			bIsBackgroundThreadRunning = true;
			NeuralNetworkAsyncTask->StartBackgroundTask();
		}
		else
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("FImplBackEndUEAndORT::Run(): Unknown SynchronousMode = %d."), (int32)InSynchronousMode);
		}
	}
#if WITH_EDITOR
	catch (const std::exception& Exception)
	{
		UE_LOG(LogNeuralNetworkInference, Error, TEXT("%s"), UTF8_TO_TCHAR(Exception.what()));
	}
#endif //WITH_EDITOR

#else //WITH_UE_AND_ORT_SUPPORT
	UE_LOG(LogNeuralNetworkInference, Warning, TEXT("FImplBackEndUEAndORT::Run(): Platform or Operating System not suported yet for UEAndORT"
		" BackEnd. Set BackEnd to ENeuralBackEnd::Auto or ENeuralBackEnd::UEOnly for this platform."));
#endif //WITH_UE_AND_ORT_SUPPORT
}

/* UNeuralNetwork private functions
 *****************************************************************************/

#ifdef WITH_UE_AND_ORT_SUPPORT

void UNeuralNetwork::FImplBackEndUEAndORT::RunSessionAsync(const ENeuralDeviceType InDeviceType, const ENeuralDeviceType InInputDeviceType,
	const ENeuralDeviceType InOutputDeviceType)
{
	const FScopeLock ResourcesLock(&ResoucesCriticalSection);

	RunSessionImpl(InDeviceType, InInputDeviceType, InOutputDeviceType);

	OnAsyncRunCompletedDelegate.ExecuteIfBound();
	bIsBackgroundThreadRunning = false;
}

void UNeuralNetwork::FImplBackEndUEAndORT::RunSessionSync(const ENeuralDeviceType InDeviceType, const ENeuralDeviceType InInputDeviceType,
	const ENeuralDeviceType InOutputDeviceType)
{
	RunSessionImpl(InDeviceType, InInputDeviceType, InOutputDeviceType);
}

// Used when uploading tensors to GPU
// NOTE: Upload parameter is not yet used, we plan to use it in the future
BEGIN_SHADER_PARAMETER_STRUCT(FUploadTensorParameters, )
	RDG_BUFFER_ACCESS(Upload, ERHIAccess::CopySrc)
	RDG_BUFFER_ACCESS(Input, ERHIAccess::CopyDest)
END_SHADER_PARAMETER_STRUCT()

void UNeuralNetwork::FImplBackEndUEAndORT::RunSessionImpl(const ENeuralDeviceType InDeviceType, const ENeuralDeviceType InInputDeviceType,
	const ENeuralDeviceType InOutputDeviceType)
{
	if (InDeviceType == ENeuralDeviceType::GPU)
	{
		// Copy data to GPU (if required)
		bool bNeedsGPUCopy = false;

		for (auto& InputTensor : InputTensors)
		{
			if (InputTensor.GetTensorTypeGPU() != ENeuralTensorTypeGPU::Input)
				continue;

			bNeedsGPUCopy = true;

			ENQUEUE_RENDER_COMMAND(UploadTensorToGPU)([this, InputTensor] (FRHICommandListImmediate& RHICmdList)
			{
				FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("UploadTensorToGPU"));

				// Set parameters
				TRefCountPtr< FRDGPooledBuffer >&	PooledBuffer = InputTensor.GetPooledBuffer();
				FRDGBufferRef						InputBufferRef = GraphBuilder.RegisterExternalBuffer(PooledBuffer);

				FUploadTensorParameters*			UploadParameters = GraphBuilder.AllocParameters<FUploadTensorParameters>();

				UploadParameters->Input = InputBufferRef;

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("NNI:UploadTensor:%s", InputTensor.GetNameData()),
					FUploadTensorParameters::FTypeInfo::GetStructMetadata(),
					UploadParameters,
					ERDGPassFlags::Copy | ERDGPassFlags::NeverCull,
					[this, UploadParameters](FRHICommandListImmediate& RHICmdList)
					{
						FRHIBuffer* InputBuffer = UploadParameters->Input->GetRHI();

						// NOTE: We're using UAVMask to trigger the UAV barrier in RDG
						RHICmdList.Transition(FRHITransitionInfo(InputBuffer, ERHIAccess::CopyDest, ERHIAccess::UAVMask));
					}
				);

				GraphBuilder.Execute();
			});
		}

		if (bNeedsGPUCopy)
		{
			ENQUEUE_RENDER_COMMAND(FlushUploadTensorToGPU)([this] (FRHICommandListImmediate& RHICmdList)
			{
				FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("NNI:FlushUploadTensorsToGPU"));

				RHICmdList.SubmitCommandsHint();
				RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);

				GraphBuilder.Execute();
			});

			// TODO: Remove this sync point and move session run to render thread
			FNeuralNetworkInferenceUtils::WaitUntilRHIFinished();
		}
	}

	if (InDeviceType == ENeuralDeviceType::GPU)
	{
		FNNIGPUProfiler::Instance()->EventBegin("NNI:SessionRun");
	}

	Session->Run(Ort::RunOptions{ nullptr },
		InputTensorNames.GetData(), &InputOrtTensors[0], InputTensorNames.Num(),
		OutputTensorNames.GetData(), &OutputOrtTensors[0], OutputTensorNames.Num());

	if (InDeviceType == ENeuralDeviceType::GPU)
	{
		FNNIGPUProfiler::Instance()->EventEnd();
	}
}

bool UNeuralNetwork::FImplBackEndUEAndORT::InitializedAndConfigureMembers(
			TSharedPtr<FImplBackEndUEAndORT>& InOutImplBackEndUEAndORT,
			FOnAsyncRunCompleted& InOutOnAsyncRunCompletedDelegate,
			std::atomic<bool>& bInOutIsBackgroundThreadRunning,
			FCriticalSection& InOutResoucesCriticalSection,
			const FString& InModelFullFilePath,
			const ENeuralDeviceType InDeviceType)
{
	// Initialize InOutImplBackEndUEAndORT
	if (!InOutImplBackEndUEAndORT.IsValid())
	{
		InOutImplBackEndUEAndORT = MakeShared<FImplBackEndUEAndORT>(InOutOnAsyncRunCompletedDelegate, bInOutIsBackgroundThreadRunning,
			InOutResoucesCriticalSection);

		// Set up ORT and create an environment
		const char* const ModelFullFilePathCharPtr = TCHAR_TO_ANSI(*InModelFullFilePath);
		// @todo: ModelFullFilePathCharPtr -> I thought any unique string would work, but it might be output logging file, so it has to be a non-existing file!
		InOutImplBackEndUEAndORT->Environment = MakeUnique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, ModelFullFilePathCharPtr);

		InOutImplBackEndUEAndORT->Allocator = MakeUnique<Ort::AllocatorWithDefaultOptions>();

		InOutImplBackEndUEAndORT->AllocatorInfo = MakeUnique<Ort::MemoryInfo>(Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU));
	}

	InOutImplBackEndUEAndORT->ClearResources();

	// Configure InOutImplBackEndUEAndORT
	if (!InOutImplBackEndUEAndORT->ConfigureMembers(InDeviceType))
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("FImplBackEndUEAndORT::InitializedAndConfigureMembers(): ConfigureMembers failed."));
		return false;
	}

	return true;
}

bool UNeuralNetwork::FImplBackEndUEAndORT::ConfigureMembers(const ENeuralDeviceType InDeviceType)
{
	// Configure Session
	SessionOptions = MakeUnique<Ort::SessionOptions>();

	// Configure number threads
	SessionOptions->SetIntraOpNumThreads(2);
	// Uncomment if you want to change the priority of the threads, by default is TPri_Normal
	SessionOptions->SetPriorityOpThreads(EThreadPriority::TPri_Normal);

	// Configure Provider
	// GPU
	if (InDeviceType == ENeuralDeviceType::GPU)
	{
#ifdef PLATFORM_WIN64
		// To create a DirectML device we need to check that we're using DX12 first
		if (!IsGPUConfigCompatible())
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("FImplBackEndUEAndORT::ConfigureMembers(): UEAndORT back end for GPU needs DX12 enabled."));
			return false;
		}

		// Get adapter's D3D12 device that we would like to share with DirectML execution provider
		// NOTE: For now we're only using first device that has Dadapter 0 and device 0
		FD3D12DynamicRHI* Rhi = static_cast<FD3D12DynamicRHI*>(GDynamicRHI);

		if (Rhi->GetNumAdapters() > 1 || Rhi->GetAdapter(0).GetDesc().NumDeviceNodes > 1)
		{
			UE_LOG(LogNeuralNetworkInference, Warning,
				TEXT("FImplBackEndUEAndORT::ConfigureMembers(): There are multiple (%d) adapters and/or multiple (%d) devices, using device at index 0."),
				Rhi->GetNumAdapters(), Rhi->GetAdapter(0).GetDesc().NumDeviceNodes);
			return false;
		}

		ID3D12Device* NativeDevice = Rhi->GetAdapter(0).GetD3DDevice();

		// Make sure that we have one DMLDevice per D3D12 device
		IDMLDevice* DmlDevice = FPrivateImplBackEndUEAndORT::GetDMLDeviceThreadSafe(NativeDevice);

		if (!DmlDevice)
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("FImplBackEndUEAndORT::ConfigureMembers(): Invalid DML device found."));
			return false;
		}

		// Get a ID3D12CommandQueue as well
		// TODO: Should we create our own queue?
		ID3D12CommandQueue* NativeCmdQ = Rhi->RHIGetD3DCommandQueue();

		// ORT GPU (Direct ML)
		SessionOptions->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL); // ORT_ENABLE_ALL, ORT_ENABLE_EXTENDED, ORT_ENABLE_BASIC, ORT_DISABLE_ALL

		// Get DML API
		const OrtApi* OrtApi = OrtGetApiBase()->GetApi(ORT_API_VERSION);
		
		OrtApi->GetExecutionProviderApi(/*onnxruntime::DML*/ "DML", ORT_API_VERSION, (const void**) &DmlApi);
		if (!DmlApi)
		{
			UE_LOG(LogNeuralNetworkInference, Warning,
				TEXT("FImplBackEndUEAndORT::ConfigureMembers(): Failed to obtain OrtDmlApi."));
			return false;
		}

		// Set session options
		if (DmlApi->SessionOptionsAppendExecutionProvider_DML1(*SessionOptions.Get(), DmlDevice, NativeCmdQ))
		{
			UE_LOG(LogNeuralNetworkInference, Warning,
				TEXT("FImplBackEndUEAndORT::ConfigureMembers(): Some error occurred when using OrtDmlApi::SessionOptionsAppendExecutionProvider_DML1."));
			return false;
		}
		return true; // @todo: Remove this line when NNI_HLSL is working
#else
		UE_LOG(LogNeuralNetworkInference, Warning,
			TEXT("FImplBackEndUEAndORT::ConfigureMembers(): GPU mode only supported in Windows for now. Please, switch to CPU or to Windows."));

		//SessionOptions->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_DISABLE_ALL); // ORT_ENABLE_ALL, ORT_ENABLE_EXTENDED, ORT_ENABLE_BASIC, ORT_DISABLE_ALL
		//if (OrtSessionOptionsAppendExecutionProvider_NNI_HLSL(*SessionOptions, 0))
		//{
		//	UE_LOG(LogNeuralNetworkInference, Warning, TEXT("FImplBackEndUEAndORT::ConfigureMembers(): Some error occurred."));
		//	return false;
		//}
#endif //PLATFORM_WIN64
	}
	// CPU
	//else // @todo: Uncomment this line when NNI_HLSL is working
	{
#ifdef WITH_NNI_CPU_NOT_RECOMMENDED
		// NNI CPU (Deprecated)
		SessionOptions->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_DISABLE_ALL); // ORT_ENABLE_ALL, ORT_ENABLE_EXTENDED, ORT_ENABLE_BASIC, ORT_DISABLE_ALL
		if (OrtSessionOptionsAppendExecutionProvider_NNI_CPU(*SessionOptions))
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("FImplBackEndUEAndORT::ConfigureMembers(): OrtSessionOptionsAppendExecutionProvider_NNI_CPU failed."));
			return false;
		}
#else
		// ORT CPU
		SessionOptions->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL); // ORT_ENABLE_ALL, ORT_ENABLE_EXTENDED, ORT_ENABLE_BASIC, ORT_DISABLE_ALL
#endif //ORT_CPU
	}

	return true;
}

bool UNeuralNetwork::FImplBackEndUEAndORT::ConfigureTensors(TArray<FNeuralTensor>& OutTensors, TArray<bool>* OutAreInputTensorSizesVariable,
	const ENeuralDeviceType InDeviceType, const ENeuralDeviceType InInputDeviceType, const ENeuralDeviceType InOutputDeviceType)
{
	const bool bIsInput = (OutAreInputTensorSizesVariable != nullptr);
	TArray<const char*> TensorNames;
	TArray<ENeuralDataType> TensorDataTypes;
	TArray<TArray<int64>> TensorSizes;
	TArray<ENeuralTensorTypeGPU> TensorGPUTypes;

	const uint32 NumberTensors = bIsInput ? Session->GetInputCount() : Session->GetOutputCount();
	if (OutAreInputTensorSizesVariable)
	{
		OutAreInputTensorSizesVariable->SetNum(NumberTensors);
	}
	for (uint32 TensorIndex = 0; TensorIndex < NumberTensors; ++TensorIndex)
	{
		// Get node name
		{
			const char* TensorName = bIsInput ? Session->GetInputName(TensorIndex, *Allocator) : Session->GetOutputName(TensorIndex, *Allocator);
			TensorNames.Emplace(TensorName);
		}

		// Get node type
		Ort::TypeInfo CurrentTypeInfo = bIsInput ? Session->GetInputTypeInfo(TensorIndex) : Session->GetOutputTypeInfo(TensorIndex);

		Ort::TensorTypeAndShapeInfo CurrentTensorInfo = CurrentTypeInfo.GetTensorTypeAndShapeInfo();

		ENeuralDataType TensorDataType;
		{
			const ONNXTensorElementDataType ONNXTensorElementDataTypeEnum = CurrentTensorInfo.GetElementType();
			if (ONNXTensorElementDataTypeEnum == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
			{
				TensorDataType = ENeuralDataType::Float;
			}
			//else if (ONNXTensorElementDataTypeEnum == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32)
			//{
			//	TensorDataType = ENeuralDataType::Int32;
			//}
			//else if (ONNXTensorElementDataTypeEnum == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64)
			//{
			//	TensorDataType = ENeuralDataType::Int64;
			//}
			//else if (ONNXTensorElementDataTypeEnum == ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT32)
			//{
			//	TensorDataType = ENeuralDataType::UInt32;
			//}
			//else if (ONNXTensorElementDataTypeEnum == ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT64)
			//{
			//	TensorDataType = ENeuralDataType::UInt64;
			//}
			else
			{
				TensorDataType = ENeuralDataType::None;
				UE_LOG(LogNeuralNetworkInference, Warning,
					TEXT("FImplBackEndUEAndORT::ConfigureTensors(): ONNXTensorElementDataTypeEnum = %d not implemented yet."),
					(int32)ONNXTensorElementDataTypeEnum);
				return false;
			}
		}
		TensorDataTypes.Push(TensorDataType);

		// Get input shapes/dims
		TArray<int64> CurrentTensorSizes;
		{
			for (const int64_t CurrentTensorSize : CurrentTensorInfo.GetShape())
			{
				if (OutAreInputTensorSizesVariable)
				{
					(*OutAreInputTensorSizesVariable)[TensorIndex] |= (CurrentTensorSize < 0);
				}
				// Negative (variable) dimensions not implemented yet
				if (CurrentTensorSize < 0)
				{
					CurrentTensorSizes.Push(1);
					UE_LOG(LogNeuralNetworkInference, Display,
						TEXT("Negative (i.e., variable) dimensions not allowed yet, hard-coded to 1. Let us know if you really need variable dimensions."
							" Keep in mind that fixed sizes might allow additional optimizations and speedup of the network during Run()."));
				}
				else
				{
					CurrentTensorSizes.Push(CurrentTensorSize);
				}
			}
		}
		TensorSizes.Push(CurrentTensorSizes);

		// @todo: Should caller specify tensor GPU type?
		// Input/Output tensor GPU type means that data should not be copied from CPU
		// Generic means that data is on the CPU and it will be copied to GPU
		ENeuralTensorTypeGPU	TensorGPUType;

		if (InDeviceType == ENeuralDeviceType::GPU)
		{
			if (bIsInput)
			{
				TensorGPUType = InInputDeviceType == ENeuralDeviceType::GPU ? ENeuralTensorTypeGPU::Input : ENeuralTensorTypeGPU::Generic;
			}
			else
			{
				TensorGPUType = InOutputDeviceType == ENeuralDeviceType::GPU ? ENeuralTensorTypeGPU::Output : ENeuralTensorTypeGPU::Generic;
			}
		}
		else
		{
			TensorGPUType = ENeuralTensorTypeGPU::Generic;
		}

		TensorGPUTypes.Push(TensorGPUType);

		CurrentTypeInfo.release();
	}

	return SetTensorsFromNetwork(OutTensors, TensorNames, TensorDataTypes, TensorSizes, TensorGPUTypes, bIsInput);
}

bool UNeuralNetwork::FImplBackEndUEAndORT::SetTensorsFromNetwork(TArray<FNeuralTensor>& OutTensors, TArray<const char*>& InTensorNames,
	TArray<ENeuralDataType>& InTensorDataTypes, TArray<TArray<int64>>& InSizes, TArray<ENeuralTensorTypeGPU>& InTensorGPUTypes, const bool bIsInput)
{
	const int32 TensorNumber = InTensorNames.Num();
	if (InTensorDataTypes.Num() != TensorNumber || InSizes.Num() != TensorNumber)
	{
		UE_LOG(LogNeuralNetworkInference, Warning,
			TEXT("FImplBackEndUEAndORT::SetTensorsFromNetwork(): InTensorNames.Num() == InTensorDataTypes.Num() == InSizes.Num() failed, %d vs. %d vs. %d."),
			InTensorNames.Num(), InTensorDataTypes.Num(), InSizes.Num());
		return false;
	}

	// Swap variables
	TArray<const char*>& TensorNames = (bIsInput ? InputTensorNames : OutputTensorNames);
	Swap(TensorNames, InTensorNames);

	// Note: Switching from/to CPU to/from GPU would cause the FNeuralTensors to be re-initialized. We need to avoid that. For that,
	// we will only re-allocate the tensors...
	// - If bAreTensorsAlreadyCreatedWithRightNames == false, meaning tensors had not been created until now for this network.
	// - And if the existing tensors have the right size, given that SetNumUninitialized() only re-allocates them if their size has changed.

	// Fill bAreTensorsAlreadyCreatedWithRightNames - Check if tensors already created with the right names
	bool bAreTensorsAlreadyCreatedWithRightNames = (OutTensors.Num() == TensorNames.Num());
	if (bAreTensorsAlreadyCreatedWithRightNames)
	{
		for (int32 TensorIndex = 0; TensorIndex < TensorNumber; ++TensorIndex)
		{
			if (OutTensors[TensorIndex].GetName() != ANSI_TO_TCHAR(TensorNames[TensorIndex]))
			{
				bAreTensorsAlreadyCreatedWithRightNames = false;
				break;
			}
		}
	}

	// Assign name to each input/output tensor
	if (!bAreTensorsAlreadyCreatedWithRightNames)
	{
		OutTensors.Empty();
		for (int32 TensorIndex = 0; TensorIndex < TensorNumber; ++TensorIndex)
		{
			const char* TensorName = TensorNames[TensorIndex];
			OutTensors.Emplace(FNeuralTensor(ANSI_TO_TCHAR(TensorName), InTensorGPUTypes[TensorIndex]));
		}
	}
	else
	{
		for (int32 TensorIndex = 0; TensorIndex < TensorNumber; ++TensorIndex)
		{
			OutTensors[TensorIndex].SetTensorTypeGPU(InTensorGPUTypes[TensorIndex]);
		}
	}

	ensureMsgf(OutTensors.Num() == TensorNumber, TEXT("OutTensors.Num() == TensorNumber failed, %d != %d."), OutTensors.Num(), TensorNumber);

	// Config each TensorIndex
	TArray<Ort::Value>& OrtTensors = (bIsInput ? InputOrtTensors : OutputOrtTensors);
	for (int32 TensorIndex = 0; TensorIndex < TensorNumber; ++TensorIndex)
	{
		if (OrtTensors.Num() <= TensorIndex)
		{
			OrtTensors.Emplace(Ort::Value(nullptr));
		}

#ifdef PLATFORM_WIN64
		if (InTensorGPUTypes[TensorIndex] == ENeuralTensorTypeGPU::Generic)
		{
			// Pre-allocate TArray (if size is different)
			OutTensors[TensorIndex].SetNumUninitialized(InSizes[TensorIndex], InTensorDataTypes[TensorIndex]);
			// Link tensor with ORT blob
			LinkTensorToONNXRuntime(OutTensors, OrtTensors, *AllocatorInfo, TensorIndex);
		}
		else if (InTensorGPUTypes[TensorIndex] == ENeuralTensorTypeGPU::Input || InTensorGPUTypes[TensorIndex] == ENeuralTensorTypeGPU::Output)
		{
			// @todo: should we remove this? It's currently used to read memory from GPU to CPU
			OutTensors[TensorIndex].SetNumUninitialized(InSizes[TensorIndex], InTensorDataTypes[TensorIndex]);

			OutTensors[TensorIndex].SetEnableGPU(true);

			// @todo: This requires SetNumUnitialized() to be run, otherwise Size and Volume will be set to 0
			void* D3DResource = nullptr;

			if (!OutTensors[TensorIndex].InitPooledBuffer(&D3DResource))
			{
				UE_LOG(LogNeuralNetworkInference, Warning,
					TEXT("FImplBackEndUEAndORT::SetTensorsFromNetwork(): Failed to initialize pooled buffer"));
				return false;
			}

			// Link tensor with ORT blob
			if (!LinkTensorResourceToONNXRuntime(OutTensors[TensorIndex], OrtTensors[TensorIndex], D3DResource))
			{
				UE_LOG(LogNeuralNetworkInference, Warning,
					TEXT("FImplBackEndUEAndORT::SetTensorsFromNetwork(): Failed to link GPU resource to ONNX runtime"));
				return false;
			}
		}
#else
		// Pre-allocate TArray (if size is different)
		OutTensors[TensorIndex].SetNumUninitialized(InSizes[TensorIndex], InTensorDataTypes[TensorIndex]);
		// Link tensor with ORT blob
		LinkTensorToONNXRuntime(OutTensors, OrtTensors, *AllocatorInfo, TensorIndex);
#endif
	}

	return true;
}

void UNeuralNetwork::FImplBackEndUEAndORT::LinkTensorToONNXRuntime(TArray<FNeuralTensor>& InOutTensors, TArray<Ort::Value>& InOutOrtTensors,
	Ort::MemoryInfo& InOutAllocatorInfo, const int32 InTensorIndex)
{
	const TArray<int64>& Sizes = InOutTensors[InTensorIndex].GetSizes();
	if (Sizes.Num() > 0 && InOutTensors[InTensorIndex].Num() > 0)
	{
		FNeuralTensor& Tensor = InOutTensors[InTensorIndex];
		const int64 Volume = Tensor.Num();
		const int32 ArrayDimensions = Sizes.Num();

		const ENeuralDataType NeuralDataType = Tensor.GetDataType();
		if (NeuralDataType == ENeuralDataType::Float)
		{
#ifdef _WIN32
			const TArray<int64_t>& SizesInt64t = Sizes;
#else
			checkf(sizeof(int64) == sizeof(int64_t), TEXT("int64 and int64_t should both have the same size."));
			TArray<int64_t> SizesInt64t;
			SizesInt64t.SetNumUninitialized(ArrayDimensions);
			FMemory::Memcpy(SizesInt64t.GetData(), (int64_t*)Sizes.GetData(), sizeof(int64_t) * ArrayDimensions);
#endif //_WIN32
			InOutOrtTensors[InTensorIndex] = Ort::Value::CreateTensor<float>(InOutAllocatorInfo, Tensor.GetDataCasted<float>(), Volume, SizesInt64t.GetData(), ArrayDimensions);
		}
		//else if (NeuralDataType == ENeuralDataType::Double)
		//{
		//	InOutOrtTensors[InTensorIndex] = Ort::Value::CreateTensor<double>(InOutAllocatorInfo, Tensor.GetDataCasted<double>(), Volume, Sizes.GetData(), ArrayDimensions);
		//}
		else
		{
			UE_LOG(LogNeuralNetworkInference, Warning, TEXT("FImplBackEndUEAndORT::LinkTensorToONNXRuntime(): Not implemented (yet) for ENeuralDataType = %d."), (int32)NeuralDataType);
		}
	}
}

#ifdef PLATFORM_WIN64

bool UNeuralNetwork::FImplBackEndUEAndORT::LinkTensorResourceToONNXRuntime(FNeuralTensor& InOutTensor, Ort::Value& InOutOrtTensor, void* D3DResource)
{
	if (!DmlApi)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("FImplBackEndUEAndORT::LinkTensorResourceToONNXRuntime(): DmlGPUAllocator is not valid"));
		return false;
	}

	void* DmlGPUAllocation = nullptr;
	
	DmlApi->CreateGPUAllocationFromD3DResource(reinterpret_cast<ID3D12Resource*>(D3DResource), &DmlGPUAllocation);

	if (!DmlGPUAllocation)
	{
		UE_LOG(LogNeuralNetworkInference, Warning, TEXT("FImplBackEndUEAndORT::LinkTensorResourceToONNXRuntime(): DmlGPUAllocation is NULL"));
		return false;
	}

	DmlGPUResources.Emplace(DmlGPUAllocation);

	const TArray<int64>& Sizes = InOutTensor.GetSizes();
	if (Sizes.Num() > 0 && InOutTensor.Num() > 0)
	{
		const int32 ArrayDimensions = Sizes.Num();

		const ENeuralDataType NeuralDataType = InOutTensor.GetDataType();
		if (NeuralDataType == ENeuralDataType::Float)
		{
#ifdef _WIN32
			const TArray<int64_t>& SizesInt64t = Sizes;
#else
			checkf(sizeof(int64) == sizeof(int64_t), TEXT("int64 and int64_t should both have the same size."));
			TArray<int64_t> SizesInt64t;
			SizesInt64t.SetNumUninitialized(ArrayDimensions);
			FMemory::Memcpy(SizesInt64t.GetData(), (int64_t*)Sizes.GetData(), sizeof(int64_t) * ArrayDimensions);
#endif //_WIN32
			InOutOrtTensor = Ort::Value::CreateTensor(*DmlGPUMemoryInfo.Get(), DmlGPUAllocation, InOutTensor.NumInBytes(), SizesInt64t.GetData(),
				ArrayDimensions, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
		}
		//else if (NeuralDataType == ENeuralDataType::Double)
		//{
		//	InOutOrtTensor = Ort::Value::CreateTensor(DmlGPUAllocator->GetProviderMemoryInfo(), DmlGPUAllocation, InOutTensor.NumInBytes(), SizesInt64t.GetData(),
		//		ArrayDimensions, ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE);
		//}
		else
		{
			UE_LOG(LogNeuralNetworkInference, Warning,
				TEXT("FImplBackEndUEAndORT::LinkTensorToONNXRuntime(): Not implemented (yet) for ENeuralDataType = %d."), (int32)NeuralDataType);
			return false;
		}
	}

	return true;
}

#endif

#endif //WITH_UE_AND_ORT_SUPPORT
