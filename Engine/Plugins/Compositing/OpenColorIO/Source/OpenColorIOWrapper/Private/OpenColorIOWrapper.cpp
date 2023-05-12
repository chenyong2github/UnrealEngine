// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenColorIOWrapper.h"
#include "OpenColorIOWrapperModule.h"

#include "Containers/Map.h"
#include "ColorSpace.h"
#include "Engine/TextureDefines.h"
#include "ImageCore.h"

#if WITH_OCIO
THIRD_PARTY_INCLUDES_START
#include "OpenColorIO/OpenColorIO.h"
THIRD_PARTY_INCLUDES_END
#endif

#define LOCTEXT_NAMESPACE "OpenColorIOWrapper"

namespace OpenColorIOWrapper
{
	/** OCIO_NAMESPACE::ROLE_INTERCHANGE_SCENE equivalent, since we currently cannot delay-load this definition. */
	constexpr const ANSICHAR* GetInterchangeName()
	{
		return "aces_interchange";
	}

	// Build routine since there is no FAnsiString
	TUniquePtr<ANSICHAR[]> MakeAnsiString(const TCHAR* Str)
	{
		int32 Num = FCString::Strlen(Str);
		TUniquePtr<ANSICHAR[]> Ret =  MakeUnique<ANSICHAR[]>(Num);
		FMemory::Memcpy(Ret.Get(), StringCast<ANSICHAR>(Str).Get(), Num);
		return Ret;
	}
}

const TCHAR* OpenColorIOWrapper::GetVersion()
{
#if WITH_OCIO
	return TEXT(OCIO_VERSION);
#else
	return TEXT("");
#endif // WITH_OCIO
}

struct FOpenColorIOConfigPimpl
{
#if WITH_OCIO
	OCIO_NAMESPACE::ConstConfigRcPtr Config = nullptr;
#endif // WITH_OCIO
};

struct FOpenColorIOProcessorPimpl
{
#if WITH_OCIO
	OCIO_NAMESPACE::ConstProcessorRcPtr Processor = nullptr;

	/** Get processor optimization flags. */
	static OCIO_NAMESPACE::OptimizationFlags GetOptimizationFlags()
	{
		return static_cast<OCIO_NAMESPACE::OptimizationFlags>(
			OCIO_NAMESPACE::OptimizationFlags::OPTIMIZATION_DEFAULT |
			OCIO_NAMESPACE::OptimizationFlags::OPTIMIZATION_NO_DYNAMIC_PROPERTIES
		);
	}
#endif // WITH_OCIO
};

struct FOpenColorIOGPUProcessorPimpl
{
#if WITH_OCIO
	OCIO_NAMESPACE::ConstGPUProcessorRcPtr Processor = nullptr;
	OCIO_NAMESPACE::GpuShaderDescRcPtr ShaderDescription = nullptr;
#endif // WITH_OCIO
};

#if WITH_OCIO
namespace {
	TUniquePtr<OCIO_NAMESPACE::PackedImageDesc> GetImageDesc(const FImageView& InImage)
	{
		OCIO_NAMESPACE::ChannelOrdering Ordering;
		OCIO_NAMESPACE::BitDepth BitDepth;

		switch (InImage.Format)
		{
		case ERawImageFormat::BGRA8:
			Ordering = OCIO_NAMESPACE::CHANNEL_ORDERING_BGRA;
			BitDepth = OCIO_NAMESPACE::BIT_DEPTH_UINT8;
			break;
		case ERawImageFormat::RGBA16:
			Ordering = OCIO_NAMESPACE::CHANNEL_ORDERING_RGBA;
			BitDepth = OCIO_NAMESPACE::BIT_DEPTH_UINT16;
			break;
		case ERawImageFormat::RGBA16F:
			Ordering = OCIO_NAMESPACE::CHANNEL_ORDERING_RGBA;
			BitDepth = OCIO_NAMESPACE::BIT_DEPTH_F16;
			break;
		case ERawImageFormat::RGBA32F:
			Ordering = OCIO_NAMESPACE::CHANNEL_ORDERING_RGBA;
			BitDepth = OCIO_NAMESPACE::BIT_DEPTH_F32;
			break;
		default:

			UE_LOG(LogOpenColorIOWrapper, Log, TEXT("Unsupported texture format."));
			return nullptr;
		}

		return MakeUnique<OCIO_NAMESPACE::PackedImageDesc>(
			InImage.RawData,
			static_cast<long>(InImage.GetWidth()),
			static_cast<long>(InImage.GetHeight()),
			Ordering,
			BitDepth,
			OCIO_NAMESPACE::AutoStride,
			OCIO_NAMESPACE::AutoStride,
			OCIO_NAMESPACE::AutoStride
			);
	};
}
#endif // WITH_OCIO

TUniquePtr<FOpenColorIOConfigWrapper> FOpenColorIOConfigWrapper::CreateWorkingColorSpaceToInterchangeConfig()
{
#if WITH_OCIO
	TRACE_CPUPROFILER_EVENT_SCOPE(FOpenColorIOConfigWrapper::CreateWorkingColorSpaceToInterchangeConfig)
	using namespace OCIO_NAMESPACE;
	using namespace UE::Color;

	TUniquePtr<FOpenColorIOConfigWrapper> Result = MakeUnique<FOpenColorIOConfigWrapper>();

	ColorSpaceRcPtr AP0 = ColorSpace::Create();
	AP0->setName("ACES2065-1");
	AP0->setBitDepth(BIT_DEPTH_F32);
	AP0->setEncoding("scene-linear");

	ColorSpaceRcPtr WCS = ColorSpace::Create();
	WCS->setName(StringCast<ANSICHAR>(OpenColorIOWrapper::GetWorkingColorSpaceName()).Get());
	WCS->setBitDepth(BIT_DEPTH_F32);
	WCS->setEncoding("scene-linear");

	const FMatrix44d TransformMat = Transpose<double>(FColorSpaceTransform(FColorSpace::GetWorking(), FColorSpace(EColorSpace::ACESAP0)));
	MatrixTransformRcPtr MatrixTransform = MatrixTransform::Create();
	MatrixTransform->setMatrix(&TransformMat.M[0][0]);
	WCS->setTransform(MatrixTransform, COLORSPACE_DIR_TO_REFERENCE);

	ConfigRcPtr InterchangeConfig = Config::Create();
	InterchangeConfig->addColorSpace(AP0);
	InterchangeConfig->addColorSpace(WCS);
	InterchangeConfig->setRole("aces_interchange", "ACES2065-1");

	Result->Pimpl->Config = InterchangeConfig;

	return Result;
#else
	return nullptr;
#endif
}

FOpenColorIOConfigWrapper::FOpenColorIOConfigWrapper()
	: Pimpl(MakePimpl<FOpenColorIOConfigPimpl, EPimplPtrMode::DeepCopy>())
{ }

FOpenColorIOConfigWrapper::FOpenColorIOConfigWrapper(FStringView InFilePath, FInitializationOptions InOptions)
	: FOpenColorIOConfigWrapper()
{
#if WITH_OCIO
	TRACE_CPUPROFILER_EVENT_SCOPE(FOpenColorIOConfigWrapper::FOpenColorIOConfigWrapper)
#if !PLATFORM_EXCEPTIONS_DISABLED
	try
#endif
	{
		using namespace OCIO_NAMESPACE;

		ConstConfigRcPtr NewConfig = Config::CreateFromFile(StringCast<ANSICHAR>(InFilePath.GetData()).Get());

		if (NewConfig && InOptions.bAddWorkingColorSpace)
		{
			const TUniquePtr<ANSICHAR[]> AnsiWorkingColorSpaceName = OpenColorIOWrapper::MakeAnsiString(OpenColorIOWrapper::GetWorkingColorSpaceName());
			ConstColorSpaceRcPtr InterchangeCS = NewConfig->getColorSpace(NewConfig->getCanonicalName(OpenColorIOWrapper::GetInterchangeName()));

			// When the aces interchange color space is present, we add the working color space as an additional option.
			if (InterchangeCS != nullptr && NewConfig->getColorSpace(AnsiWorkingColorSpaceName.Get()) == nullptr)
			{
				ColorSpaceRcPtr WorkingCS = InterchangeCS->createEditableCopy();
				WorkingCS->setName(AnsiWorkingColorSpaceName.Get());
				WorkingCS->setFamily("UE");
				WorkingCS->clearAliases();

				ConfigRcPtr NewConfigCopy = NewConfig->createEditableCopy();
				NewConfigCopy->addColorSpace(WorkingCS);

				NewConfig = NewConfigCopy;
			}
		}

		Pimpl->Config = NewConfig;
	}
#if !PLATFORM_EXCEPTIONS_DISABLED
	catch (OCIO_NAMESPACE::Exception& Exc)
	{
		UE_LOG(LogOpenColorIOWrapper, Error, TEXT("Could not create OCIO configuration file for %s. Error message: %s."), InFilePath.GetData(), StringCast<TCHAR>(Exc.what()).Get());
	}
#endif
#else
	UE_LOG(LogOpenColorIOWrapper, Error, TEXT("The OpenColorIO library is not available, functionality will be disabled."));
#endif // WITH_OCIO
}

bool FOpenColorIOConfigWrapper::IsValid() const
{
#if WITH_OCIO
	return Pimpl->Config != nullptr;
#else
	return false;
#endif // WITH_OCIO
}

int32 FOpenColorIOConfigWrapper::GetNumColorSpaces() const
{
#if WITH_OCIO
	if (IsValid())
	{
		return Pimpl->Config->getNumColorSpaces(OCIO_NAMESPACE::SEARCH_REFERENCE_SPACE_ALL, OCIO_NAMESPACE::COLORSPACE_ACTIVE);
	}
#endif  // WITH_OCIO

	return 0;
}

FString FOpenColorIOConfigWrapper::GetColorSpaceName(int32 Index) const
{
#if WITH_OCIO
	if (IsValid())
	{
		const char* ColorSpaceName = Pimpl->Config->getColorSpaceNameByIndex(OCIO_NAMESPACE::SEARCH_REFERENCE_SPACE_ALL, OCIO_NAMESPACE::COLORSPACE_ACTIVE, Index);

		return StringCast<TCHAR>(ColorSpaceName).Get();
	}
#endif // WITH_OCIO

	return {};
}

int32 FOpenColorIOConfigWrapper::GetColorSpaceIndex(const TCHAR* InColorSpaceName)
{
#if WITH_OCIO
	if (IsValid())
	{
		return Pimpl->Config->getIndexForColorSpace(StringCast<ANSICHAR>(InColorSpaceName).Get());
	}
#endif // WITH_OCIO

	return false;
}

FString FOpenColorIOConfigWrapper::GetColorSpaceFamilyName(const TCHAR* InColorSpaceName) const
{
#if WITH_OCIO
	if (IsValid())
	{
		OCIO_NAMESPACE::ConstColorSpaceRcPtr ColorSpace = Pimpl->Config->getColorSpace(StringCast<ANSICHAR>(InColorSpaceName).Get());
		
		if (ColorSpace != nullptr)
		{
			return StringCast<TCHAR>(ColorSpace->getFamily()).Get();
		}
	}
#endif // WITH_OCIO

	return {};
}

int32 FOpenColorIOConfigWrapper::GetNumDisplays() const
{
#if WITH_OCIO
	if (IsValid())
	{
		return Pimpl->Config->getNumDisplays();
	}
#endif // WITH_OCIO

	return 0;
}

FString FOpenColorIOConfigWrapper::GetDisplayName(int32 Index) const
{
#if WITH_OCIO
	if (IsValid())
	{
		const char* DisplayName = Pimpl->Config->getDisplay(Index);

		return StringCast<TCHAR>(DisplayName).Get();
	}
#endif // WITH_OCIO

	return {};
}

int32 FOpenColorIOConfigWrapper::GetNumViews(const TCHAR* InDisplayName) const
{
#if WITH_OCIO
	if (IsValid())
	{
		return Pimpl->Config->getNumViews(StringCast<ANSICHAR>(InDisplayName).Get());
	}
#endif // WITH_OCIO

	return 0;
}

FString FOpenColorIOConfigWrapper::GetViewName(const TCHAR* InDisplayName, int32 Index) const
{
#if WITH_OCIO
	if (IsValid())
	{
		const char* ViewName = Pimpl->Config->getView(StringCast<ANSICHAR>(InDisplayName).Get(), Index);

		return StringCast<TCHAR>(ViewName).Get();
	}
#endif // WITH_OCIO

	return {};
}

FString FOpenColorIOConfigWrapper::GetDisplayViewTransformName(const TCHAR* InDisplayName, const TCHAR* InViewName) const
{
#if WITH_OCIO
	if (IsValid())
	{
		const char* TransformName = Pimpl->Config->getDisplayViewTransformName(StringCast<ANSICHAR>(InDisplayName).Get(), StringCast<ANSICHAR>(InViewName).Get());

		return StringCast<TCHAR>(TransformName).Get();
	}
#endif // WITH_OCIO

	return {};
}

FString FOpenColorIOConfigWrapper::GetCacheID() const
{
#if WITH_OCIO
	if (IsValid())
	{
		return StringCast<TCHAR>(Pimpl->Config->getCacheID()).Get();
	}
#endif // WITH_OCIO

	return {};
}

FString FOpenColorIOConfigWrapper::GetDebugString() const
{
	TStringBuilder<1024> DebugStringBuilder;
	
#if WITH_OCIO
	if (IsValid())
	{
		const OCIO_NAMESPACE::ConstConfigRcPtr& Config = Pimpl->Config;
		if (Config->getNumColorSpaces() > 0)
		{
			DebugStringBuilder.Append(TEXT("** ColorSpaces **\n"));
			
			const int32 NumCS = Config->getNumColorSpaces(
				OCIO_NAMESPACE::SEARCH_REFERENCE_SPACE_ALL,   // Iterate over scene & display color spaces.
				OCIO_NAMESPACE::COLORSPACE_ALL);              // Iterate over active & inactive color spaces.

			for (int32 IndexCS = 0; IndexCS < NumCS; ++IndexCS)
			{
				OCIO_NAMESPACE::ConstColorSpaceRcPtr cs = Config->getColorSpace(Config->getColorSpaceNameByIndex(
					OCIO_NAMESPACE::SEARCH_REFERENCE_SPACE_ALL,
					OCIO_NAMESPACE::COLORSPACE_ALL,
					IndexCS));

				DebugStringBuilder.Append(cs->getName());
				DebugStringBuilder.Append(TEXT("\n"));
			}

			DebugStringBuilder.Append(TEXT("** (Display, View) pairs **\n"));

			for (int32 IndexDisplay = 0; IndexDisplay < Config->getNumDisplaysAll(); ++IndexDisplay)
			{
				const ANSICHAR* DisplayName = Config->getDisplayAll(IndexDisplay);

				// Iterate over shared views.
				int32 NumViews = Config->getNumViews(DisplayName);
				for (int IndexView = 0; IndexView < NumViews; ++IndexView)
				{
					const ANSICHAR* ViewName = Config->getView(
						DisplayName,
						IndexView);

					DebugStringBuilder.Append(TEXT("("));
					DebugStringBuilder.Append(DisplayName);
					DebugStringBuilder.Append(TEXT(", "));
					DebugStringBuilder.Append(ViewName);
					DebugStringBuilder.Append(TEXT(")\n"));
				}
			}
		}
	}

#endif // WITH_OCIO

	return DebugStringBuilder.ToString();
}



FOpenColorIOProcessorWrapper::FOpenColorIOProcessorWrapper(
	const FOpenColorIOConfigWrapper* InConfig,
	FStringView InSourceColorSpace,
	FStringView InDestinationColorSpace,
	const TMap<FString, FString>& InContextKeyValues)
	: Pimpl(MakePimpl<FOpenColorIOProcessorPimpl, EPimplPtrMode::DeepCopy>())
	, OwnerConfig(InConfig)
	, WorkingColorSpaceTransformType(EOpenColorIOWorkingColorSpaceTransform::None)
{
	if (InSourceColorSpace == OpenColorIOWrapper::GetWorkingColorSpaceName())
	{
		WorkingColorSpaceTransformType = EOpenColorIOWorkingColorSpaceTransform::Source;
	}
	else if (InDestinationColorSpace == OpenColorIOWrapper::GetWorkingColorSpaceName())
	{
		WorkingColorSpaceTransformType = EOpenColorIOWorkingColorSpaceTransform::Destination;
	}

#if WITH_OCIO
#if !PLATFORM_EXCEPTIONS_DISABLED
	try
#endif
	{
		if (OwnerConfig != nullptr && OwnerConfig->IsValid())
		{
			const OCIO_NAMESPACE::ConstConfigRcPtr& Config = OwnerConfig->Pimpl->Config;
			 OCIO_NAMESPACE::ContextRcPtr Context = Config->getCurrentContext()->createEditableCopy();

			for (const TPair<FString, FString>& KeyValue : InContextKeyValues)
			{
				Context->setStringVar(TCHAR_TO_ANSI(*KeyValue.Key), TCHAR_TO_ANSI(*KeyValue.Value));
			}

			Pimpl->Processor = Config->getProcessor(
				Context,
				StringCast<ANSICHAR>(InSourceColorSpace.GetData()).Get(),
				StringCast<ANSICHAR>(InDestinationColorSpace.GetData()).Get()
			);
		}
	}
#if !PLATFORM_EXCEPTIONS_DISABLED
	catch (OCIO_NAMESPACE::Exception& Exc)
	{
		UE_LOG(LogOpenColorIOWrapper, Log, TEXT("Failed to create processor for [%s, %s]. Error message: %s"), InSourceColorSpace.GetData(), InDestinationColorSpace.GetData(), StringCast<TCHAR>(Exc.what()).Get());
	}
#endif
#endif // WITH_OCIO
}

FOpenColorIOProcessorWrapper::FOpenColorIOProcessorWrapper(
	const FOpenColorIOConfigWrapper* InConfig,
	FStringView InSourceColorSpace,
	FStringView InDisplay,
	FStringView InView,
	bool bInverseDirection,
	const TMap<FString, FString>& InContextKeyValues)
	: Pimpl(MakePimpl<FOpenColorIOProcessorPimpl, EPimplPtrMode::DeepCopy>())
	, OwnerConfig(InConfig)
	, WorkingColorSpaceTransformType(EOpenColorIOWorkingColorSpaceTransform::None)
{
	if (InSourceColorSpace == OpenColorIOWrapper::GetWorkingColorSpaceName())
	{
		if (bInverseDirection)
		{
			WorkingColorSpaceTransformType = EOpenColorIOWorkingColorSpaceTransform::Destination;
		}
		else
		{
			WorkingColorSpaceTransformType = EOpenColorIOWorkingColorSpaceTransform::Source;
		}
	}

#if WITH_OCIO
#if !PLATFORM_EXCEPTIONS_DISABLED
	try
#endif
	{
		if (OwnerConfig != nullptr && OwnerConfig->IsValid())
		{
			const OCIO_NAMESPACE::ConstConfigRcPtr& Config = OwnerConfig->Pimpl->Config;
			OCIO_NAMESPACE::ContextRcPtr Context = Config->getCurrentContext()->createEditableCopy();

			for (const TPair<FString, FString>& KeyValue : InContextKeyValues)
			{
				Context->setStringVar(TCHAR_TO_ANSI(*KeyValue.Key), TCHAR_TO_ANSI(*KeyValue.Value));
			}

			Pimpl->Processor = Config->getProcessor(
				StringCast<ANSICHAR>(InSourceColorSpace.GetData()).Get(),
				StringCast<ANSICHAR>(InDisplay.GetData()).Get(),
				StringCast<ANSICHAR>(InView.GetData()).Get(),
				static_cast<OCIO_NAMESPACE::TransformDirection>(bInverseDirection)
			);
		}
	}
#if !PLATFORM_EXCEPTIONS_DISABLED
	catch (OCIO_NAMESPACE::Exception& Exc)
	{
		UE_LOG(LogOpenColorIOWrapper, Log, TEXT("Failed to create processor for [%s, %s, %s, %s]. Error message: %s"), InSourceColorSpace.GetData(), InDisplay.GetData(), InView.GetData(), (bInverseDirection ? TEXT("Inverse") : TEXT("Forward")), StringCast<TCHAR>(Exc.what()).Get());
	}
#endif
#endif // WITH_OCIO
}

bool FOpenColorIOProcessorWrapper::IsValid() const
{
#if WITH_OCIO
	return OwnerConfig != nullptr && Pimpl->Processor != nullptr;
#else
	return false;
#endif
}

FOpenColorIOCPUProcessorWrapper::FOpenColorIOCPUProcessorWrapper(FOpenColorIOProcessorWrapper InProcessor)
	: ParentProcessor(MoveTemp(InProcessor))
{
}

bool FOpenColorIOCPUProcessorWrapper::IsValid() const
{
	return ParentProcessor.IsValid();
}

bool FOpenColorIOCPUProcessorWrapper::TransformImage(const FImageView& InOutImage) const
{
#if WITH_OCIO
	TRACE_CPUPROFILER_EVENT_SCOPE(FOpenColorIOCPUProcessorWrapper::TransformImage)
#if !PLATFORM_EXCEPTIONS_DISABLED
	try
#endif
	{
		using namespace OCIO_NAMESPACE;

		if (IsValid())
		{
			TUniquePtr<PackedImageDesc> ImageDesc = GetImageDesc(InOutImage);
			if (ImageDesc)
			{
				const TUniquePtr<ANSICHAR[]> AnsiWorkingColorSpaceName = OpenColorIOWrapper::MakeAnsiString(OpenColorIOWrapper::GetWorkingColorSpaceName());
				ConstConfigRcPtr	InterchangeConfig = IOpenColorIOWrapperModule::Get().GetWorkingColorSpaceToInterchangeConfig()->Pimpl->Config;
				ConstConfigRcPtr Config = ParentProcessor.OwnerConfig->Pimpl->Config;

				BitDepth BitDepth = ImageDesc->getBitDepth();

				// Conditionally apply a conversion from the working color space to interchange space
				if (ParentProcessor.WorkingColorSpaceTransformType == EOpenColorIOWorkingColorSpaceTransform::Source)
				{
					ConstProcessorRcPtr	InterchangeProcessor = InterchangeConfig->GetProcessorFromConfigs(
						InterchangeConfig,
						AnsiWorkingColorSpaceName.Get(),
						Config,
						Config->getCanonicalName(OpenColorIOWrapper::GetInterchangeName()));

					ConstCPUProcessorRcPtr InterchangeCPUProcessor = InterchangeProcessor->getOptimizedCPUProcessor(BitDepth, BitDepth, OPTIMIZATION_DEFAULT);
					InterchangeCPUProcessor->apply(*ImageDesc);
				}

				// Apply the main color transformation
				ConstCPUProcessorRcPtr CPUProcessor = ParentProcessor.Pimpl->Processor->getOptimizedCPUProcessor(BitDepth, BitDepth, OPTIMIZATION_DEFAULT);
				CPUProcessor->apply(*ImageDesc);

				// Conditionally apply a conversion from the interchange space to the working color space
				if (ParentProcessor.WorkingColorSpaceTransformType == EOpenColorIOWorkingColorSpaceTransform::Destination)
				{
					ConstProcessorRcPtr	InterchangeProcessor = InterchangeConfig->GetProcessorFromConfigs(
						Config,
						Config->getCanonicalName(OpenColorIOWrapper::GetInterchangeName()),
						InterchangeConfig,
						AnsiWorkingColorSpaceName.Get());

					ConstCPUProcessorRcPtr InterchangeCPUProcessor = InterchangeProcessor->getOptimizedCPUProcessor(BitDepth, BitDepth, OPTIMIZATION_DEFAULT);
					InterchangeCPUProcessor->apply(*ImageDesc);
				}

				return true;
			}
		}
	}
#if !PLATFORM_EXCEPTIONS_DISABLED
	catch (OCIO_NAMESPACE::Exception& Exc)
	{
		UE_LOG(LogOpenColorIOWrapper, Log, TEXT("Failed to transform image. Error message: %s"), StringCast<TCHAR>(Exc.what()).Get());
	}
#endif
#endif // WITH_OCIO

	return false;
}

bool FOpenColorIOCPUProcessorWrapper::TransformImage(const FImageView& SrcImage, const FImageView& DestImage) const
{
#if WITH_OCIO
	TRACE_CPUPROFILER_EVENT_SCOPE(FOpenColorIOCPUProcessorWrapper::TransformImage)
#if !PLATFORM_EXCEPTIONS_DISABLED
	try
#endif
	{
		using namespace OCIO_NAMESPACE;

		if (IsValid())
		{
			TUniquePtr<PackedImageDesc> SrcImageDesc = GetImageDesc(SrcImage);
			TUniquePtr<PackedImageDesc> DestImageDesc = GetImageDesc(DestImage);
			if (SrcImageDesc && DestImageDesc)
			{
				const TUniquePtr<ANSICHAR[]> AnsiWorkingColorSpaceName = OpenColorIOWrapper::MakeAnsiString(OpenColorIOWrapper::GetWorkingColorSpaceName());
				ConstConfigRcPtr	InterchangeConfig = IOpenColorIOWrapperModule::Get().GetWorkingColorSpaceToInterchangeConfig()->Pimpl->Config;
				ConstConfigRcPtr	Config = ParentProcessor.OwnerConfig->Pimpl->Config;

				BitDepth SrcBitDepth = SrcImageDesc->getBitDepth();
				BitDepth DestBitDepth = DestImageDesc->getBitDepth();

				// Conditionally apply a conversion from the working color space to interchange space
				if (ParentProcessor.WorkingColorSpaceTransformType == EOpenColorIOWorkingColorSpaceTransform::Source)
				{
					ConstProcessorRcPtr	InterchangeProcessor = InterchangeConfig->GetProcessorFromConfigs(
						InterchangeConfig,
						AnsiWorkingColorSpaceName.Get(),
						Config,
						Config->getCanonicalName(OpenColorIOWrapper::GetInterchangeName()));

					ConstCPUProcessorRcPtr InterchangeCPUProcessor = InterchangeProcessor->getOptimizedCPUProcessor(SrcBitDepth, SrcBitDepth, OPTIMIZATION_DEFAULT);
					InterchangeCPUProcessor->apply(*SrcImageDesc);
				}


				// Apply the main color transformation
				ConstCPUProcessorRcPtr CPUProcessor = ParentProcessor.Pimpl->Processor->getOptimizedCPUProcessor(SrcBitDepth, DestBitDepth, OPTIMIZATION_DEFAULT);
				CPUProcessor->apply(*SrcImageDesc, *DestImageDesc);

				// Conditionally apply a conversion from the interchange space to the working color space
				if (ParentProcessor.WorkingColorSpaceTransformType == EOpenColorIOWorkingColorSpaceTransform::Destination)
				{
					ConstProcessorRcPtr	InterchangeProcessor = InterchangeConfig->GetProcessorFromConfigs(
						Config,
						Config->getCanonicalName(OpenColorIOWrapper::GetInterchangeName()),
						InterchangeConfig,
						AnsiWorkingColorSpaceName.Get());

					ConstCPUProcessorRcPtr InterchangeCPUProcessor = InterchangeProcessor->getOptimizedCPUProcessor(DestBitDepth, DestBitDepth, OPTIMIZATION_DEFAULT);
					InterchangeCPUProcessor->apply(*DestImageDesc);
				}

				return true;
			}
		}
	}
#if !PLATFORM_EXCEPTIONS_DISABLED
	catch (OCIO_NAMESPACE::Exception& exception)
	{
		UE_LOG(LogOpenColorIOWrapper, Log, TEXT("Failed to transform image. Error message: %s"), StringCast<TCHAR>(exception.what()).Get());
	}
#endif
#endif // WITH_OCIO

	return false;
}



FOpenColorIOGPUProcessorWrapper::FOpenColorIOGPUProcessorWrapper(FOpenColorIOProcessorWrapper InProcessor, FInitializationOptions InShaderParams)
	: ParentProcessor(MoveTemp(InProcessor))
	, GPUPimpl(MakePimpl<FOpenColorIOGPUProcessorPimpl, EPimplPtrMode::DeepCopy>())
{
#if WITH_OCIO
	TRACE_CPUPROFILER_EVENT_SCOPE(FOpenColorIOGPUProcessorWrapper::FOpenColorIOGPUProcessorWrapper)	
#if !PLATFORM_EXCEPTIONS_DISABLED
	try
#endif
	{
		using namespace OCIO_NAMESPACE;

		if (ParentProcessor.IsValid())
		{
			GpuShaderDescRcPtr ShaderDescription = GpuShaderDesc::CreateShaderDesc();
			ShaderDescription->setLanguage(GPU_LANGUAGE_HLSL_DX11);
			ShaderDescription->setFunctionName(StringCast<ANSICHAR>(OpenColorIOWrapper::GetShaderFunctionName()).Get());
			ShaderDescription->setResourcePrefix("Ocio");

			ConstGPUProcessorRcPtr GPUProcessor = nullptr;
			OptimizationFlags OptFlags = FOpenColorIOProcessorPimpl::GetOptimizationFlags();

			if (InShaderParams.bIsLegacy)
			{
				unsigned int EdgeLength = static_cast<unsigned int>(OpenColorIOWrapper::Legacy3dEdgeLength);
				GPUProcessor = ParentProcessor.Pimpl->Processor->getOptimizedLegacyGPUProcessor(OptFlags, EdgeLength);
			}
			else
			{
				GPUProcessor = ParentProcessor.Pimpl->Processor->getOptimizedGPUProcessor(FOpenColorIOProcessorPimpl::GetOptimizationFlags());
			}
			GPUProcessor->extractGpuShaderInfo(ShaderDescription);

			GPUPimpl->Processor = GPUProcessor;
			GPUPimpl->ShaderDescription = ShaderDescription;
		}
	}
#if !PLATFORM_EXCEPTIONS_DISABLED
	catch (OCIO_NAMESPACE::Exception& Exc)
	{
		UE_LOG(LogOpenColorIOWrapper, Log, TEXT("Failed to fetch shader info for color transform. Error message: %s"), StringCast<TCHAR>(Exc.what()).Get());
	}
#endif
#endif // WITH_OCIO
}

bool FOpenColorIOGPUProcessorWrapper::IsValid() const
{
#if WITH_OCIO
	return ParentProcessor.IsValid() && GPUPimpl->Processor != nullptr && GPUPimpl->ShaderDescription != nullptr;
#else
	return false;
#endif // WITH_OCIO
}

bool FOpenColorIOGPUProcessorWrapper::GetShader(FString& OutShaderCode, FString& OutShaderCacheID) const
{
#if WITH_OCIO
	if (IsValid())
	{
		ensureMsgf(GPUPimpl->ShaderDescription->getNumDynamicProperties() == 0, TEXT("We do not currently support dynamic properties."));

		OutShaderCode = StringCast<TCHAR>(GPUPimpl->ShaderDescription->getShaderText()).Get();
		OutShaderCacheID = StringCast<TCHAR>(GPUPimpl->ShaderDescription->getCacheID()).Get();

		return true;
	}
#endif // WITH_OCIO

	return false;
}

uint32 FOpenColorIOGPUProcessorWrapper::GetNum3DTextures() const
{
#if WITH_OCIO
	if (IsValid())
	{
		return GPUPimpl->ShaderDescription->getNum3DTextures();
	}
#endif // WITH_OCIO

	return 0;
}

bool FOpenColorIOGPUProcessorWrapper::Get3DTexture(uint32 InIndex, FName& OutName, uint32& OutEdgeLength, TextureFilter& OutTextureFilter, const float*& OutData) const
{
#if WITH_OCIO
	ensure(InIndex < GetNum3DTextures());

#if !PLATFORM_EXCEPTIONS_DISABLED
	try
#endif
	{
		if (IsValid())
		{
			const ANSICHAR* TextureName = nullptr;
			const ANSICHAR* SamplerName = nullptr;
			OCIO_NAMESPACE::Interpolation Interpolation = OCIO_NAMESPACE::INTERP_TETRAHEDRAL;

			// Read texture information
			GPUPimpl->ShaderDescription->get3DTexture(InIndex, TextureName, SamplerName, OutEdgeLength, Interpolation);

			// Read texture data
			OutData = 0x0;
			GPUPimpl->ShaderDescription->get3DTextureValues(InIndex, OutData);

			OutName = FName(TextureName);

			OutTextureFilter = TF_Bilinear;
			if (Interpolation == OCIO_NAMESPACE::Interpolation::INTERP_NEAREST || Interpolation == OCIO_NAMESPACE::Interpolation::INTERP_TETRAHEDRAL)
			{
				OutTextureFilter = TF_Nearest;
			}

			return TextureName && OutEdgeLength > 0 && OutData;
		}
	}
#if !PLATFORM_EXCEPTIONS_DISABLED
	catch (OCIO_NAMESPACE::Exception& Exc)
	{
		UE_LOG(LogOpenColorIOWrapper, Error, TEXT("Failed to fetch 3d texture(s) info for color transform. Error message: %s"), StringCast<TCHAR>(Exc.what()).Get());
	}
#endif
#endif //WITH_OCIO

	return false;
}
uint32 FOpenColorIOGPUProcessorWrapper::GetNumTextures() const
{
#if WITH_OCIO
	if (IsValid())
	{
		return GPUPimpl->ShaderDescription->getNumTextures();  //noexcept
	}
#endif // WITH_OCIO

	return 0;
}

bool FOpenColorIOGPUProcessorWrapper::GetTexture(uint32 InIndex, FName& OutName, uint32& OutWidth, uint32& OutHeight, TextureFilter& OutTextureFilter, bool& bOutRedChannelOnly, const float*& OutData) const
{
	ensure(InIndex < GetNumTextures());

#if WITH_OCIO
#if !PLATFORM_EXCEPTIONS_DISABLED
	try
#endif
	{
		if (IsValid())
		{
			const ANSICHAR* TextureName = nullptr;
			const ANSICHAR* SamplerName = nullptr;
			OCIO_NAMESPACE::GpuShaderDesc::TextureType Channel = OCIO_NAMESPACE::GpuShaderDesc::TEXTURE_RGB_CHANNEL;
			OCIO_NAMESPACE::Interpolation Interpolation = OCIO_NAMESPACE::Interpolation::INTERP_LINEAR;

			// Read texture information
			GPUPimpl->ShaderDescription->getTexture(InIndex, TextureName, SamplerName, OutWidth, OutHeight, Channel, Interpolation);

			// Read texture data
			OutData = 0x0;
			GPUPimpl->ShaderDescription->getTextureValues(InIndex, OutData);

			OutName = FName(TextureName);
			OutTextureFilter = Interpolation == OCIO_NAMESPACE::Interpolation::INTERP_NEAREST ? TF_Nearest : TF_Bilinear;
			bOutRedChannelOnly = Channel == OCIO_NAMESPACE::GpuShaderCreator::TEXTURE_RED_CHANNEL;

			return TextureName && OutWidth > 0 && OutHeight > 0 && OutData;
		}
	}
#if !PLATFORM_EXCEPTIONS_DISABLED
	catch (OCIO_NAMESPACE::Exception& Exc)
	{
		UE_LOG(LogOpenColorIOWrapper, Error, TEXT("Failed to fetch texture(s) info for color transform. Error message: %s"), StringCast<TCHAR>(Exc.what()).Get());
	}
#endif
#endif // WITH_OCIO

	return false;
}

FString FOpenColorIOGPUProcessorWrapper::GetCacheID() const
{
#if WITH_OCIO
	if (IsValid())
	{
		return StringCast<TCHAR>(GPUPimpl->Processor->getCacheID()).Get();
	}
#endif //WITH_OCIO

	return {};
}

#undef LOCTEXT_NAMESPACE
