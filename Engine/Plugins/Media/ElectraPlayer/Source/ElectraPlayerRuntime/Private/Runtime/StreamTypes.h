// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"
#include "PlayerTime.h"
#include "ParameterDictionary.h"

namespace Electra
{

	enum class EStreamType
	{
		Unsupported,
		Video,
		Audio,
		Subtitle,
	};

	static inline const TCHAR* GetStreamTypeName(EStreamType StreamType)
	{
		switch (StreamType)
		{
			case EStreamType::Video:
				return TEXT("Video");
			case EStreamType::Audio:
				return TEXT("Audio");
			case EStreamType::Subtitle:
				return TEXT("Subtitle");
			case EStreamType::Unsupported:
				return TEXT("Unsupported");
		}
		return TEXT("n/a");
	}



	class FStreamCodecInformation
	{
	public:
		FStreamCodecInformation()
		{
			Clear();
		}
		enum class ECodec
		{
			// --- Unknown ---
			Unknown = 0,
			// --- Video ---
			H264 = 1,
			H265,
			// --- Audio ---
			AAC = 100,
			//
			LastEntryDummy
		};

		EStreamType GetStreamType() const
		{
			return StreamType;
		}

		void SetStreamType(EStreamType InStreamType)
		{
			StreamType = InStreamType;
		}

		ECodec GetCodec() const
		{
			return Codec;
		}

		void SetCodec(ECodec InCodec)
		{
			Codec = InCodec;
		}

		bool IsVideoCodec() const
		{
			switch (GetCodec())
			{
				case ECodec::H264:
					return true;
				case ECodec::H265:
					return true;
				default:
					return false;
			}
		}

		bool IsAudioCodec() const
		{
			switch (GetCodec())
			{
				case ECodec::AAC:
					return true;
				default:
					return false;
			}
		}

		const FString& GetCodecSpecifierRFC6381() const
		{
			return CodecSpecifier;
		}

		void SetCodecSpecifierRFC6381(const FString& InCodecSpecifier)
		{
			CodecSpecifier = InCodecSpecifier;
		}

		struct FResolution
		{
			FResolution(int32 w = 0, int32 h = 0)
				: Width(w)
				, Height(h)
			{
			}
			void Clear()
			{
				Width = Height = 0;
			}
			bool IsSet() const
			{
				return Width != 0 || Height != 0;
			}
			bool ExceedsLimit(int32 LimitWidth, int32 LimitHeight) const
			{
				if (LimitWidth && Width > LimitWidth)
				{
					return true;
				}
				if (LimitHeight && Height > LimitHeight)
				{
					return true;
				}
				return false;
			}
			bool ExceedsLimit(const FResolution& Limit) const
			{
				return ExceedsLimit(Limit.Width, Limit.Height);
			}

			bool operator == (const FResolution& rhs) const
			{
				return Width == rhs.Width && Height == rhs.Height;
			}

			bool operator != (const FResolution& rhs) const
			{
				return !(*this == rhs);
			}

			int32	Width;
			int32	Height;
		};

		const FResolution& GetResolution() const
		{
			return Resolution;
		}

		void SetResolution(const FResolution& InResolution)
		{
			Resolution = InResolution;
		}


		struct FAspectRatio
		{
			FAspectRatio(int32 w = 0, int32 h = 0)
				: Width(w)
				, Height(h)
			{
			}
			void Clear()
			{
				Width = Height = 0;
			}
			bool IsSet() const
			{
				return Width != 0 || Height != 0;
			}

			bool operator == (const FAspectRatio& rhs) const
			{
				return Width == rhs.Width && Height == rhs.Height;
			}

			bool operator != (const FAspectRatio& rhs) const
			{
				return !(*this == rhs);
			}

			int32	Width;
			int32	Height;
		};

		struct FCrop
		{
			FCrop(int32 InLeft = 0, int32 InTop = 0, int32 InRight = 0, int32 InBottom = 0)
				: Left(InLeft)
				, Top(InTop)
				, Right(InRight)
				, Bottom(InBottom)
			{
			}

			void Clear()
			{
				Left = Top = Right = Bottom = 0;
			}

			bool operator == (const FCrop& rhs) const
			{
				return Left == rhs.Left && Top == rhs.Top && Right == rhs.Right && Bottom == rhs.Bottom;
			}

			bool operator != (const FCrop& rhs) const
			{
				return !(*this == rhs);
			}

			int32	Left;
			int32	Top;
			int32	Right;
			int32	Bottom;
		};

		const FCrop& GetCrop() const
		{
			return Crop;
		}

		void SetCrop(const FCrop& InCrop)
		{
			Crop = InCrop;
		}

		const FAspectRatio& GetAspectRatio() const
		{
			return AspectRatio;
		}

		void SetAspectRatio(const FAspectRatio& InAspectRatio)
		{
			AspectRatio = InAspectRatio;
		}

		const FTimeFraction& GetFrameRate() const
		{
			return FrameRate;
		}

		void SetFrameRate(const FTimeFraction& InFrameRate)
		{
			FrameRate = InFrameRate;
		}

		void SetProfile(int32 InProfile)
		{
			ProfileLevel.Profile = InProfile;
		}

		int32 GetProfile() const
		{
			return ProfileLevel.Profile;
		}

		void SetProfileLevel(int32 InLevel)
		{
			ProfileLevel.Level = InLevel;
		}

		int32 GetProfileLevel() const
		{
			return ProfileLevel.Level;
		}

		void SetProfileConstraints(int32 InConstraints)
		{
			ProfileLevel.Constraints = InConstraints;
		}

		int32 GetProfileConstraints() const
		{
			return ProfileLevel.Constraints;
		}

		void SetProfileTier(int32 InTier)
		{
			ProfileLevel.Tier = InTier;
		}

		int32 GetProfileTier() const
		{
			return ProfileLevel.Tier;
		}

		void SetSamplingRate(int32 InSamplingRate)
		{
			SampleRate = InSamplingRate;
		}

		int32 GetSamplingRate() const
		{
			return SampleRate;
		}

		void SetNumberOfChannels(int32 InNumberOfChannels)
		{
			NumChannels = InNumberOfChannels;
		}

		int32 GetNumberOfChannels() const
		{
			return NumChannels;
		}

		void SetChannelConfiguration(uint32 InChannelConfiguration)
		{
			ChannelConfiguration = InChannelConfiguration;
		}

		uint32 GetChannelConfiguration() const
		{
			return ChannelConfiguration;
		}

		void SetAudioDecodingComplexity(int InAudioDecodingComplexity)
		{
			AudioDecodingComplexity = InAudioDecodingComplexity;
		}

		int32 GetAudioDecodingComplexity() const
		{
			return AudioDecodingComplexity;
		}

		void SetAudioAccessibility(int32 InAudioAccessibility)
		{
			AudioAccessibility = InAudioAccessibility;
		}

		int32 GetAudioAccessibility() const
		{
			return AudioAccessibility;
		}

		void SetNumberOfAudioObjects(int32 InNumberOfAudioObjects)
		{
			NumberOfAudioObjects = InNumberOfAudioObjects;
		}

		int32 GetNumberOfAudioObjects() const
		{
			return NumberOfAudioObjects;
		}

		void SetStreamLanguageCode(const FString& InStreamLanguageCode)
		{
			StreamLanguageCode = InStreamLanguageCode;
		}

		const FString& GetStreamLanguageCode() const
		{
			return StreamLanguageCode;
		}

		FParamDict& GetExtras()
		{
			return Extras;
		}

		const FParamDict& GetExtras() const
		{
			return Extras;
		}

		void Clear()
		{
			StreamType = EStreamType::Unsupported;
			CodecSpecifier.Empty();
			Codec = ECodec::Unknown;
			Resolution.Clear();
			Crop.Clear();
			AspectRatio.Clear();
			FrameRate = FTimeFraction::GetInvalid();
			ProfileLevel.Clear();
			StreamLanguageCode.Empty();
			SampleRate = 0;
			NumChannels = 0;
			ChannelConfiguration = 0;
			AudioDecodingComplexity = 0;
			AudioAccessibility = 0;
			NumberOfAudioObjects = 0;
			Extras.Clear();
		}

		bool IsDifferentFromOtherVideo(const FStreamCodecInformation& Other) const
		{
			// We do not compare frame rate here as we are interested in values that may require a decoder reconfiguration.
			return Codec != Other.Codec || Resolution != Other.Resolution || ProfileLevel != Other.ProfileLevel || AspectRatio != Other.AspectRatio;
		}
	private:
		struct FProfileLevel
		{
			FProfileLevel()
			{
				Clear();
			}
			void Clear()
			{
				Profile = 0;
				Level = 0;
				Constraints = 0;
				Tier = 0;
			}
			int32		Profile;
			int32		Level;
			int32		Constraints;
			int32		Tier;

			bool operator == (const FProfileLevel& rhs) const
			{
				return Profile == rhs.Profile && Level == rhs.Level && Constraints == rhs.Constraints && Tier == rhs.Tier;
			}

			bool operator != (const FProfileLevel& rhs) const
			{
				return !(*this == rhs);
			}
		};

		EStreamType		StreamType;
		FString			CodecSpecifier;				//!< Codec specifier as per RFC 6381
		ECodec			Codec;
		FResolution		Resolution;					//!< Resolution, if this is a video stream
		FCrop			Crop;						//!< Cropping, if this is a video stream
		FAspectRatio	AspectRatio;				//!< Aspect ratio, if this is a video stream
		FTimeFraction	FrameRate;					//!< Frame rate, if this is a video stream
		FProfileLevel	ProfileLevel;
		FString			StreamLanguageCode;			//!< Language code from the stream itself, if present.
		int32			SampleRate;					//!< Decoded sample rate, if this is an audio stream.
		int32			NumChannels;				//!< Number of decoded channels, if this is an audio stream.
		uint32			ChannelConfiguration;		//!< Format specific audio channel configuration
		int32			AudioDecodingComplexity;	//!< Format specific audio decoding complexity
		int32			AudioAccessibility;			//!< Format specific audio accessibility
		int32			NumberOfAudioObjects;		//!< Format specific number of audio objects
		FParamDict		Extras;						//!< Additional details/properties, depending on playlist.
	};




	/**
	 * Metadata of an elementary stream as specified by the playlist/manifest.
	 * This is only as correct as the information in the manifest. If any part is not listed there
	 * the information in here will be incomplete.
	 */
	struct FStreamMetadata
	{
		FStreamCodecInformation		CodecInformation;					//!< Stream codec information
		FString						PlaylistID;							//!< URL or some other ID of the playlist (FIXME: this is mostly for HLS)
		int32						Bandwidth;							//!< Bandwidth required for this stream in bits per second
		uint32						StreamUniqueID;						//!< Uniquely identifies this stream
		FString						LanguageCode;
		// TODO: additional information
	};




	/**
	 * Temporary!
	 * Used to convey the user's stream preferences (ie. language and other things) to the player.
	 */
	struct FStreamPreferences
	{
		FParamDict	Unused;							//!< This is only here as a placeholder for now. Nothing writes to or reads from this.
	};



	struct FPlayerLoopState
	{
		FPlayerLoopState()
		{
			Reset();
		}
		void Reset()
		{
			LoopBasetime.SetToZero();
			LoopCount = 0;
			bLoopEnabled = false;
		}
		FTimeValue	LoopBasetime;				//!< Base time added to the play position to have it monotonously increasing. Subtract this from the current play position to get the local time into the media.
		int32		LoopCount;					//!< Number of times playback jumped back to loop. 0 on first playthrough, 1 on first loop, etc.
		bool		bLoopEnabled;
	};



} // namespace Electra


