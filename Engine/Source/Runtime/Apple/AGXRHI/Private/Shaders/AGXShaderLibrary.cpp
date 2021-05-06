// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXShaderLibrary.cpp: AGX RHI Shader Library Class Implementation.
=============================================================================*/


#include "AGXRHIPrivate.h"
#if !UE_BUILD_SHIPPING
#include "Debugging/AGXShaderDebugCache.h"
#include "Debugging/AGXShaderDebugZipFile.h"
#endif // !UE_BUILD_SHIPPING
#include "AGXShaderLibrary.h"
#include "AGXShaderTypes.h"


//------------------------------------------------------------------------------

#pragma mark - AGX RHI Shader Library Class Support Routines


template<typename ShaderType>
static TRefCountPtr<FRHIShader> AGXCreateMetalShader(TArrayView<const uint8> InCode, mtlpp::Library InLibrary)
{
	ShaderType* Shader = new ShaderType(InCode, InLibrary);
	if (!Shader->GetFunction())
	{
		delete Shader;
		Shader = nullptr;
	}

	return TRefCountPtr<FRHIShader>(Shader);
}


//------------------------------------------------------------------------------

#pragma mark - AGX RHI Shader Library Class Public Static Members


FCriticalSection FAGXShaderLibrary::LoadedShaderLibraryMutex;
TMap<FString, FRHIShaderLibrary*> FAGXShaderLibrary::LoadedShaderLibraryMap;


//------------------------------------------------------------------------------

#pragma mark - AGX RHI Shader Library Class


FAGXShaderLibrary::FAGXShaderLibrary(EShaderPlatform Platform,
										 FString const& Name,
										 const FString& InShaderLibraryFilename,
										 const FMetalShaderLibraryHeader& InHeader,
										 const FSerializedShaderArchive& InSerializedShaders,
										 const TArray<uint8>& InShaderCode,
										 const TArray<mtlpp::Library>& InLibrary)
	: FRHIShaderLibrary(Platform, Name)
	, ShaderLibraryFilename(InShaderLibraryFilename)
	, Library(InLibrary)
	, Header(InHeader)
	, SerializedShaders(InSerializedShaders)
	, ShaderCode(InShaderCode)
{
#if !UE_BUILD_SHIPPING
	DebugFile = nullptr;

	FName PlatformName = LegacyShaderPlatformToShaderFormat(Platform);
	FString LibName = FString::Printf(TEXT("%s_%s"), *Name, *PlatformName.GetPlainNameString());
	LibName.ToLowerInline();
	FString Path = FPaths::ProjectContentDir() / LibName + TEXT(".zip");

	if (IFileManager::Get().FileExists(*Path))
	{
		DebugFile = FAGXShaderDebugCache::Get().GetDebugFile(Path);
	}
#endif // !UE_BUILD_SHIPPING
}

FAGXShaderLibrary::~FAGXShaderLibrary()
{
	FScopeLock Lock(&LoadedShaderLibraryMutex);
	LoadedShaderLibraryMap.Remove(ShaderLibraryFilename);
}

bool FAGXShaderLibrary::IsNativeLibrary() const
{
	return true;
}

int32 FAGXShaderLibrary::GetNumShaders() const
{
	return SerializedShaders.ShaderEntries.Num();
}

int32 FAGXShaderLibrary::GetNumShaderMaps() const
{
	return SerializedShaders.ShaderMapEntries.Num();
}

int32 FAGXShaderLibrary::GetNumShadersForShaderMap(int32 ShaderMapIndex) const
{
	return SerializedShaders.ShaderMapEntries[ShaderMapIndex].NumShaders;
}

int32 FAGXShaderLibrary::GetShaderIndex(int32 ShaderMapIndex, int32 i) const
{
	const FShaderMapEntry& ShaderMapEntry = SerializedShaders.ShaderMapEntries[ShaderMapIndex];
	return SerializedShaders.ShaderIndices[ShaderMapEntry.ShaderIndicesOffset + i];
}

int32 FAGXShaderLibrary::FindShaderMapIndex(const FSHAHash& Hash)
{
	return SerializedShaders.FindShaderMap(Hash);
}

int32 FAGXShaderLibrary::FindShaderIndex(const FSHAHash& Hash)
{
	return SerializedShaders.FindShader(Hash);
}

TRefCountPtr<FRHIShader> FAGXShaderLibrary::CreateShader(int32 Index)
{
	const FShaderCodeEntry& ShaderEntry = SerializedShaders.ShaderEntries[Index];

	// We don't handle compressed shaders here, since typically these are just tiny headers.
	check(ShaderEntry.Size == ShaderEntry.UncompressedSize);

	const TArrayView<uint8> Code = MakeArrayView(ShaderCode.GetData() + ShaderEntry.Offset, ShaderEntry.Size);
	const int32 LibraryIndex = Index / Header.NumShadersPerLibrary;

	TRefCountPtr<FRHIShader> Shader;
	switch (ShaderEntry.Frequency)
	{
		case SF_Vertex:
			Shader = AGXCreateMetalShader<FAGXVertexShader>(Code, Library[LibraryIndex]);
			break;

		case SF_Pixel:
			Shader = AGXCreateMetalShader<FAGXPixelShader>(Code, Library[LibraryIndex]);
			break;

		case SF_Geometry:
			checkf(false, TEXT("Geometry shaders not supported"));
			break;

		case SF_Compute:
			Shader = AGXCreateMetalShader<FAGXComputeShader>(Code, Library[LibraryIndex]);
			break;

		default:
			checkNoEntry();
			break;
	}

	if (Shader)
	{
		Shader->SetHash(SerializedShaders.ShaderHashes[Index]);
	}

	return Shader;
}
