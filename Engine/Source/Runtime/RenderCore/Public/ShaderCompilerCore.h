// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShaderCompilerCore.h: Shader Compiler core module definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "Templates/RefCounting.h"
#include "Misc/SecureHash.h"
#include "Misc/Paths.h"
#include "Misc/CoreStats.h"

class Error;

// this is for the protocol, not the data, bump if FShaderCompilerInput or ProcessInputFromArchive changes.
const int32 ShaderCompileWorkerInputVersion = 13;
// this is for the protocol, not the data, bump if FShaderCompilerOutput or WriteToOutputArchive changes.
const int32 ShaderCompileWorkerOutputVersion = 6;
// this is for the protocol, not the data.
const int32 ShaderCompileWorkerSingleJobHeader = 'S';
// this is for the protocol, not the data.
const int32 ShaderCompileWorkerPipelineJobHeader = 'P';

/** Returns true if debug information should be kept for a given platform. */
extern RENDERCORE_API bool ShouldKeepShaderDebugInfo(EShaderPlatform Platform);

/** Returns true if debug information should be exported to separate files for a given platform . */
extern RENDERCORE_API bool ShouldExportShaderDebugInfo(EShaderPlatform Platform);

enum ECompilerFlags
{
	CFLAG_PreferFlowControl = 0,
	CFLAG_Debug,
	CFLAG_AvoidFlowControl,
	// Disable shader validation
	CFLAG_SkipValidation,
	// Only allows standard optimizations, not the longest compile times.
	CFLAG_StandardOptimization,
	// Always optimize, even when CFLAG_Debug is set. Required for some complex shaders and features.
	CFLAG_ForceOptimization,
	// Shader should use on chip memory instead of main memory ring buffer memory.
	CFLAG_OnChip,
	// Shader should insert debug/name info at the risk of generating non-deterministic libraries
	CFLAG_KeepDebugInfo,
	CFLAG_NoFastMath,
	// Explicitly enforce zero initialization on shader platforms that may omit it.
	CFLAG_ZeroInitialise,
	// Explicitly enforce bounds checking on shader platforms that may omit it.
	CFLAG_BoundsChecking,
	// Force removing unused interpolators for platforms that can opt out
	CFLAG_ForceRemoveUnusedInterpolators,
	// Set default precision to highp in a pixel shader (default is mediump on ES platforms)
	CFLAG_UseFullPrecisionInPS,
	// Hint that it is a vertex to geometry shader
	CFLAG_VertexToGeometryShader,
	// Hint that it is a vertex to primitive shader
	CFLAG_VertexToPrimitiveShader,
	// Hint that a vertex shader should use automatic culling on certain platforms.
	CFLAG_VertexUseAutoCulling,
	// Prepare the shader for archiving in the native binary shader cache format
	CFLAG_Archive,
	// Shaders uses external texture so may need special runtime handling
	CFLAG_UsesExternalTexture,
	// Use emulated uniform buffers on supported platforms
	CFLAG_UseEmulatedUB,
	// Enable wave operation intrinsics (requires DX12 and DXC/DXIL on PC).
	// Check GRHISupportsWaveOperations before using shaders compiled with this flag at runtime.
	// https://github.com/Microsoft/DirectXShaderCompiler/wiki/Wave-Intrinsics
	CFLAG_WaveOperations,
	// Use DirectX Shader Compiler (DXC) to compile all shaders, intended for compatibility testing.
	CFLAG_ForceDXC,
	CFLAG_SkipOptimizations,
	// Temporarily disable optimizations with DXC compiler only, intended to workaround shader compiler bugs until they can be resolved with 1st party
	CFLAG_SkipOptimizationsDXC,
	// Typed UAV loads are disallowed by default, as Windows 7 D3D 11.0 does not support them; this flag allows a shader to use them.
	CFLAG_AllowTypedUAVLoads,

	// Force using the SC rewrite functionality before calling DXC on D3D12
	CFLAG_D3D12ForceShaderConductorRewrite,
	// Enable support of C-style data types for platforms that can. Check for PLATFORM_SUPPORTS_REAL_TYPES.
	CFLAG_AllowRealTypes,

	CFLAG_Max,
};
static_assert(CFLAG_Max < 32, "Out of bitfields!");

struct FShaderCompilerResourceTable
{
	/** Bits indicating which resource tables contain resources bound to this shader. */
	uint32 ResourceTableBits;

	/** The max index of a uniform buffer from which resources are bound. */
	uint32 MaxBoundResourceTable;

	/** Mapping of bound Textures to their location in resource tables. */
	TArray<uint32> TextureMap;

	/** Mapping of bound SRVs to their location in resource tables. */
	TArray<uint32> ShaderResourceViewMap;

	/** Mapping of bound sampler states to their location in resource tables. */
	TArray<uint32> SamplerMap;

	/** Mapping of bound UAVs to their location in resource tables. */
	TArray<uint32> UnorderedAccessViewMap;

	/** Hash of the layouts of resource tables at compile time, used for runtime validation. */
	TArray<uint32> ResourceTableLayoutHashes;

	FShaderCompilerResourceTable()
		: ResourceTableBits(0)
		, MaxBoundResourceTable(0)
	{
	}
};

/** Additional compilation settings that can be configured by each FMaterial instance before compilation */
struct FExtraShaderCompilerSettings
{
	bool bExtractShaderSource = false;
	FString OfflineCompilerPath;

	friend FArchive& operator<<(FArchive& Ar, FExtraShaderCompilerSettings& StatsSettings)
	{
		// Note: this serialize is used to pass between UE4 and the shader compile worker, recompile both when modifying
		return Ar << StatsSettings.bExtractShaderSource << StatsSettings.OfflineCompilerPath;
	}
};

/** Struct that gathers all readonly inputs needed for the compilation of a single shader. */
struct FShaderCompilerInput
{
	FShaderTarget Target;
	FName ShaderFormat;
	FString SourceFilePrefix;
	FString VirtualSourceFilePath;
	FString EntryPointName;

	// Skips the preprocessor and instead loads the usf file directly
	bool bSkipPreprocessedCache;

	bool bGenerateDirectCompileFile;

	// Shader pipeline information
	bool bCompilingForShaderPipeline;
	bool bIncludeUsedOutputs;
	TArray<FString> UsedOutputs;

	// Dump debug path (up to platform) e.g. "D:/MMittring-Z3941-A/UE4-Orion/OrionGame/Saved/ShaderDebugInfo/PCD3D_SM5"
	FString DumpDebugInfoRootPath;
	// only used if enabled by r.DumpShaderDebugInfo (platform/groupname) e.g. ""
	FString DumpDebugInfoPath;
	// materialname or "Global" "for debugging and better error messages
	FString DebugGroupName;

	FString DebugExtension;

	// Description of the configuration used when compiling. 
	FString DebugDescription;

	// Compilation Environment
	FShaderCompilerEnvironment Environment;
	TRefCountPtr<FSharedShaderCompilerEnvironment> SharedEnvironment;


	struct FRootParameterBinding
	{
		/** Name of the constant buffer stored parameter. */
		FString Name;

		/** Type expected in the shader code to ensure the binding is bug free. */
		FString ExpectedShaderType;

		/** The offset of the parameter in the root shader parameter struct. */
		uint16 ByteOffset;

		friend FArchive& operator<<(FArchive& Ar, FRootParameterBinding& RootParameterBinding)
		{
			Ar << RootParameterBinding.Name;
			Ar << RootParameterBinding.ExpectedShaderType;
			Ar << RootParameterBinding.ByteOffset;
			return Ar;
		}
	};

	TArray<FRootParameterBinding> RootParameterBindings;


	// Additional compilation settings that can be filled by FMaterial::SetupExtaCompilationSettings
	// FMaterial::SetupExtaCompilationSettings is usually called by each (*)MaterialShaderType::BeginCompileShader() function
	FExtraShaderCompilerSettings ExtraSettings;

	FShaderCompilerInput() :
		Target(SF_NumFrequencies, SP_NumPlatforms),
		bSkipPreprocessedCache(false),
		bGenerateDirectCompileFile(false),
		bCompilingForShaderPipeline(false),
		bIncludeUsedOutputs(false)
	{
	}

	// generate human readable name for debugging
	FString GenerateShaderName() const
	{
		FString Name;

		if(DebugGroupName == TEXT("Global"))
		{
			Name = VirtualSourceFilePath + TEXT("|") + EntryPointName;
		}
		else
		{
			// we skip EntryPointName as it's usually not useful
			Name = DebugGroupName + TEXT(":") + VirtualSourceFilePath;
		}

		return Name;
	}

	FString GetSourceFilename() const
	{
		return FPaths::GetCleanFilename(VirtualSourceFilePath);
	}

	void GatherSharedInputs(TMap<FString,FString>& ExternalIncludes, TArray<TRefCountPtr<FSharedShaderCompilerEnvironment>>& SharedEnvironments)
	{
		check(!SharedEnvironment || SharedEnvironment->IncludeVirtualPathToExternalContentsMap.Num() == 0);

		for (const auto& It : Environment.IncludeVirtualPathToExternalContentsMap)
		{
			FString* FoundEntry = ExternalIncludes.Find(It.Key);

			if (!FoundEntry)
			{
				ExternalIncludes.Add(It.Key, *It.Value);
			}
		}

		if (SharedEnvironment)
		{
			SharedEnvironments.AddUnique(SharedEnvironment);
		}
	}

	void SerializeSharedInputs(FArchive& Ar, const TArray<TRefCountPtr<FSharedShaderCompilerEnvironment>>& SharedEnvironments)
	{
		check(Ar.IsSaving());

		TArray<FString> ReferencedExternalIncludes;
		ReferencedExternalIncludes.Empty(Environment.IncludeVirtualPathToExternalContentsMap.Num());

		for (const auto& It : Environment.IncludeVirtualPathToExternalContentsMap)
		{
			ReferencedExternalIncludes.Add(It.Key);
		}

		Ar << ReferencedExternalIncludes;

		int32 SharedEnvironmentIndex = SharedEnvironments.Find(SharedEnvironment);
		Ar << SharedEnvironmentIndex;
	}

	void DeserializeSharedInputs(FArchive& Ar, const TMap<FString, FThreadSafeSharedStringPtr>& ExternalIncludes, const TArray<FShaderCompilerEnvironment>& SharedEnvironments)
	{
		check(Ar.IsLoading());

		TArray<FString> ReferencedExternalIncludes;
		Ar << ReferencedExternalIncludes;

		Environment.IncludeVirtualPathToExternalContentsMap.Reserve(ReferencedExternalIncludes.Num());

		for (int32 i = 0; i < ReferencedExternalIncludes.Num(); i++)
		{
			Environment.IncludeVirtualPathToExternalContentsMap.Add(ReferencedExternalIncludes[i], ExternalIncludes.FindChecked(ReferencedExternalIncludes[i]));
		}

		int32 SharedEnvironmentIndex = 0;
		Ar << SharedEnvironmentIndex;

		if (SharedEnvironments.IsValidIndex(SharedEnvironmentIndex))
		{
			Environment.Merge(SharedEnvironments[SharedEnvironmentIndex]);
		}
	}

	friend FArchive& operator<<(FArchive& Ar,FShaderCompilerInput& Input)
	{
		// Note: this serialize is used to pass between UE4 and the shader compile worker, recompile both when modifying
		Ar << Input.Target;
		{
			FString ShaderFormatString(Input.ShaderFormat.ToString());
			Ar << ShaderFormatString;
			Input.ShaderFormat = FName(*ShaderFormatString);
		}
		Ar << Input.SourceFilePrefix;
		Ar << Input.VirtualSourceFilePath;
		Ar << Input.EntryPointName;
		Ar << Input.bSkipPreprocessedCache;
		Ar << Input.bCompilingForShaderPipeline;
		Ar << Input.bGenerateDirectCompileFile;
		Ar << Input.bIncludeUsedOutputs;
		Ar << Input.UsedOutputs;
		Ar << Input.DumpDebugInfoRootPath;
		Ar << Input.DumpDebugInfoPath;
		Ar << Input.DebugExtension;
		Ar << Input.DebugGroupName;
		Ar << Input.DebugDescription;
		Ar << Input.Environment;
		Ar << Input.ExtraSettings;
		Ar << Input.RootParameterBindings;

		// Note: skipping Input.SharedEnvironment, which is handled by FShaderCompileUtilities::DoWriteTasks in order to maintain sharing

		return Ar;
	}

	bool IsUsingTessellation() const
	{
		switch (Target.GetFrequency())
		{
		case SF_Vertex:
		{
			const FString* UsingTessellationDefine = Environment.GetDefinitions().Find(TEXT("USING_TESSELLATION"));
			return (UsingTessellationDefine != nullptr && *UsingTessellationDefine == TEXT("1"));
		}
		case SF_Hull:
		case SF_Domain:
			return true;
		default:
			return false;
		}
	}

	bool IsRayTracingShader() const
	{
		return IsRayTracingShaderFrequency(Target.GetFrequency());
	}
};

/** A shader compiler error or warning. */
struct FShaderCompilerError
{
	FShaderCompilerError(const TCHAR* InStrippedErrorMessage = TEXT(""))
		: ErrorVirtualFilePath(TEXT(""))
		, ErrorLineString(TEXT(""))
		, StrippedErrorMessage(InStrippedErrorMessage)
		, HighlightedLine(TEXT(""))
		, HighlightedLineMarker(TEXT(""))
	{}

	FShaderCompilerError(const TCHAR* InVirtualFilePath, const TCHAR* InLineString, const TCHAR* InStrippedErrorMessage)
		: ErrorVirtualFilePath(InVirtualFilePath)
		, ErrorLineString(InLineString)
		, StrippedErrorMessage(InStrippedErrorMessage)
		, HighlightedLine(TEXT(""))
		, HighlightedLineMarker(TEXT(""))
	{}

	FShaderCompilerError(FString&& InStrippedErrorMessage)
		: ErrorVirtualFilePath(TEXT(""))
		, ErrorLineString(TEXT(""))
		, StrippedErrorMessage(MoveTemp(InStrippedErrorMessage))
		, HighlightedLine(TEXT(""))
		, HighlightedLineMarker(TEXT(""))
	{}

	FShaderCompilerError(FString&& InStrippedErrorMessage, FString&& InHighlightedLine, FString&& InHighlightedLineMarker)
		: ErrorVirtualFilePath(TEXT(""))
		, ErrorLineString(TEXT(""))
		, StrippedErrorMessage(MoveTemp(InStrippedErrorMessage))
		, HighlightedLine(MoveTemp(InHighlightedLine))
		, HighlightedLineMarker(MoveTemp(InHighlightedLineMarker))
	{}

	FString ErrorVirtualFilePath;
	FString ErrorLineString;
	FString StrippedErrorMessage;
	FString HighlightedLine;
	FString HighlightedLineMarker;

	/** Returns the error message with source file and source line (if present). */
	FString GetErrorString() const
	{
		if (ErrorVirtualFilePath.IsEmpty())
		{
			return StrippedErrorMessage;
		}
		else
		{
			return ErrorVirtualFilePath + TEXT("(") + ErrorLineString + TEXT("): ") + StrippedErrorMessage;
		}
	}

	/** Returns the error message with source file and source line (if present), as well as a line marker separated with a LINE_TERMINATOR. */
	FString GetErrorStringWithLineMarker() const
	{
		if (HasLineMarker())
		{
			// Append highlighted line and its marker to the same error message with line terminators
			// to get a similar multiline error output as with DXC
			return (GetErrorString() + LINE_TERMINATOR + TEXT("\t") + HighlightedLine + LINE_TERMINATOR + TEXT("\t") + HighlightedLineMarker);
		}
		else
		{
			return GetErrorString();
		}
	}

	/**
	Returns true if this error message has a marker string for the highlighted source line where the error occurred. Example:
		/Engine/Private/MySourceFile.usf(120): error: undeclared identifier 'a'
		float b = a;
				  ^
	*/
	bool HasLineMarker() const
	{
		return !HighlightedLine.IsEmpty() && !HighlightedLineMarker.IsEmpty();
	}

	/** Returns the path of the underlying source file relative to the process base dir. */
	FString RENDERCORE_API GetShaderSourceFilePath() const;

	friend FArchive& operator<<(FArchive& Ar,FShaderCompilerError& Error)
	{
		return Ar << Error.ErrorVirtualFilePath << Error.ErrorLineString << Error.StrippedErrorMessage << Error.HighlightedLine << Error.HighlightedLineMarker;
	}
};

/**
 *	The output of the shader compiler.
 *	Bump ShaderCompileWorkerOutputVersion if FShaderCompilerOutput changes
 */
struct FShaderCompilerOutput
{
	FShaderCompilerOutput()
	:	NumInstructions(0)
	,	NumTextureSamplers(0)
	,	CompileTime(0.0)
	,	bSucceeded(false)
	,	bFailedRemovingUnused(false)
	,	bSupportsQueryingUsedAttributes(false)
	,	bUsedHLSLccCompiler(false)
	{
	}

	FShaderParameterMap ParameterMap;
	TArray<FShaderCompilerError> Errors;
	TArray<FString> PragmaDirectives;
	FShaderTarget Target;
	FShaderCode ShaderCode;
	FSHAHash OutputHash;
	uint32 NumInstructions;
	uint32 NumTextureSamplers;
	double CompileTime;
	bool bSucceeded;
	bool bFailedRemovingUnused;
	bool bSupportsQueryingUsedAttributes;
	bool bUsedHLSLccCompiler;
	TArray<FString> UsedAttributes;

	FString OptionalFinalShaderSource;

	TArray<uint8> PlatformDebugData;

	/** Generates OutputHash from the compiler output. */
	RENDERCORE_API void GenerateOutputHash();

	friend FArchive& operator<<(FArchive& Ar, FShaderCompilerOutput& Output)
	{
		// Note: this serialize is used to pass between UE4 and the shader compile worker, recompile both when modifying
		Ar << Output.ParameterMap << Output.Errors << Output.Target << Output.ShaderCode << Output.NumInstructions << Output.NumTextureSamplers << Output.bSucceeded;
		Ar << Output.bFailedRemovingUnused << Output.bSupportsQueryingUsedAttributes << Output.UsedAttributes;
		Ar << Output.CompileTime;
		Ar << Output.OptionalFinalShaderSource;
		Ar << Output.PlatformDebugData;

		return Ar;
	}
};

enum class ESCWErrorCode
{
	NotSet = -1,
	Success,
	GeneralCrash,
	BadShaderFormatVersion,
	BadInputVersion,
	BadSingleJobHeader,
	BadPipelineJobHeader,
	CantDeleteInputFile,
	CantSaveOutputFile,
	NoTargetShaderFormatsFound,
	CantCompileForSpecificFormat,
	CrashInsidePlatformCompiler,
};

/**
 * Validates the format of a virtual shader file path.
 * Meant to be use as such: check(CheckVirtualShaderFilePath(VirtualFilePath));
 * CompileErrors output array is optional.
 */
extern RENDERCORE_API bool CheckVirtualShaderFilePath(const FString& VirtualPath, TArray<FShaderCompilerError>* CompileErrors = nullptr);

/**
 * Loads the shader file with the given name.
 * @param VirtualFilePath - The virtual path of shader file to load.
 * @param OutFileContents - If true is returned, will contain the contents of the shader file. Can be null.
 * @return True if the file was successfully loaded.
 */
extern RENDERCORE_API bool LoadShaderSourceFile(const TCHAR* VirtualFilePath, EShaderPlatform ShaderPlatform, FString* OutFileContents, TArray<FShaderCompilerError>* OutCompileErrors);
