// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXShaders.cpp: AGX RHI shader implementation.
=============================================================================*/

#include "AGXRHIPrivate.h"

#include "Shaders/Debugging/AGXShaderDebugCache.h"
#include "Shaders/AGXCompiledShaderKey.h"
#include "Shaders/AGXCompiledShaderCache.h"
#include "Shaders/AGXShaderLibrary.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "MetalShaderResources.h"
#include "AGXResources.h"
#include "AGXProfiler.h"
#include "Serialization/MemoryReader.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/Compression.h"
#include "Misc/MessageDialog.h"

#define SHADERCOMPILERCOMMON_API
#	include "Developer/ShaderCompilerCommon/Public/ShaderCompilerCommon.h"
#undef SHADERCOMPILERCOMMON_API


NSString* AGXDecodeMetalSourceCode(uint32 CodeSize, TArray<uint8> const& CompressedSource)
{
	NSString* GlslCodeNSString = nil;
	if (CodeSize && CompressedSource.Num())
	{
		TArray<ANSICHAR> UncompressedCode;
		UncompressedCode.AddZeroed(CodeSize+1);
		bool bSucceed = FCompression::UncompressMemory(NAME_Zlib, UncompressedCode.GetData(), CodeSize, CompressedSource.GetData(), CompressedSource.Num());
		if (bSucceed)
		{
			GlslCodeNSString = [[NSString stringWithUTF8String:UncompressedCode.GetData()] retain];
		}
	}
	return GlslCodeNSString;
}

mtlpp::LanguageVersion AGXValidateVersion(uint32 Version)
{
    mtlpp::LanguageVersion Result = mtlpp::LanguageVersion::Version2_2;
#if PLATFORM_MAC
    Result = mtlpp::LanguageVersion::Version2_2;
    switch(Version)
    {
        case 7:
            Result = mtlpp::LanguageVersion::Version2_4;
            break;
        case 6:
            Result = mtlpp::LanguageVersion::Version2_3;
            break;
        case 5:
            Result = mtlpp::LanguageVersion::Version2_2;
            break;
        case 0:
            Version = 5;
            Result = mtlpp::LanguageVersion::Version2_2; // minimum version as of UE5.0
            break;
        default:
            //EMacMetalShaderStandard::MacMetalSLStandard_Minimum is currently 2.2
            UE_LOG(LogTemp, Warning, TEXT("The Metal version currently set is not supported anymore. Set it in the Project Settings. Defaulting to the minimum version."));
            Version = 5;
            Result = mtlpp::LanguageVersion::Version2_2;
            break;
    }
#else
    Result = mtlpp::LanguageVersion::Version2_3;
    switch(Version)
    {
        case 7:
            Result = mtlpp::LanguageVersion::Version2_4;
            break;
        case 6:
            Result = mtlpp::LanguageVersion::Version2_3;
            break;
        case 0:
            Version = 6;
            Result = mtlpp::LanguageVersion::Version2_3; // minimum version as of UE5.0
            break;
        default:
            //EMacMetalShaderStandard::MacMetalSLStandard_Minimum and EIOSMetalShaderStandard::IOSMetalSLStandard_Minimum is currently 2.3
            UE_LOG(LogTemp, Warning, TEXT("The Metal version currently set is not supported anymore. Set it in the Project Settings. Defaulting to the minimum version."));
            Version = 6;
            Result = mtlpp::LanguageVersion::Version2_3;
            break;
    }
#endif
    return Result;
}
