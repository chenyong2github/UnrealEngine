// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#ifdef _WINDOWS
	#ifdef UE4BLACKMAGICDESIGN_EXPORTS
		#define UE4BLACKMAGICDESIGN_API __declspec(dllexport)
	#else
		#define UE4BLACKMAGICDESIGN_API __declspec(dllimport)
	#endif
#else // _WINDOWS
	#ifdef UE4BLACKMAGICDESIGN_EXPORTS
		#define UE4BLACKMAGICDESIGN_API __attribute__ ((visibility ("default")))
	#else
		#define UE4BLACKMAGICDESIGN_API
	#endif
#endif // _WINDOWS

#include "BlackmagicReferencePtr.h"

namespace BlackmagicDesign
{
	using LoggingCallbackPtr = void(*)(const TCHAR* Format, ...);
	using FBlackmagicVideoFormat = int32_t; //BMDDisplayMode

	enum class EPixelFormat
	{
		pf_8Bits,
		pf_10Bits
	};

	enum class EPixelColor
	{
		YCbCr,
		RGB,
	};

	enum class EFieldDominance
	{
		Progressive,
		Interlaced,
		ProgressiveSegmentedFrame,
	};

	namespace Private
	{
		class DeviceScanner;
		class VideoFormatsScanner;
	}

	/* FUniqueIdentifier definition
	*****************************************************************************/
	struct UE4BLACKMAGICDESIGN_API FUniqueIdentifier
	{
		FUniqueIdentifier();
		explicit FUniqueIdentifier(int32_t InIdentifier);
		bool IsValid() const;
		bool operator== (const FUniqueIdentifier& rhs) const;

	private:
		int32_t Identifier;
	};

	/* FTimecode definition
	 * limited to 30fps
	*****************************************************************************/
	struct UE4BLACKMAGICDESIGN_API FTimecode
	{
		FTimecode();
		bool operator== (const FTimecode& Other) const;

		uint32_t Hours;
		uint32_t Minutes;
		uint32_t Seconds;
		uint32_t Frames;
		bool bIsDropFrame;
	};

	enum struct ETimecodeFormat
	{
		TCF_None,
		TCF_LTC,
		TCF_VITC1,
	};

	enum struct ELinkConfiguration
	{
		SingleLink,
		DualLink,
		QuadLinkTSI,
		QuadLinkSqr,
	};
 
	/* FFormatInfo definition
	 * Information about a given frame desc
	*****************************************************************************/
	struct UE4BLACKMAGICDESIGN_API FFormatInfo
	{
		/** Framerate */
		uint32_t FrameRateNumerator;
		uint32_t FrameRateDenominator;

		/** Image Width & Weight in texels */
		uint32_t Width;
		uint32_t Height;

		EFieldDominance FieldDominance;
		FBlackmagicVideoFormat DisplayMode; // Unique identifier that represent all that combination for the device

		bool operator==(FFormatInfo& Other) const;

	};

	/* FChannelInfo definition
	*****************************************************************************/
	struct UE4BLACKMAGICDESIGN_API FChannelInfo
	{
		int32_t DeviceIndex;

		bool operator==(FChannelInfo& Other) const;
	};

	/* FInputChannelOptions definition
	*****************************************************************************/
	struct UE4BLACKMAGICDESIGN_API FInputChannelOptions
	{
		FInputChannelOptions();

		FFormatInfo FormatInfo;
		int32_t CallbackPriority;

		bool bReadVideo;
		EPixelFormat PixelFormat;
		
		ETimecodeFormat TimecodeFormat;

		bool bReadAudio;
		int32_t NumberOfAudioChannel;

		bool bUseTheDedicatedLTCInput;
	};

	/* FOutputChannelOptions definition
	*****************************************************************************/
	struct UE4BLACKMAGICDESIGN_API FOutputChannelOptions
	{
		FOutputChannelOptions();

		FFormatInfo FormatInfo;
		int32_t CallbackPriority;
		EPixelFormat PixelFormat;

		uint32_t NumberOfBuffers;

		ETimecodeFormat TimecodeFormat;
		ELinkConfiguration LinkConfiguration;

		bool bOutputKey;
		bool bOutputVideo;
		bool bOutputInterlacedFieldsTimecodeNeedToMatch;
		bool bLogDropFrames;
	};

	/* IInputEventCallback definition
	*****************************************************************************/
	struct UE4BLACKMAGICDESIGN_API IInputEventCallback
	{
		struct UE4BLACKMAGICDESIGN_API FFrameReceivedInfo
		{
			FFrameReceivedInfo();

			bool bHasInputSource;

			int64_t FrameNumber;

			// Timecode
			bool bHaveTimecode;
			FTimecode Timecode;

			// Video
			void* VideoBuffer;
			int32_t VideoWidth;
			int32_t VideoHeight;
			int32_t VideoPitch;
			EPixelFormat PixelFormat;
			EFieldDominance FieldDominance;

			// Audio
			void* AudioBuffer;
			int32_t AudioBufferSize;
			int32_t NumberOfAudioChannel;
			int32_t AudioRate;
		};

		virtual ~IInputEventCallback();

		virtual void AddRef() = 0;
		virtual void Release() = 0;

		virtual void OnInitializationCompleted(bool bSuccess) = 0;
		virtual void OnShutdownCompleted() = 0;

		virtual void OnFrameReceived(const FFrameReceivedInfo&) = 0;
		virtual void OnFrameFormatChanged(const FFormatInfo& NewFormat) = 0;
		virtual void OnInterlacedOddFieldEvent() = 0;
	};

	/* IOutputEventCallback definition
	*****************************************************************************/
	struct UE4BLACKMAGICDESIGN_API IOutputEventCallback
	{
		struct UE4BLACKMAGICDESIGN_API FFrameSentInfo
		{
			FFrameSentInfo();

			uint32_t FramesLost;
			uint32_t FramesDropped;
		};

		virtual ~IOutputEventCallback();

		virtual void AddRef() = 0;
		virtual void Release() = 0;

		virtual void OnInitializationCompleted(bool bSuccess) = 0;
		virtual void OnShutdownCompleted() = 0;

		virtual void OnOutputFrameCopied(const FFrameSentInfo& InFrameInfo) = 0;
		virtual void OnPlaybackStopped() = 0;
		virtual void OnInterlacedOddFieldEvent() = 0;
	};

	struct UE4BLACKMAGICDESIGN_API FFrameDescriptor
	{
		uint8_t* VideoBuffer;
		int32_t VideoWidth;
		int32_t VideoHeight;

		FTimecode Timecode;
		uint32_t FrameIdentifier;
	};

	/* BlackmagicDeviceScanner definition
	*****************************************************************************/
	class UE4BLACKMAGICDESIGN_API BlackmagicDeviceScanner
	{
	public:
		const static int32_t FormatedTextSize = 64;
		using FormatedTextType = TCHAR[FormatedTextSize];

		struct UE4BLACKMAGICDESIGN_API DeviceInfo
		{
			bool bIsSupported;
			bool bCanDoCapture;
			bool bCanDoPlayback;
			bool bCanDoFullDuplex;
			bool bCanDoDualLink;
			bool bCanDoQuadLink;
			bool bCanDoQuadSquareLink;
			bool bHasGenlockReferenceInput;
			bool bHasLTCTimecodeInput;
			bool bCanAutoDetectInputFormat;
			bool bSupportInternalKeying;
			bool bSupportExternalKeying;

			uint32_t NumberOfSubDevices;
			uint32_t DevicePersistentId;
			uint32_t ProfileId;
			uint32_t DeviceGroupId;
			uint32_t SubDeviceIndex;
		};

		BlackmagicDeviceScanner();
		~BlackmagicDeviceScanner();

		BlackmagicDeviceScanner(const BlackmagicDeviceScanner&) = delete;
		BlackmagicDeviceScanner& operator=(const BlackmagicDeviceScanner&) = delete;

		int32_t GetNumDevices() const;
		bool GetDeviceTextId(int32_t InDeviceIndex, FormatedTextType& OutTextId) const;
		bool GetDeviceInfo(int32_t InDeviceIndex, DeviceInfo& OutDeviceInfo) const;

	private:
		Private::DeviceScanner* Scanner;
	};

	/* BlackmagicVideoFormats definition
	*****************************************************************************/
	struct UE4BLACKMAGICDESIGN_API BlackmagicVideoFormats
	{
		struct UE4BLACKMAGICDESIGN_API VideoFormatDescriptor
		{
			VideoFormatDescriptor();

			FBlackmagicVideoFormat VideoFormatIndex;
			uint32_t FrameRateNumerator;
			uint32_t FrameRateDenominator;
			uint32_t ResolutionWidth;
			uint32_t ResolutionHeight;
			bool bIsProgressiveStandard;
			bool bIsInterlacedStandard;
			bool bIsPsfStandard;
			bool bIsSD;
			bool bIsHD;
			bool bIs2K;
			bool bIs4K;
			bool bIs8K;

			bool bIsValid;
		};

		BlackmagicVideoFormats(int32_t InDeviceId, bool bForOutput);
		~BlackmagicVideoFormats();

		BlackmagicVideoFormats(const BlackmagicVideoFormats&) = delete;
		BlackmagicVideoFormats& operator=(const BlackmagicVideoFormats&) = delete;

		int32_t GetNumSupportedFormat() const;
		VideoFormatDescriptor GetSupportedFormat(int32_t InIndex) const;

	private:
		Private::VideoFormatsScanner* Formats;
	};

	 /* Configure Logging
	 *****************************************************************************/
	UE4BLACKMAGICDESIGN_API void SetLoggingCallbacks(LoggingCallbackPtr LogInfoFunc, LoggingCallbackPtr LogWarningFunc, LoggingCallbackPtr LogErrorFunc);

	 /* Initialization
	 *****************************************************************************/
	UE4BLACKMAGICDESIGN_API bool ApiInitialization();
	UE4BLACKMAGICDESIGN_API void ApiUninitialization();

	 /* Register/Unregister
	 *****************************************************************************/
	UE4BLACKMAGICDESIGN_API FUniqueIdentifier RegisterCallbackForChannel(const FChannelInfo& InChannelInfo, const FInputChannelOptions& InChannelOptions, ReferencePtr<IInputEventCallback> InCallback);
	UE4BLACKMAGICDESIGN_API void UnregisterCallbackForChannel(const FChannelInfo& InChannelInfo, FUniqueIdentifier InIdentifier);
	
	UE4BLACKMAGICDESIGN_API FUniqueIdentifier RegisterOutputChannel(const FChannelInfo& InChannelInfo, const FOutputChannelOptions& InChannelOptions, ReferencePtr<IOutputEventCallback> InCallback);
	UE4BLACKMAGICDESIGN_API void UnregisterOutputChannel(const FChannelInfo& InChannelInfo, FUniqueIdentifier InIdentifier, bool bCallCompleted);
	UE4BLACKMAGICDESIGN_API bool SendVideoFrameData(const FChannelInfo& InChannelInfo, const FFrameDescriptor& InFrame);
};
