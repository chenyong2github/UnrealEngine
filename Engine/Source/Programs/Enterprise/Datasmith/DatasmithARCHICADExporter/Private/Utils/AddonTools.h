// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if defined(WIN32)
	#pragma warning(push)
	#pragma warning(disable : 4800)
	#pragma warning(disable : 4505)
#endif

#include <string>
#include <cstdarg>

#if defined(WIN32)
	#pragma warning(pop)
#endif

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "Md5.hpp"

#include "LocalizeTools.h"
#include "DebugTools.h"

BEGIN_NAMESPACE_UE_AC

// Print in a string using the format and arguments list
utf8_string VStringFormat(const utf8_t* InFmt, std::va_list InArgumentsList) __printflike(1, 0);

// Print in a string using the format and arguments
utf8_string Utf8StringFormat(const utf8_t* InFmt, ...) __printflike(1, 2);

// Zero the content of structure *x
#define Zap(x) BNZeroMemory(x, sizeof(*x));

// Short way to get Utf8 char pointer of a GS::UniString
#define ToUtf8() ToCStr(0, GS::MaxUSize, CC_UTF8).Get()

// Short way to get TCHAR pointer of a GS::UniString
#define GSStringToUE(InGSString) UTF16_TO_TCHAR(InGSString.ToUStr().Get())

// Convert Unreal string to Archicad string
inline GS::UniString FStringToGSString(const FString& InString)
{
	return TCHAR_TO_UTF16(*InString);
}

// Convert Unreal TCHAR string pointer to Archicad string
inline GS::UniString TCHARToGSString(const TCHAR* InString)
{
	return TCHAR_TO_UTF16(InString);
}

// Convert utf8 string pointer to Unreal string
inline FString Utf8ToFString(const utf8_t* InUtf8String)
{
	return FString(UTF8_TO_TCHAR(InUtf8String));
}

// Convert utf8 string to Unreal string
inline FString Utf8ToFString(const utf8_string& InUtf8String)
{
	return FString(UTF8_TO_TCHAR(InUtf8String.c_str()));
}

// Convert Unreal string to utf8 string
inline utf8_string FString2Utf8(const FString& InUEString)
{
	return TCHAR_TO_UTF8(*InUEString);
}

// return true if string is empty
inline bool IsStringEmpty(const TCHAR* InUEString)
{
	return InUEString == nullptr || *InUEString == 0;
}

// Convert an Archicad fingerprint to an API_Guid
inline const API_Guid& Fingerprint2API_Guid(const MD5::FingerPrint& inFP)
{
	return *(const API_Guid*)&inFP;
}

// Compute Guid from MD5 of the value
template < class T > inline API_Guid GuidFromMD5(const T& inV)
{
	MD5::Generator MD5Generator;
	MD5Generator.Update(&inV, sizeof(T));
	MD5::FingerPrint FingerPrint;
	MD5Generator.Finish(FingerPrint);
	return Fingerprint2API_Guid(FingerPrint);
}

// Compute Guid of the string
API_Guid String2API_Guid(const GS::UniString& InString);

// Combine 2 guid in one
API_Guid CombineGuid(const API_Guid& InGuid1, const API_Guid& InGuid2);

// Class for mapping by name
struct FCompareName
{
	bool operator()(const TCHAR* InName1, const TCHAR* InName2) const { return FCString::Strcmp(InName1, InName2) < 0; }
};

// Convert StandardRGB component to LinearRGB component
inline float StandardRGBToLinear(double InRgbComponent)
{
	return float(InRgbComponent > 0.04045 ? pow(InRgbComponent * (1.0 / 1.055) + 0.0521327, 2.4)
										  : InRgbComponent * (1.0 / 12.92));
}

// Convert Archicad RGB color to UE Linear color
inline FLinearColor ACRGBColorToUELinearColor(const ModelerAPI::Color& InColor)
{
	return FLinearColor(StandardRGBToLinear(InColor.red), StandardRGBToLinear(InColor.green),
						StandardRGBToLinear(InColor.blue));
}

// Stack class to auto dispose memo handles
class FAutoMemo
{
	API_ElementMemo* Memo;

  public:
	FAutoMemo(API_ElementMemo* InMemoToDispose)
		: Memo(InMemoToDispose)
	{
	}

	~FAutoMemo()
	{
		if (Memo)
		{
			ACAPI_DisposeElemMemoHdls(Memo);
		}
	}
};

// Stack class to auto dispose handles
class FAutoHandle
{
	GSHandle Handle;

  public:
	FAutoHandle(GSHandle InHandleToDispose)
		: Handle(InHandleToDispose)
	{
	}

	~FAutoHandle()
	{
		if (Handle)
		{
			BMKillHandle(&Handle);
		}
	}

	// Take ownership of this handle
	GSHandle Take()
	{
		GSHandle TmpHandle = Handle;
		Handle = nullptr;
		return TmpHandle;
	}
};

// kStrListENames multi strings
enum ENames
{
	kName_Invalid,
	kName_Company,
	kName_Floor,
	kName_Layer,
	kName_Group,
	kName_LayerDeleted,
	kName_LayerError,
	kName_ElementType,
	kName_InvalidGroupId,
	kName_UndefinedValueType,
	kName_InvalidVariant,
	kName_UndefinedCollectionType,
	kName_InvalidCollectionType,
	kName_InvalidPrimitiveType,
	kName_IFCAttributes,
	kName_IFC_,
	kName_Camera,
	kName_Textures,
	kName__Assets,
	kName_TextureExtension,
	kName_TextureMime,
	kName_SyncOptions,
	kName_ExportOptions,
	kName_Unknown,
	kName_ShowPalette,
	kName_HidePalette,
	kName_OkButtonLabel,
	kName_CancelButtonLabel,
	kName_Undefined,
	kName_NBNames
};
const utf8_t*		 GetStdName(ENames InStrIndex);
const GS::UniString& GetGSName(ENames InStrIndex);

// Return the company directory
IO::Location GetCompanyDataDirectory();

// Return the data directory of the addon
const GS::UniString& GetAddonDataDirectory();

// Return the addon version string
GS::UniString GetAddonVersionsStr();

// Tool to get the name of layer
GS::UniString GetLayerName(API_AttributeIndex InLayer);

// Return current display unit name
const utf8_t* GetCurrentUnitDisplayName();

// Return current time as string
utf8_string GetCurrentLocalDateTime();

// Return true if 3d window is the current
bool Is3DCurrenWindow();

END_NAMESPACE_UE_AC
