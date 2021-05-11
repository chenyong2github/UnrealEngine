// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerCore.h"
#include "StreamTypes.h"
#include "Utilities/StringHelpers.h"

namespace Electra
{

	FString FStreamCodecInformation::GetMimeType() const
	{
		switch(GetCodec())
		{
			case ECodec::H264:
				return FString(TEXT("video/mp4"));
			case ECodec::H265:
				return FString(TEXT("video/mp4"));
			case ECodec::AAC:
				return FString(TEXT("audio/mp4"));
			case ECodec::EAC3:
				return FString(TEXT("audio/mp4"));
			case ECodec::WebVTT:
				return FString(TEXT("application/mp4"));
			default:
				return FString(TEXT("application/octet-stream"));
		}
	}

	FString FStreamCodecInformation::GetMimeTypeWithCodec() const
	{
		return GetMimeType() + FString::Printf(TEXT("; codecs=\"%s\""), *GetCodecSpecifierRFC6381());
	}

	FString FStreamCodecInformation::GetMimeTypeWithCodecAndFeatures() const
	{
		if (GetStreamType() == EStreamType::Video && GetResolution().Width && GetResolution().Height)
		{
			return GetMimeTypeWithCodec() + FString::Printf(TEXT("; resolution=%dx%d"), GetResolution().Width, GetResolution().Height);
		}
		return GetMimeTypeWithCodec();
	}


	bool FStreamCodecInformation::ParseFromRFC6381(const FString& CodecOTI)
	{
		if (CodecOTI.StartsWith("avc"))
		{
			// avc1 and avc3 (inband SPS/PPS) are recognized.
			StreamType = EStreamType::Video;
			CodecSpecifier = CodecOTI;
			Codec = ECodec::H264;
			if (CodecOTI.Len() > 3)
			{
				// avc 1 or 3 only.
				if (CodecOTI[3] != TCHAR('1') && CodecOTI[3] != TCHAR('3'))
				{
					return false;
				}
				// Profile and level follow?
				if (CodecOTI.Len() > 5 && CodecOTI[4] == TCHAR('.'))
				{
					FString Temp;
					int32 TempValue;
					// We recognize the expected format avcC.xxyyzz and for legacy reasons also avcC.xx.zz
					if (CodecOTI.Len() == 11)
					{
						Temp = CodecOTI.Mid(5, 2);
						LexFromStringHex(TempValue, *Temp);
						SetProfile(TempValue);
						Temp = CodecOTI.Mid(7, 2);
						LexFromStringHex(TempValue, *Temp);
						SetProfileConstraints(TempValue);
						Temp = CodecOTI.Mid(9, 2);
						LexFromStringHex(TempValue, *Temp);
						SetProfileLevel(TempValue);
					}
					else if (CodecOTI.Len() == 10 && CodecOTI[7] == TCHAR('.'))
					{
						Temp = CodecOTI.Mid(5, 2);
						LexFromStringHex(TempValue, *Temp);
						SetProfile(TempValue);
						Temp = CodecOTI.Mid(8, 2);
						LexFromStringHex(TempValue, *Temp);
						SetProfileLevel(TempValue);
						// Change the string to the expected format.
						SetCodecSpecifierRFC6381(FString::Printf(TEXT("avc%c.%02x00%02x"), CodecOTI[3], GetProfile(), GetProfileLevel()));
					}
					else
					{
						return false;
					}
				}
			}
			return true;
		}
		else if (CodecOTI.StartsWith("hvc") || CodecOTI.StartsWith("hev"))
		{
			// hvc1 and hev1 (inband VPS/SPS/PPS) are recognized.
			StreamType = EStreamType::Video;
			CodecSpecifier = CodecOTI;
			Codec = ECodec::H265;
			// Presently not supported.
			return false;
		}
		else if (CodecOTI.StartsWith("mp4a"))
		{
			StreamType = EStreamType::Audio;
			CodecSpecifier = CodecOTI;
			Codec = ECodec::AAC;
			// Object and profile follow?
			if (CodecOTI.Len() > 6 && CodecOTI[4] == TCHAR('.'))
			{
				// mp4a.40.d is recognized.
				FString OT, Profile;
				int32 DotPos = CodecOTI.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromStart, 5);
				OT = CodecOTI.Mid(5, DotPos != INDEX_NONE ? DotPos - 5 : DotPos);
				Profile = CodecOTI.Mid(DotPos != INDEX_NONE ? DotPos + 1 : DotPos);
				if (!OT.Equals(TEXT("40")))
				{
					return false;
				}
				int32 ProfileValue = 0;
				LexFromString(ProfileValue, *Profile);
				SetProfile(ProfileValue);
				// AAC-LC, AAC-HE, AAC-HEv2
				if (!(ProfileValue == 2 || ProfileValue == 5 || ProfileValue == 29))
				{
					return false;
				}
			}
			return true;
		}
		else if (CodecOTI.StartsWith("ec-3") || CodecOTI.StartsWith("ec+3") || CodecOTI.StartsWith("ec3") || CodecOTI.StartsWith("eac3"))
		{
			StreamType = EStreamType::Audio;
			CodecSpecifier = CodecOTI;
			Codec = ECodec::EAC3;
			// Presently not supported.
			return false;
		}
		else if (CodecOTI.Equals(TEXT("wvtt")))
		{
			StreamType = EStreamType::Subtitle;
			CodecSpecifier = CodecOTI;
			Codec = ECodec::WebVTT;
			// Presently not supported.
			return false;
		}
		else
		{
			StreamType = EStreamType::Unsupported;
			CodecSpecifier = CodecOTI;
			Codec = ECodec::Unknown;
			return false;
		}
	}
} // namespace Electra
