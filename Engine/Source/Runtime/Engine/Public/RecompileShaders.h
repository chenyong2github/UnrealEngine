// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShaderCore.h"
#include "ShaderCompilerCore.h"
#include "Shader.h"

/**
* Handles serializing in MeshMaterialMaps or GlobalShaderMap from a CookOnTheFly command and applying them to the in-memory shadermaps.
*
* @param MeshMaterialMaps				Byte array that contains the serialized material shadermap from across the network.
* @param MaterialsToLoad				The materials contained in the MeshMaterialMaps
* @param GlobalShaderMap				Byte array that contains the serialized global shadermap from across the network.
**/
extern ENGINE_API void ProcessCookOnTheFlyShaders(bool bReloadGlobalShaders, const TArray<uint8>& MeshMaterialMaps, const TArray<FString>& MaterialsToLoad, const TArray<uint8>& GlobalShaderMap);

struct FODSCRequestPayload
{
	/** The shader platform to compile for. */
	EShaderPlatform ShaderPlatform;

	/** Which material do we compile for?. */
	FString MaterialName;

	/** The vertex factory type name to compile shaders for. */
	FString VertexFactoryName;

	/** The name of the pipeline to compile shaders for. */
	FString PipelineName;

	/** An array of shader type names for each stage in the Pipeline. */
	TArray<FString> ShaderTypeNames;

	/** A hash of the above information to uniquely identify a Request. */
	FString RequestHash;

	ENGINE_API FODSCRequestPayload() {};
	ENGINE_API FODSCRequestPayload(EShaderPlatform InShaderPlatform, const FString& InMaterialName, const FString& InVertexFactoryName, const FString& InPipelineName, const TArray<FString>& InShaderTypeNames, const FString& InRequestHash);

	/**
	* Serializes FODSCRequestPayload value from or into this archive.
	*
	* @param Ar The archive to serialize to.
	* @param Value The value to serialize.
	* @return The archive.
	*/
	ENGINE_API friend FArchive& operator<<(FArchive& Ar, FODSCRequestPayload& Elem);
};

enum class ODSCRecompileCommand
{
	None,
	Changed,
	Global,
	Material
};

struct FShaderRecompileData
{
	/** The platform name to compile for. */
	FString PlatformName;

	/** Shader platform */
	EShaderPlatform ShaderPlatform = SP_NumPlatforms;

	/** All filenames that have been changed during the shader compilation. */
	TArray<FString>* ModifiedFiles = nullptr;

	/** Mesh materials, returned to the caller.  */
	TArray<uint8>* MeshMaterialMaps = nullptr;

	/** Materials to load. */
	TArray<FString> MaterialsToLoad;

	/** What type of shaders to recompile. All, Changed, Global, or Material? */
	ODSCRecompileCommand CommandType = ODSCRecompileCommand::Changed;

	/** Global shader map, returned to the caller.  */
	TArray<uint8>* GlobalShaderMap = nullptr;

	/** On-demand shader compiler payload.  */
	TArray<FODSCRequestPayload> ShadersToRecompile;

	/** Default constructor. */
	ENGINE_API FShaderRecompileData() {};

	/** Recompile all the changed shaders for the current platform. */
	ENGINE_API FShaderRecompileData(const FString& InPlatformName, TArray<FString>* OutModifiedFiles, TArray<uint8>* OutMeshMaterialMaps, TArray<uint8>* OutGlobalShaderMap);

	/** For recompiling just global shaders. */
	ENGINE_API FShaderRecompileData(const FString& InPlatformName, EShaderPlatform InShaderPlatform, ODSCRecompileCommand InCommandType, TArray<FString>* OutModifiedFiles, TArray<uint8>* OutMeshMaterialMaps, TArray<uint8>* OutGlobalShaderMap);

	FShaderRecompileData& operator=(const FShaderRecompileData& Other)
	{
		PlatformName = Other.PlatformName;
		ShaderPlatform = Other.ShaderPlatform;
		ModifiedFiles = Other.ModifiedFiles;
		MeshMaterialMaps = Other.MeshMaterialMaps;
		MaterialsToLoad = Other.MaterialsToLoad;
		CommandType = Other.CommandType;
		GlobalShaderMap = Other.GlobalShaderMap;

		ShadersToRecompile = Other.ShadersToRecompile;

		return *this;
	}
};

/** Serializes a global shader map to an archive (used with recompiling shaders for a remote console) */
extern ENGINE_API void SaveGlobalShadersForRemoteRecompile(FArchive& Ar, EShaderPlatform ShaderPlatform);

/** Serializes a global shader map to an archive (used with recompiling shaders for a remote console) */
extern ENGINE_API void LoadGlobalShadersForRemoteRecompile(FArchive& Ar, EShaderPlatform ShaderPlatform);

#if WITH_EDITOR

/**
* Recompiles global shaders
*
* @param Args					Arguments and configuration for issuing recompiles.
* @param OutputDirectory		The directory the compiled data will be stored to
**/
extern ENGINE_API void RecompileShadersForRemote(FShaderRecompileData& Args, const FString& OutputDirectory);

#endif // WITH_EDITOR