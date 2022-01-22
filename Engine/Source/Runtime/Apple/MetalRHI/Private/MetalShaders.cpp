// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalShaders.cpp: Metal shader RHI implementation.
=============================================================================*/

#include "MetalRHIPrivate.h"

#include "Shaders/Debugging/MetalShaderDebugCache.h"
#include "Shaders/MetalCompiledShaderKey.h"
#include "Shaders/MetalCompiledShaderCache.h"
#include "Shaders/MetalShaderLibrary.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "MetalShaderResources.h"
#include "MetalResources.h"
#include "MetalProfiler.h"
#include "MetalCommandBuffer.h"
#include "Serialization/MemoryReader.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/Compression.h"
#include "Misc/MessageDialog.h"

#define SHADERCOMPILERCOMMON_API
#	include "Developer/ShaderCompilerCommon/Public/ShaderCompilerCommon.h"
#undef SHADERCOMPILERCOMMON_API


NSString* DecodeMetalSourceCode(uint32 CodeSize, TArray<uint8> const& CompressedSource)
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

mtlpp::LanguageVersion ValidateVersion(uint32 Version)
{
	static uint32 MetalMacOSVersions[][3] = {
		{10,15,0},
		{11,0,0},
		{12,0,0}
	};
	static uint32 MetaliOSVersions[][3] = {
		{13,0,0},
		{14,0,0},
		{15,0,0},
	};
	static TCHAR const* StandardNames[] =
	{
		TEXT("Metal 2.2"),
		TEXT("Metal 2.3"),
		TEXT("Metal 2.4"),
	};
	
	mtlpp::LanguageVersion Result = mtlpp::LanguageVersion::Version2_2;
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
			//EMacMetalShaderStandard::MacMetalSLStandard_Minimum and EIOSMetalShaderStandard::IOSMetalSLStandard_Minimum is currently 2.2
			UE_LOG(LogTemp, Warning, TEXT("The Metal version currently set is not supported anymore. Set it in the Project Settings. Defaulting to the minimum version."));
			Version = 5;
			Result = mtlpp::LanguageVersion::Version2_2;
			break;
	}
	
	return Result;
}
