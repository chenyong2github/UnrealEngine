// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXDynamicRHI_Shaders.cpp: AGX Dynamic RHI Class Shader Methods.
=============================================================================*/


#include "AGXRHIPrivate.h"
#include "AGXShaderTypes.h"
#include "Shaders/AGXShaderLibrary.h"


//------------------------------------------------------------------------------

#pragma mark - AGX Dynamic RHI Shader Methods


FVertexShaderRHIRef FAGXDynamicRHI::RHICreateVertexShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	@autoreleasepool {
		FAGXVertexShader* Shader = new FAGXVertexShader(Code);
		return Shader;
	}
}

FPixelShaderRHIRef FAGXDynamicRHI::RHICreatePixelShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	@autoreleasepool {
		FAGXPixelShader* Shader = new FAGXPixelShader(Code);
		return Shader;
	}
}

FGeometryShaderRHIRef FAGXDynamicRHI::RHICreateGeometryShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	@autoreleasepool {
		FAGXGeometryShader* Shader = new FAGXGeometryShader;
		FMetalCodeHeader Header;
		Shader->Init(Code, Header);
		return Shader;
	}
}

FComputeShaderRHIRef FAGXDynamicRHI::RHICreateComputeShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	@autoreleasepool {
		return new FAGXComputeShader(Code);
	}
}

FRHIShaderLibraryRef FAGXDynamicRHI::RHICreateShaderLibrary(EShaderPlatform Platform, FString const& FilePath, FString const& Name)
{
	FString METAL_MAP_EXTENSION(TEXT(".metalmap"));

	@autoreleasepool {
		FRHIShaderLibraryRef Result = nullptr;

		FName PlatformName = LegacyShaderPlatformToShaderFormat(Platform);
		FString LibName = FString::Printf(TEXT("%s_%s"), *Name, *PlatformName.GetPlainNameString());
		LibName.ToLowerInline();

		FString BinaryShaderFile = FilePath / LibName + METAL_MAP_EXTENSION;

		if (IFileManager::Get().FileExists(*BinaryShaderFile) == false)
		{
			// the metal map files are stored in UFS file system
			// for pak files this means they might be stored in a different location as the pak files will mount them to the project content directory
			// the metal libraries are stores non UFS and could be anywhere on the file system.
			// if we don't find the metalmap file straight away try the pak file path
			BinaryShaderFile = FPaths::ProjectContentDir() / LibName + METAL_MAP_EXTENSION;
		}

		FScopeLock Lock(&FAGXShaderLibrary::LoadedShaderLibraryMutex);

		FRHIShaderLibrary** FoundShaderLibrary = FAGXShaderLibrary::LoadedShaderLibraryMap.Find(BinaryShaderFile);
		if (FoundShaderLibrary)
		{
			return *FoundShaderLibrary;
		}

		FArchive* BinaryShaderAr = IFileManager::Get().CreateFileReader(*BinaryShaderFile);

		if( BinaryShaderAr != NULL )
		{
			FMetalShaderLibraryHeader Header;
			FSerializedShaderArchive SerializedShaders;
			TArray<uint8> ShaderCode;

			*BinaryShaderAr << Header;
			*BinaryShaderAr << SerializedShaders;
			*BinaryShaderAr << ShaderCode;
			BinaryShaderAr->Flush();
			delete BinaryShaderAr;

			// Would be good to check the language version of the library with the archive format here.
			if (Header.Format == PlatformName.GetPlainNameString())
			{
				check(((SerializedShaders.GetNumShaders() + Header.NumShadersPerLibrary - 1) / Header.NumShadersPerLibrary) == Header.NumLibraries);

				TArray<mtlpp::Library> Libraries;
				Libraries.Empty(Header.NumLibraries);

				for (uint32 i = 0; i < Header.NumLibraries; i++)
				{
					FString MetalLibraryFilePath = (FilePath / LibName) + FString::Printf(TEXT(".%d.metallib"), i);
					MetalLibraryFilePath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*MetalLibraryFilePath);

					METAL_GPUPROFILE(FAGXScopedCPUStats CPUStat(FString::Printf(TEXT("NewLibraryFile: %s"), *MetalLibraryFilePath)));
					NSError* Error;
					mtlpp::Library Library([GMtlDevice newLibraryWithFile:MetalLibraryFilePath.GetNSString() error:&Error], nullptr, ns::Ownership::Assign);
					if (Library != nil)
					{
						Libraries.Add(Library);
					}
					else
					{
						UE_LOG(LogAGX, Display, TEXT("Failed to create library: %s"), *FString(Error.description));
						return nullptr;
					}
				}

				Result = new FAGXShaderLibrary(Platform, Name, BinaryShaderFile, Header, SerializedShaders, ShaderCode, Libraries);
				FAGXShaderLibrary::LoadedShaderLibraryMap.Add(BinaryShaderFile, Result.GetReference());
			}
			//else
			//{
			//	UE_LOG(LogAGX, Display, TEXT("Wrong shader platform wanted: %s, got: %s"), *LibName, *Map.Format);
			//}
		}
		else
		{
			UE_LOG(LogAGX, Display, TEXT("No .metalmap file found for %s!"), *LibName);
		}

		return Result;
	}
}

FBoundShaderStateRHIRef FAGXDynamicRHI::RHICreateBoundShaderState(
	FRHIVertexDeclaration* VertexDeclarationRHI,
	FRHIVertexShader* VertexShaderRHI,
	FRHIPixelShader* PixelShaderRHI,
	FRHIGeometryShader* GeometryShaderRHI)
{
	NOT_SUPPORTED("RHICreateBoundShaderState");
	return nullptr;
}

FVertexShaderRHIRef FAGXDynamicRHI::CreateVertexShader_RenderThread(class FRHICommandListImmediate& RHICmdList, TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return RHICreateVertexShader(Code, Hash);
}

FGeometryShaderRHIRef FAGXDynamicRHI::CreateGeometryShader_RenderThread(class FRHICommandListImmediate& RHICmdList, TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return RHICreateGeometryShader(Code, Hash);
}

FPixelShaderRHIRef FAGXDynamicRHI::CreatePixelShader_RenderThread(class FRHICommandListImmediate& RHICmdList, TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return RHICreatePixelShader(Code, Hash);
}

FComputeShaderRHIRef FAGXDynamicRHI::CreateComputeShader_RenderThread(class FRHICommandListImmediate& RHICmdList, TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return RHICreateComputeShader(Code, Hash);
}

FRHIShaderLibraryRef FAGXDynamicRHI::RHICreateShaderLibrary_RenderThread(class FRHICommandListImmediate& RHICmdList, EShaderPlatform Platform, FString FilePath, FString Name)
{
	return RHICreateShaderLibrary(Platform, FilePath, Name);
}
