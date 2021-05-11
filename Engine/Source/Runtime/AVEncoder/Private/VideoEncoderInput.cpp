// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoEncoderInput.h"
#include "VideoEncoderInputImpl.h"
#include "VideoEncoderCommon.h"
#include "VideoEncoderFactory.h"
#include "AVEncoderDebug.h"
#include "Misc/Paths.h"
#include "VideoCommon.h"

#if PLATFORM_WINDOWS

// // Disable macro redefinition warning for compatibility with Windows SDK 8+
// #pragma warning(push)
// #pragma warning(disable : 4005)	// macro redefinition

// #include "Windows/AllowWindowsPlatformTypes.h"
// #include "Windows/PreWindowsApi.h"
// 	#include <d3d11.h>
// 	#include <mftransform.h>
// 	#include <mfapi.h>
// 	#include <mferror.h>
// 	#include <mfidl.h>
// 	#include <codecapi.h>
// 	#include <shlwapi.h>
// 	#include <mfreadwrite.h>
// 	#include <d3d11_1.h>
// 	#include <d3d12.h>
// 	#include <dxgi1_4.h>
// #include "Windows/PostWindowsApi.h"
// #include "Windows/HideWindowsPlatformTypes.h"
#include "MicrosoftCommon.h"
#endif /* PLATFORM_WINDOWS */

#if WITH_CUDA
#include "CudaModule.h"
#endif

namespace AVEncoder
{

// *** FVideoEncoderInput *************************************************************************

// --- construct video encoder input based on expected input frame format -------------------------

TSharedPtr<FVideoEncoderInput> FVideoEncoderInput::CreateDummy(uint32 InWidth, uint32 InHeight, bool bIsResizable)
{
	TSharedPtr<FVideoEncoderInputImpl>	Input = MakeShared<FVideoEncoderInputImpl>();
	Input->bIsResizable = bIsResizable;

	if (!Input->SetupForDummy(InWidth, InHeight))
	{
		Input.Reset();
	}
	return Input;
}

TSharedPtr<FVideoEncoderInput> FVideoEncoderInput::CreateForYUV420P(uint32 InWidth, uint32 InHeight, bool bIsResizable)
{
	TSharedPtr<FVideoEncoderInputImpl>	Input = MakeShared<FVideoEncoderInputImpl>();
	Input->bIsResizable = bIsResizable;

	if (!Input->SetupForYUV420P(InWidth, InHeight))
	{
		Input.Reset();
	}
	return Input;
}

TSharedPtr<FVideoEncoderInput> FVideoEncoderInput::CreateForD3D11(void* InApplicationD3DDevice, uint32 InWidth, uint32 InHeight, bool bIsResizable)
{
#if PLATFORM_WINDOWS
	TSharedPtr<FVideoEncoderInputImpl>	Input = MakeShared<FVideoEncoderInputImpl>();
	Input->bIsResizable = bIsResizable;

	if (!Input->SetupForD3D11(static_cast<ID3D11Device*>(InApplicationD3DDevice), InWidth, InHeight))
	{
		Input.Reset();
	}
	return Input;
#endif

	return nullptr;
}

TSharedPtr<FVideoEncoderInput> FVideoEncoderInput::CreateForD3D12(void* InApplicationD3DDevice, uint32 InWidth, uint32 InHeight, bool bIsResizable)
{
#if PLATFORM_WINDOWS
	TSharedPtr<FVideoEncoderInputImpl>	Input = MakeShared<FVideoEncoderInputImpl>();
	Input->bIsResizable = bIsResizable;

	if (!Input->SetupForD3D12(static_cast<ID3D12Device*>(InApplicationD3DDevice), InWidth, InHeight))
	{
		Input.Reset();
	}
	return Input;
#endif

	return nullptr;
}

TSharedPtr<FVideoEncoderInput> FVideoEncoderInput::CreateForCUDA(void* InApplicationContext, uint32 InWidth, uint32 InHeight, bool bIsResizable)
{
#if WITH_CUDA

	TSharedPtr<FVideoEncoderInputImpl>	Input = MakeShared<FVideoEncoderInputImpl>();
	Input->bIsResizable = bIsResizable;
		
	if (!Input->SetupForCUDA(reinterpret_cast<CUcontext>(InApplicationContext), InWidth, InHeight))
	{
		Input.Reset();
	}
	return Input;
#else
	return nullptr;
#endif
}

void FVideoEncoderInput::SetResolution(uint32 InWidth, uint32 InHeight)
{
	this->Width = InWidth;
	this->Height = InHeight;
}

// --- encoder input frames -----------------------------------------------------------------------



// *** FVideoEncoderInputImpl *********************************************************************

FVideoEncoderInputImpl::~FVideoEncoderInputImpl()
{
	{
		FScopeLock						Guard(&ProtectFrames);
		if (ActiveFrames.Num() > 0)
		{
			UE_LOG(LogVideoEncoder, Error, TEXT("There are still %d active input frames."), ActiveFrames.Num());
		}
		
		check(ActiveFrames.Num() == 0);
		
		while (!AvailableFrames.IsEmpty())
		{
			FVideoEncoderInputFrameImpl* Frame = nullptr;
			AvailableFrames.Dequeue(Frame);
			delete Frame;
		}
	}
#if PLATFORM_WINDOWS
//	DEBUG_D3D11_REPORT_LIVE_DEVICE_OBJECT(FrameInfoD3D.EncoderDeviceD3D11);
#endif
}

bool FVideoEncoderInputImpl::SetupForDummy(uint32 InWidth, uint32 InHeight)
{
	FrameFormat = EVideoFrameFormat::Undefined;
	this->Width = InWidth;
	this->Height = InHeight;
	return true;
}

bool FVideoEncoderInputImpl::SetupForYUV420P(uint32 InWidth, uint32 InHeight)
{
	FrameFormat = EVideoFrameFormat::YUV420P;
	this->Width = InWidth;
	this->Height = InHeight;
	FrameInfoYUV420P.StrideY = InWidth;
	FrameInfoYUV420P.StrideU = (InWidth + 1) / 2;
	FrameInfoYUV420P.StrideV = (InWidth + 1) / 2;

	CollectAvailableEncoders();
	return true;
}

bool FVideoEncoderInputImpl::SetupForD3D11(void* InApplicationD3DDevice, uint32 InWidth, uint32 InHeight)
{
#if PLATFORM_WINDOWS

	TRefCountPtr<IDXGIDevice>	DXGIDevice;
	TRefCountPtr<IDXGIAdapter>	Adapter;

	HRESULT		Result = static_cast<ID3D11Device*>(InApplicationD3DDevice)->QueryInterface(__uuidof(IDXGIDevice), (void**)DXGIDevice.GetInitReference());
	if (Result != S_OK)
	{
		UE_LOG(LogVideoEncoder, Error, TEXT("ID3D11Device::QueryInterface() failed 0x%X - %s."), Result, *GetComErrorDescription(Result));
		return false;
	}
	else if ((Result = DXGIDevice->GetAdapter(Adapter.GetInitReference())) != S_OK)
	{
		UE_LOG(LogVideoEncoder, Error, TEXT("DXGIDevice::GetAdapter() failed 0x%X - %s."), Result, *GetComErrorDescription(Result));
		return false;
	}

	uint32				DeviceFlags = 0;
	D3D_FEATURE_LEVEL	FeatureLevel = D3D_FEATURE_LEVEL_11_0;
	D3D_FEATURE_LEVEL	ActualFeatureLevel;

	if ((Result = D3D11CreateDevice(
		Adapter,
		D3D_DRIVER_TYPE_UNKNOWN,
		NULL,
		DeviceFlags,
		&FeatureLevel,
		1,
		D3D11_SDK_VERSION,
		FrameInfoD3D.EncoderDeviceD3D11.GetInitReference(),
		&ActualFeatureLevel,
		FrameInfoD3D.EncoderDeviceContextD3D11.GetInitReference())) != S_OK)
	{
		UE_LOG(LogVideoEncoder, Error, TEXT("D3D11CreateDevice() failed 0x%X - %s."), Result, *GetComErrorDescription(Result));
		return false;
	}
	DEBUG_SET_D3D11_OBJECT_NAME(FrameInfoD3D.EncoderDeviceD3D11, "FVideoEncoderInputImpl");
	DEBUG_SET_D3D11_OBJECT_NAME(FrameInfoD3D.EncoderDeviceContextD3D11, "FVideoEncoderInputImpl");

	FrameFormat = EVideoFrameFormat::D3D11_R8G8B8A8_UNORM;
	this->Width = InWidth;
	this->Height = InHeight;

	CollectAvailableEncoders();
	return true;

#endif
	return false;
}

bool FVideoEncoderInputImpl::SetupForD3D12(void* InApplicationD3DDevice, uint32 InWidth, uint32 InHeight)
{
#if PLATFORM_WINDOWS

	LUID						AdapterLuid = static_cast<ID3D12Device*>(InApplicationD3DDevice)->GetAdapterLuid();
	TRefCountPtr<IDXGIFactory4>	DXGIFactory;
	HRESULT						Result;
	if ((Result = CreateDXGIFactory(IID_PPV_ARGS(DXGIFactory.GetInitReference()))) != S_OK)
	{
		UE_LOG(LogVideoEncoder, Error, TEXT("CreateDXGIFactory() failed 0x%X - %s."), Result, *GetComErrorDescription(Result));
		return false;
	}
	// get the adapter game uses to render
	TRefCountPtr<IDXGIAdapter>	Adapter;
	if ((Result = DXGIFactory->EnumAdapterByLuid(AdapterLuid, IID_PPV_ARGS(Adapter.GetInitReference()))) != S_OK)
	{
		UE_LOG(LogVideoEncoder, Error, TEXT("DXGIFactory::EnumAdapterByLuid() failed 0x%X - %s."), Result, *GetComErrorDescription(Result));
		return false;
	}

	uint32				DeviceFlags = 0;
	D3D_FEATURE_LEVEL	FeatureLevel = D3D_FEATURE_LEVEL_11_1;
	if ((Result = D3D12CreateDevice(Adapter, FeatureLevel, IID_PPV_ARGS(FrameInfoD3D.EncoderDeviceD3D12.GetInitReference()))) != S_OK)
	{
		UE_LOG(LogVideoEncoder, Error, TEXT("D3D11CreateDevice() failed 0x%X - %s."), Result, *GetComErrorDescription(Result));
		return false;
	}

	FrameFormat = EVideoFrameFormat::D3D12_R8G8B8A8_UNORM;
	this->Width = InWidth;
	this->Height = InHeight;

	CollectAvailableEncoders();
	return true;

#endif

	return false;
}


bool FVideoEncoderInputImpl::SetupForCUDA(void* InApplicationContext, uint32 InWidth, uint32 InHeight)
{
#if WITH_CUDA

	FrameInfoCUDA.EncoderContextCUDA = static_cast<CUcontext>(InApplicationContext);

	FrameFormat = EVideoFrameFormat::CUDA_R8G8B8A8_UNORM;
	this->Width = InWidth;
	this->Height = InHeight;

	CollectAvailableEncoders();
	return true;

#endif

	return false;
}

// --- available encoders -------------------------------------------------------------------------

void FVideoEncoderInputImpl::CollectAvailableEncoders()
{
	AvailableEncoders.Empty();
	for (const FVideoEncoderInfo& Info : FVideoEncoderFactory::Get().GetAvailable())
	{
		if (Info.SupportedInputFormats.Contains(FrameFormat))
		{
			AvailableEncoders.Push(Info);
		}
	}
}

const TArray<FVideoEncoderInfo>& FVideoEncoderInputImpl::GetAvailableEncoders()
{
	return AvailableEncoders;
}

// --- encoder input frames -----------------------------------------------------------------------

// create a user managed buffer
FVideoEncoderInputFrame* FVideoEncoderInputImpl::CreateBuffer(OnFrameReleasedCallback InOnFrameReleased)
{
	FVideoEncoderInputFrameImpl* Frame = CreateFrame();
	if (Frame)
	{
		FScopeLock						Guard(&ProtectFrames);
		UserManagedFrames.Emplace(Frame, MoveTemp(InOnFrameReleased));
	}
	return Frame;
}

// destroy user managed buffer
void FVideoEncoderInputImpl::DestroyBuffer(FVideoEncoderInputFrame* InBuffer)
{
	FVideoEncoderInputFrameImpl*	Frame = static_cast<FVideoEncoderInputFrameImpl*>(InBuffer);
	FScopeLock						Guard(&ProtectFrames);
	bool							bAnythingRemoved = false;
	for (int32 Index = UserManagedFrames.Num() - 1; Index >= 0; --Index)
	{
		if (UserManagedFrames[Index].Key == Frame)
		{
			UserManagedFrames.RemoveAt(Index);
			bAnythingRemoved = true;
		}
	}
	if (bAnythingRemoved)
	{
		delete Frame;
	}
}

// --- encoder input frames -----------------------------------------------------------------------

FVideoEncoderInputFrame* FVideoEncoderInputImpl::ObtainInputFrame()
{
	FVideoEncoderInputFrameImpl*	Frame = nullptr;
	FScopeLock						Guard(&ProtectFrames);
	if (!AvailableFrames.IsEmpty())
	{
		AvailableFrames.Dequeue(Frame);
	}
	else
	{
		Frame = CreateFrame();
	}
	if (Frame)
	{
		ActiveFrames.Push(Frame);
		return const_cast<FVideoEncoderInputFrame*>(Frame->Obtain());
	}
	return nullptr;
}

FVideoEncoderInputFrameImpl* FVideoEncoderInputImpl::CreateFrame()
{
	FVideoEncoderInputFrameImpl*	Frame = new FVideoEncoderInputFrameImpl(this);
	switch (FrameFormat)
	{
	case EVideoFrameFormat::Undefined:
		UE_LOG(LogVideoEncoder, Error, TEXT("Got undefined frame format!"))
		break;
	case EVideoFrameFormat::YUV420P:
		SetupFrameYUV420P(Frame);
		break;
	case EVideoFrameFormat::D3D11_R8G8B8A8_UNORM:
		SetupFrameD3D11(Frame);
		break;
	case EVideoFrameFormat::D3D12_R8G8B8A8_UNORM:
		SetupFrameD3D12(Frame);
		break;
	case EVideoFrameFormat::CUDA_R8G8B8A8_UNORM:
		SetupFrameCUDA(Frame);
		break;
	default:
		check(false);
		break;
	}
	return Frame;
}

void FVideoEncoderInputImpl::ReleaseInputFrame(FVideoEncoderInputFrame* InFrame)
{
	FVideoEncoderInputFrameImpl* InFrameImpl = static_cast<FVideoEncoderInputFrameImpl*>(InFrame);

	FScopeLock	Guard(&ProtectFrames);
	// check user managed buffers first
	for (const UserManagedFrame& Frame : UserManagedFrames)
	{
		if (Frame.Key == InFrameImpl)
		{
			Frame.Value(InFrameImpl);
			return;
		}
	}

	int32		NumRemoved = ActiveFrames.Remove(InFrameImpl);
	check(NumRemoved == 1);
	if (NumRemoved > 0)
	{
		// drop frame if format changed
		if (InFrame->GetFormat() != FrameFormat)
		{
			ProtectFrames.Unlock();
			delete InFrameImpl;
			// ProtectFrames.Lock();
			return;
		}
		
		// drop frame if resolution changed
		if(bIsResizable && (InFrame->GetWidth() != this->Width || InFrame->GetHeight() != this->Height))
		{
			ProtectFrames.Unlock();
			delete InFrameImpl;
			return;
		}

		AvailableFrames.Enqueue(InFrameImpl);
	}
}

void FVideoEncoderInputImpl::Flush()
{
	ProtectFrames.Lock();
	while (!AvailableFrames.IsEmpty())
	{
		FVideoEncoderInputFrameImpl*	Frame = nullptr;
		AvailableFrames.Dequeue(Frame);
		ProtectFrames.Unlock();
		delete Frame;
		ProtectFrames.Lock();
	}
	ProtectFrames.Unlock();
}

void FVideoEncoderInputImpl::SetupFrameYUV420P(FVideoEncoderInputFrameImpl* Frame)
{
	Frame->SetFormat(EVideoFrameFormat::YUV420P);
	Frame->SetWidth(this->Width);
	Frame->SetHeight(this->Height);
	FVideoEncoderInputFrame::FYUV420P& YUV420P = Frame->GetYUV420P();
	YUV420P.StrideY = FrameInfoYUV420P.StrideY;
	YUV420P.StrideU = FrameInfoYUV420P.StrideU;
	YUV420P.StrideV = FrameInfoYUV420P.StrideV;
	YUV420P.Data[0] = YUV420P.Data[1] = YUV420P.Data[2] = nullptr;
//	YUV420P.Data = new uint8[FrameInfoYUV420P.Height * FrameInfoYUV420P.StrideY +
//		(FrameInfoYUV420P.Height + 1) / 2 * FrameInfoYUV420P.StrideU +
//		(FrameInfoYUV420P.Height + 1) / 2 * FrameInfoYUV420P.StrideV];
}

void FVideoEncoderInputImpl::SetupFrameD3D11(FVideoEncoderInputFrameImpl* Frame)
{
#if PLATFORM_WINDOWS
	Frame->SetFormat(FrameFormat);
	Frame->SetWidth(this->Width);
	Frame->SetHeight(this->Height);
	FVideoEncoderInputFrame::FD3D11& Data = Frame->GetD3D11();
	Data.EncoderDevice = FrameInfoD3D.EncoderDeviceD3D11;
#endif
}

void FVideoEncoderInputImpl::SetupFrameD3D12(FVideoEncoderInputFrameImpl* Frame)
{
#if PLATFORM_WINDOWS
	Frame->SetFormat(FrameFormat);
	Frame->SetWidth(this->Width);
	Frame->SetHeight(this->Height);
	FVideoEncoderInputFrame::FD3D12& Data = Frame->GetD3D12();
	Data.EncoderDevice = FrameInfoD3D.EncoderDeviceD3D12;
#endif
}

void FVideoEncoderInputImpl::SetupFrameCUDA(FVideoEncoderInputFrameImpl* Frame)
{
#if WITH_CUDA
	Frame->SetFormat(FrameFormat);
	Frame->SetWidth(this->Width);
	Frame->SetHeight(this->Height);
	FVideoEncoderInputFrame::FCUDA& Data = Frame->GetCUDA();
	Data.EncoderDevice = FrameInfoCUDA.EncoderContextCUDA;
#endif
}

// ---

#if PLATFORM_WINDOWS
TRefCountPtr<ID3D11Device> FVideoEncoderInputImpl::GetD3D11EncoderDevice() const
{
	return FrameInfoD3D.EncoderDeviceD3D11;
}

TRefCountPtr<ID3D11Device> FVideoEncoderInputImpl::ForceD3D11InputFrames()
{
	// need to share D3D12 textures into D3D11 device (i.e. for nvenc)?
	if (FrameFormat == AVEncoder::EVideoFrameFormat::D3D12_R8G8B8A8_UNORM)
	{
		LUID						AdapterLuid = FrameInfoD3D.EncoderDeviceD3D12->GetAdapterLuid();
		TRefCountPtr<IDXGIFactory4>	DXGIFactory;
		HRESULT						Result;
		if ((Result = CreateDXGIFactory(IID_PPV_ARGS(DXGIFactory.GetInitReference()))) != S_OK)
		{
			UE_LOG(LogVideoEncoder, Error, TEXT("CreateDXGIFactory() failed 0x%X - %s."), Result, *GetComErrorDescription(Result));
			return nullptr;
		}
		// get the adapter game uses to render
		TRefCountPtr<IDXGIAdapter>	Adapter;
		if ((Result = DXGIFactory->EnumAdapterByLuid(AdapterLuid, IID_PPV_ARGS(Adapter.GetInitReference()))) != S_OK)
		{
			UE_LOG(LogVideoEncoder, Error, TEXT("DXGIFactory::EnumAdapterByLuid() failed 0x%X - %s."), Result, *GetComErrorDescription(Result));
			return nullptr;
		}

		uint32				DeviceFlags = 0;
		// DeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
		D3D_FEATURE_LEVEL	FeatureLevel = D3D_FEATURE_LEVEL_11_1;
		D3D_FEATURE_LEVEL	ActualFeatureLevel;
		if ((Result = D3D11CreateDevice(
			Adapter,
			D3D_DRIVER_TYPE_UNKNOWN,
			NULL,
			DeviceFlags,
			&FeatureLevel,
			1,
			D3D11_SDK_VERSION,
			FrameInfoD3D.EncoderDeviceD3D11.GetInitReference(),
			&ActualFeatureLevel,
			FrameInfoD3D.EncoderDeviceContextD3D11.GetInitReference())) != S_OK)
		{
			UE_LOG(LogVideoEncoder, Error, TEXT("D3D11CreateDevice() failed 0x%X - %s."), Result, *GetComErrorDescription(Result));
			return nullptr;
		}

		if (ActualFeatureLevel != D3D_FEATURE_LEVEL_11_1)
		{
			UE_LOG(LogVideoEncoder, Error, TEXT("D3D11CreateDevice() - failed to create device w/ feature level 11.1 - needed to encode textures from D3D12."));
			FrameInfoD3D.EncoderDeviceD3D11.SafeRelease();
			FrameInfoD3D.EncoderDeviceContextD3D11.SafeRelease();
			return nullptr;
		}

		DEBUG_SET_D3D11_OBJECT_NAME(FrameInfoD3D.EncoderDeviceD3D11, "FVideoEncoderInputImpl");
		DEBUG_SET_D3D11_OBJECT_NAME(FrameInfoD3D.EncoderDeviceContextD3D11, "FVideoEncoderInputImpl");

		FrameInfoD3D.EncoderDeviceD3D12.SafeRelease();
		FrameFormat = AVEncoder::EVideoFrameFormat::D3D11_R8G8B8A8_UNORM;

		// todo: drop pending frames...
	}
	return FrameInfoD3D.EncoderDeviceD3D11;
}
#endif

#if WITH_CUDA

CUcontext FVideoEncoderInputImpl::GetCUDAEncoderContext() const
{
	return FrameInfoCUDA.EncoderContextCUDA;
}

#endif

// *** FVideoEncoderInputFrame ********************************************************************

FVideoEncoderInputFrame::FVideoEncoderInputFrame()
	: FrameID(0)
	, NumReferences(0)
	, Format(EVideoFrameFormat::Undefined)
	, Width(0)
	, Height(0)
	, bFreeYUV420PData(false)
{
	static FThreadSafeCounter	NextFrameID = 0;
	FrameID = NextFrameID.Increment();
}

FVideoEncoderInputFrame::FVideoEncoderInputFrame(const FVideoEncoderInputFrame& CloneFrom)
	: FrameID(CloneFrom.FrameID)
	, NumReferences(0)
	, Format(CloneFrom.Format)
	, Width(CloneFrom.Width)
	, Height(CloneFrom.Height)
	, bFreeYUV420PData(false)
{
#if PLATFORM_WINDOWS
	D3D11.EncoderDevice = CloneFrom.D3D11.EncoderDevice;
	D3D11.Texture = CloneFrom.D3D11.Texture;
	if ((D3D11.EncoderTexture = CloneFrom.D3D11.EncoderTexture) != nullptr)
	{
		D3D11.EncoderTexture->AddRef();
	}
#endif

#if WITH_CUDA
	CUDA.EncoderDevice = CloneFrom.CUDA.EncoderDevice;
	CUDA.EncoderTexture = CloneFrom.CUDA.EncoderTexture;
#endif
}

FVideoEncoderInputFrame::~FVideoEncoderInputFrame()
{
	if (bFreeYUV420PData)
	{
		delete[] YUV420P.Data[0];
		delete[] YUV420P.Data[1];
		delete[] YUV420P.Data[2];
		YUV420P.Data[0] = YUV420P.Data[1] = YUV420P.Data[2] = nullptr;
		bFreeYUV420PData = false;
	}
#if PLATFORM_WINDOWS
	if (D3D11.EncoderTexture)
	{
		// check to make sure this frame holds the last reference
		auto	NumRef = D3D11.EncoderTexture->AddRef();
		if (NumRef > 2)
		{
			UE_LOG(LogVideoEncoder, Warning, TEXT("VideoEncoderFame - D3D11 input texture still holds %d references."), NumRef);
		}
		D3D11.EncoderTexture->Release();
		D3D11.EncoderTexture->Release();
		D3D11.EncoderTexture = nullptr;
	}
	if (D3D11.SharedHandle)
	{
		CloseHandle(D3D11.SharedHandle);
		D3D11.SharedHandle = nullptr;
	}
	if (D3D11.Texture && OnReleaseD3D11Texture)
	{
		OnReleaseD3D11Texture(D3D11.Texture);
	}
	if (D3D12.EncoderTexture)
	{
		// check to make sure this frame holds the last reference
		auto	NumRef = D3D12.EncoderTexture->AddRef();
		if (NumRef > 2)
		{
			UE_LOG(LogVideoEncoder, Warning, TEXT("VideoEncoderFame - D3D12 input texture still holds %d references."), NumRef);
		}
		D3D12.EncoderTexture->Release();
		D3D12.EncoderTexture->Release();
		D3D12.EncoderTexture = nullptr;
	}
	if (D3D12.Texture && OnReleaseD3D12Texture)
	{
		OnReleaseD3D12Texture(D3D12.Texture);
		D3D12.Texture = nullptr;
	}
#endif 

#if WITH_CUDA
	if (CUDA.EncoderTexture)
	{
		OnReleaseCUDATexture(CUDA.EncoderTexture);
		CUDA.EncoderTexture = nullptr;
	}
#endif

}

void FVideoEncoderInputFrame::SetYUV420P(const uint8* InDataY, const uint8* InDataU, const uint8* InDataV, uint32 InStrideY, uint32 InStrideU, uint32 InStrideV)
{
	if (Format == EVideoFrameFormat::YUV420P)
	{
		if (bFreeYUV420PData)
		{
			delete[] YUV420P.Data[0];
			delete[] YUV420P.Data[1];
			delete[] YUV420P.Data[2];
			bFreeYUV420PData = false;
		}
		YUV420P.Data[0] = InDataY;
		YUV420P.Data[1] = InDataU;
		YUV420P.Data[2] = InDataV;
		YUV420P.StrideY = InStrideY;
		YUV420P.StrideU = InStrideU;
		YUV420P.StrideV = InStrideV;
	}
}

static FThreadSafeCounter	_VideoEncoderInputFrameCnt{ 0 };

#if PLATFORM_WINDOWS
void FVideoEncoderInputFrame::SetTexture(ID3D11Texture2D* InTexture, FReleaseD3D11TextureCallback InOnReleaseTexture)
{
	if (Format == EVideoFrameFormat::D3D11_R8G8B8A8_UNORM)
	{
		check(D3D11.Texture == nullptr);
		if (!D3D11.Texture)
		{
			TRefCountPtr<IDXGIResource> DXGIResource;
			HANDLE						SharedHandle;
			HRESULT		Result = InTexture->QueryInterface(IID_PPV_ARGS(DXGIResource.GetInitReference()));
			if (FAILED(Result))
			{
				UE_LOG(LogVideoEncoder, Error, TEXT("ID3D11Device::QueryInterface() failed 0x%X - %s."), Result, *GetComErrorDescription(Result));
			}
			//
			// NOTE : The HANDLE IDXGIResource::GetSharedHandle gives us is NOT an NT Handle, and therefre we should not call CloseHandle on it
			//
			else if((Result = DXGIResource->GetSharedHandle(&SharedHandle)) != S_OK)
			{
				UE_LOG(LogVideoEncoder, Error, TEXT("IDXGIResource::GetSharedHandle() failed 0x%X - %s."), Result, *GetComErrorDescription(Result));
			}
			else if (SharedHandle == nullptr)
			{
				UE_LOG(LogVideoEncoder, Error, TEXT("IDXGIResource::GetSharedHandle() failed to return a shared texture resource no created as shared? (D3D11_RESOURCE_MISC_SHARED)."));
			}
			else if((Result = D3D11.EncoderDevice->OpenSharedResource(SharedHandle, __uuidof(ID3D11Texture2D), (LPVOID*)&D3D11.EncoderTexture)) != S_OK)
			{
				UE_LOG(LogVideoEncoder, Error, TEXT("ID3D11Device::OpenSharedResource() failed 0x%X - %s."), Result, *GetComErrorDescription(Result));
			}
			else
			{
				DEBUG_SET_D3D11_OBJECT_NAME(D3D11.EncoderTexture, "FVideoEncoderInputFrame::SetTexture()");
				D3D11.Texture = InTexture;
				OnReleaseD3D11Texture = InOnReleaseTexture;
			}
		}
	}
}

void FVideoEncoderInputFrame::SetTexture(ID3D12Resource* InTexture, FReleaseD3D12TextureCallback InOnReleaseTexture)
{
	if (Format == EVideoFrameFormat::D3D11_R8G8B8A8_UNORM)
	{
		check(D3D12.Texture == nullptr);
		check(D3D12.EncoderDevice == nullptr);
		check(D3D11.EncoderDevice != nullptr);
		if (!D3D12.Texture)
		{
			//TRefCountPtr<IDXGIResource> DXGIResource;
			TRefCountPtr<ID3D12Device>	OwnerDevice;
			HRESULT						Result;
			if((Result = InTexture->GetDevice(IID_PPV_ARGS(OwnerDevice.GetInitReference()))) != S_OK)
			{
				UE_LOG(LogVideoEncoder, Error, TEXT("ID3D11Device::QueryInterface() failed 0x%X - %s."), Result, *GetComErrorDescription(Result));
			}
			//
			// NOTE: ID3D12Device::CreateSharedHandle gives as an NT Handle, and so we need to call CloseHandle on it
			//
			else if ((Result = OwnerDevice->CreateSharedHandle(InTexture, NULL, GENERIC_ALL, *FString::Printf(TEXT("FVideoEncoderInputFrame_%d"), _VideoEncoderInputFrameCnt.Increment()), &D3D11.SharedHandle)) != S_OK)
			{
				UE_LOG(LogVideoEncoder, Error, TEXT("ID3D12Device::CreateSharedHandle() failed 0x%X - %s."), Result, *GetComErrorDescription(Result));
			}
			else if (D3D11.SharedHandle == nullptr)
			{
				UE_LOG(LogVideoEncoder, Error, TEXT("ID3D12Device::CreateSharedHandle() failed to return a shared texture resource no created as shared? (D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS)."));
			}
			else
			{
				TRefCountPtr <ID3D11Device1> Device1;
				if ((Result = D3D11.EncoderDevice->QueryInterface(IID_PPV_ARGS(Device1.GetInitReference()))) != S_OK)
				{
					UE_LOG(LogVideoEncoder, Error, TEXT("ID3D11Device::QueryInterface() failed 0x%X - %s."), Result, *GetComErrorDescription(Result));
				}
				else if ((Result = Device1->OpenSharedResource1(D3D11.SharedHandle, IID_PPV_ARGS(&D3D11.EncoderTexture))) != S_OK)
				{
					UE_LOG(LogVideoEncoder, Error, TEXT("ID3D11Device::OpenSharedResource1() failed 0x%X - %s."), Result, *GetComErrorDescription(Result));
				}
				else
				{
					DEBUG_SET_D3D11_OBJECT_NAME(D3D11.EncoderTexture, "FVideoEncoderInputFrame::SetTexture()");
					D3D12.Texture = InTexture;
					OnReleaseD3D12Texture = InOnReleaseTexture;
				}
			}
		}
	}
	else
	{
		check(D3D12.Texture == nullptr);
		D3D12.Texture = InTexture;
		OnReleaseD3D12Texture = InOnReleaseTexture;
	}
}
#endif

#if WITH_CUDA

void FVideoEncoderInputFrame::SetTexture(CUarray InTexture, FReleaseCUDATextureCallback InOnReleaseTexture)
{
	if (Format == EVideoFrameFormat::CUDA_R8G8B8A8_UNORM)
	{
		CUDA.EncoderTexture = InTexture;
		OnReleaseCUDATexture = InOnReleaseTexture;
		if (!CUDA.EncoderTexture)
		{
			UE_LOG(LogVideoEncoder, Warning, TEXT("SetTexture | Cuda device pointer is null"));
		}
	}
}

#endif



// *** FVideoEncoderInputFrameImpl ****************************************************************

FVideoEncoderInputFrameImpl::FVideoEncoderInputFrameImpl(FVideoEncoderInputImpl* InInput)
	: Input(InInput)
{
}

FVideoEncoderInputFrameImpl::FVideoEncoderInputFrameImpl(const FVideoEncoderInputFrameImpl& InCloneFrom, FCloneDestroyedCallback InCloneDestroyedCallback)
	: FVideoEncoderInputFrame(InCloneFrom)
	, Input(InCloneFrom.Input)
	, ClonedReference(InCloneFrom.Obtain())
	, OnCloneDestroyed(MoveTemp(InCloneDestroyedCallback))
{
}

FVideoEncoderInputFrameImpl::~FVideoEncoderInputFrameImpl()
{
	if (ClonedReference)
	{
		ClonedReference->Release();
	}
	else
	{
	}
}

void FVideoEncoderInputFrameImpl::Release() const
{
	if (NumReferences.Decrement() == 0)
	{
		if (ClonedReference)
		{
			OnCloneDestroyed(this);
			delete this;
		}
		else
		{
			Input->ReleaseInputFrame(const_cast<FVideoEncoderInputFrameImpl*>(this));
		}
	}
}

// Clone frame - this will create a copy that references the original until destroyed
const FVideoEncoderInputFrame* FVideoEncoderInputFrameImpl::Clone(FCloneDestroyedCallback InCloneDestroyedCallback) const
{
	FVideoEncoderInputFrameImpl*	ClonedFrame = new FVideoEncoderInputFrameImpl(*this, MoveTemp(InCloneDestroyedCallback));
	return ClonedFrame;
}



} /* namespace AVEncoder */
