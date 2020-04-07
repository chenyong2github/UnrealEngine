// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShaderCodeLibrary.h: 
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Containers/StringFwd.h"
#include "RHI.h"

DECLARE_LOG_CATEGORY_EXTERN(LogShaderLibrary, Log, All);

class FShaderPipeline;
class FShaderMapResource;
class FShaderMapResourceCode;

struct RENDERCORE_API FShaderCodeLibraryPipeline
{
	FSHAHash Shaders[SF_NumGraphicsFrequencies];
	mutable uint32 Hash;
	
	/** Fills the hashes from the pipeline stage shaders */
	void Initialize(const FShaderPipeline* Pipeline);

	FShaderCodeLibraryPipeline() : Hash(0) {}
	
	friend bool operator ==(const FShaderCodeLibraryPipeline& A,const FShaderCodeLibraryPipeline& B)
	{
		for (uint32 Frequency = 0u; Frequency < SF_NumGraphicsFrequencies; ++Frequency)
		{
			if (A.Shaders[Frequency] != B.Shaders[Frequency])
			{
				return false;
			}
		}
		return true;
	}
	
	friend uint32 GetTypeHash(const FShaderCodeLibraryPipeline &Key)
	{
		if(!Key.Hash)
		{
			for (uint32 Frequency = 0u; Frequency < SF_NumGraphicsFrequencies; ++Frequency)
			{
				Key.Hash = FCrc::MemCrc32(Key.Shaders[Frequency].Hash, sizeof(FSHAHash), Key.Hash);
			}
		}
		return Key.Hash;
	}

	/** Computes a longer hash that uniquely identifies the whole pipeline, used in FStableShaderKeyAndValue */
	void GetPipelineHash(FSHAHash& Output);
	
	friend FArchive& operator<<( FArchive& Ar, FShaderCodeLibraryPipeline& Info )
	{
		for (uint32 Frequency = 0u; Frequency < SF_NumGraphicsFrequencies; ++Frequency)
		{
			Ar << Info.Shaders[Frequency];
		}
		return Ar << Info.Hash;
	}
};

struct RENDERCORE_API FCompactFullName
{
	TArray<FName, TInlineAllocator<16>> ObjectClassAndPath;

	bool operator==(const FCompactFullName& Other) const
	{
		return ObjectClassAndPath == Other.ObjectClassAndPath;
	}

	FString ToString() const;
	void AppendString(FStringBuilderBase& Out) const;
	void AppendString(FAnsiStringBuilderBase& Out) const;
	void ParseFromString(const FStringView& Src);
	friend RENDERCORE_API uint32 GetTypeHash(const FCompactFullName& A);
};


struct RENDERCORE_API FStableShaderKeyAndValue
{
	FCompactFullName ClassNameAndObjectPath;
	FName ShaderType;
	FName ShaderClass;
	FName MaterialDomain;
	FName FeatureLevel;
	FName QualityLevel;
	FName TargetFrequency;
	FName TargetPlatform;
	FName VFType;
	FName PermutationId;
	FSHAHash PipelineHash;

	uint32 KeyHash;

	FSHAHash OutputHash;

	FStableShaderKeyAndValue()
		: KeyHash(0)
	{
	}

	void ComputeKeyHash();
	void ParseFromString(const FStringView& Src);
	void ParseFromStringCached(const FStringView& Src, class TMap<uint32, FName>& NameCache);
	FString ToString() const;
	void ToString(FString& OutResult) const;
	void AppendString(FAnsiStringBuilderBase& Out) const;
	static FString HeaderLine();

	/** Computes pipeline hash from the passed pipeline. Pass nullptr to clear */
	void SetPipelineHash(const FShaderPipeline* Pipeline);

	friend bool operator ==(const FStableShaderKeyAndValue& A, const FStableShaderKeyAndValue& B)
	{
		return
			A.ClassNameAndObjectPath == B.ClassNameAndObjectPath &&
			A.ShaderType == B.ShaderType &&
			A.ShaderClass == B.ShaderClass &&
			A.MaterialDomain == B.MaterialDomain &&
			A.FeatureLevel == B.FeatureLevel &&
			A.QualityLevel == B.QualityLevel &&
			A.TargetFrequency == B.TargetFrequency &&
			A.TargetPlatform == B.TargetPlatform &&
			A.VFType == B.VFType &&
			A.PermutationId == B.PermutationId &&
			A.PipelineHash == B.PipelineHash;
	}

	friend uint32 GetTypeHash(const FStableShaderKeyAndValue &Key)
	{
		return Key.KeyHash;
	}

};

DECLARE_MULTICAST_DELEGATE_TwoParams(FSharedShaderCodeRequest, const FSHAHash&, FArchive*);
DECLARE_MULTICAST_DELEGATE_OneParam(FSharedShaderCodeRelease, const FSHAHash&);

// Collection of unique shader code
// Populated at cook time
struct RENDERCORE_API FShaderCodeLibrary
{
	static void InitForRuntime(EShaderPlatform ShaderPlatform);
	static void Shutdown();
	
	static bool IsEnabled();
	
	// Open a named library.
	// For cooking this will place all added shaders & pipelines into the library file with this name.
	// At runtime this will open the shader library with this name.
	static bool OpenLibrary(FString const& Name, FString const& Directory);
    
	// Close a named library.
	// For cooking, after this point any AddShaderCode/AddShaderPipeline calls will be invalid until OpenLibrary is called again.
	// At runtime this will release the library data and further requests for shaders from this library will fail.
	static void CloseLibrary(FString const& Name);

    static bool ContainsShaderCode(const FSHAHash& Hash);

	static TRefCountPtr<FShaderMapResource> LoadResource(const FSHAHash& Hash, FArchive* Ar);

	static bool PreloadShader(const FSHAHash& Hash, FArchive* Ar);

	static FVertexShaderRHIRef CreateVertexShader(EShaderPlatform Platform, const FSHAHash& Hash);
	static FPixelShaderRHIRef CreatePixelShader(EShaderPlatform Platform, const FSHAHash& Hash);
	static FHullShaderRHIRef CreateHullShader(EShaderPlatform Platform, const FSHAHash& Hash);
	static FDomainShaderRHIRef CreateDomainShader(EShaderPlatform Platform, const FSHAHash& Hash);
	static FGeometryShaderRHIRef CreateGeometryShader(EShaderPlatform Platform, const FSHAHash& Hash);
	static FComputeShaderRHIRef CreateComputeShader(EShaderPlatform Platform, const FSHAHash& Hash);

	// Total number of shader entries in the library
	static uint32 GetShaderCount(void);
	
	// The shader platform that the library manages - at runtime this will only be one
	static EShaderPlatform GetRuntimeShaderPlatform(void);

#if WITH_EDITOR
	// Initialize the library cooker
	static void InitForCooking(bool bNativeFormat);
	
	// Clean the cook directories
	static void CleanDirectories(TArray<FName> const& ShaderFormats);
    
	struct FShaderFormatDescriptor
	{
		FName ShaderFormat;
		bool bNeedsStableKeys;
		bool bNeedsDeterministicOrder;
	};

	// Specify the shader formats to cook and which ones needs stable keys. Provide an array of FShaderFormatDescriptors
    static void CookShaderFormats(TArray<FShaderFormatDescriptor> const& ShaderFormats);

	// At cook time, mark a shadermap boundary.
	static void BeginShaderMap(EShaderPlatform InShaderPlatform, const TArray<FString>& AssociatedAssets, const FName& ShaderMapTypeName);
	static void EndShaderMap(EShaderPlatform InShaderPlatform);

	// At cook time, add shader code to collection
	static bool AddShaderCode(EShaderPlatform ShaderPlatform, const FShaderMapResourceCode* Code);

	// We check this early in the callstack to avoid creating a bunch of FName and keys and things we will never save anyway. 
	// Pass the shader platform to check or EShaderPlatform::SP_NumPlatforms to check if any of the registered types require
	// stable keys.
	static bool NeedsShaderStableKeys(EShaderPlatform ShaderPlatform);

	// At cook time, add the human readable key value information
	static void AddShaderStableKeyValue(EShaderPlatform ShaderPlatform, FStableShaderKeyAndValue& StableKeyValue);

	// Save collected shader code to a file for each specified shader platform
	static bool SaveShaderCode(const FString& OutputDir, const FString& MetaOutputDir, const TArray<FName>& ShaderFormats, TArray<FString>& OutSCLCSVPath);
	
	// Package the separate shader bytecode files into a single native shader library. Must be called by the master process.
	static bool PackageNativeShaderLibrary(const FString& ShaderCodeDir, const TArray<FName>& ShaderFormats);
	
	// Dump collected stats for each shader platform
	static void DumpShaderCodeStats();
	
	// Create a smaller 'patch' library that only contains data from 'NewMetaDataDir' not contained in any of 'OldMetaDataDirs'
	static bool CreatePatchLibrary(TArray<FString> const& OldMetaDataDirs, FString const& NewMetaDataDir, FString const& OutDir, bool bNativeFormat, bool bNeedsDeterministicOrder);
#endif
	
	// Safely assign the hash to a shader object
	static void SafeAssignHash(FRHIShader* InShader, const FSHAHash& Hash);

	// Delegate called whenever shader code is requested.
	static FDelegateHandle RegisterSharedShaderCodeRequestDelegate_Handle(const FSharedShaderCodeRequest::FDelegate& Delegate);
	static void UnregisterSharedShaderCodeRequestDelegate_Handle(FDelegateHandle Handle);
};
