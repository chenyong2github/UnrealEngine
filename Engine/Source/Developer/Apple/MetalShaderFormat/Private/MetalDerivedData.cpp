// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetalDerivedData.h"
#include "MetalShaderFormat.h"
#include "Serialization/MemoryWriter.h"
#include "RHIDefinitions.h"
#include "Misc/FileHelper.h"
#include "hlslcc.h"
#include "MetalShaderFormat.h"
#include "MetalShaderResources.h"
#include "Misc/Paths.h"
#include "Misc/Compression.h"
#include "MetalBackend.h"
#include "Serialization/MemoryReader.h"

#if 1
#if PLATFORM_MAC || PLATFORM_WINDOWS
THIRD_PARTY_INCLUDES_START
#include "ShaderConductor/ShaderConductor.hpp"
#include "spirv_reflect.h"
THIRD_PARTY_INCLUDES_END
#endif
#endif

extern bool ExecRemoteProcess(const TCHAR* Command, const TCHAR* Params, int32* OutReturnCode, FString* OutStdOut, FString* OutStdErr);
extern FString GetXcodePath();
extern FString GetMetalStdLibPath(FString const& PlatformPath);
extern FString GetMetalCompilerVers(FString const& PlatformPath);
extern bool RemoteFileExists(const FString& Path);
extern FString MakeRemoteTempFolder(FString Path);
extern FString LocalPathToRemote(const FString& LocalPath, const FString& RemoteFolder);
extern bool CopyLocalFileToRemote(FString const& LocalPath, FString const& RemotePath);
extern bool CopyRemoteFileToLocal(FString const& RemotePath, FString const& LocalPath);
extern bool ChecksumRemoteFile(FString const& RemotePath, uint32* CRC, uint32* Len);
extern bool ModificationTimeRemoteFile(FString const& RemotePath, uint64& Time);
extern bool RemoveRemoteFile(FString const& RemotePath);
extern FString GetMetalBinaryPath(uint32 ShaderPlatform);
extern FString GetMetalToolsPath(uint32 ShaderPlatform);
extern FString GetMetalLibraryPath(uint32 ShaderPlatform);
extern FString GetMetalCompilerVersion(uint32 ShaderPlatform);
extern uint16 GetXcodeVersion(uint64& BuildVersion);
extern EShaderPlatform MetalShaderFormatToLegacyShaderPlatform(FName ShaderFormat);
extern void BuildMetalShaderOutput(
	FShaderCompilerOutput& ShaderOutput,
	const FShaderCompilerInput& ShaderInput,
	FSHAHash const& GUIDHash,
	uint32 CCFlags,
	const ANSICHAR* InShaderSource,
	uint32 SourceLen,
	uint32 SourceCRCLen,
	uint32 SourceCRC,
	uint8 Version,
	TCHAR const* Standard,
	TCHAR const* MinOSVersion,
	EMetalTypeBufferMode TypeMode,
	TArray<FShaderCompilerError>& OutErrors,
	FMetalTessellationOutputs const& TessOutputAttribs,
	uint32 TypedBuffers,
	uint32 InvariantBuffers,
	uint32 TypedUAVs,
	uint32 ConstantBuffers,
	TArray<uint8> const& TypedBufferFormats,
	bool bAllowFastIntriniscs
);

FMetalShaderDebugInfoCooker::FMetalShaderDebugInfoCooker(FMetalShaderDebugInfoJob& InJob)
	: Job(InJob)
{
}

FMetalShaderDebugInfoCooker::~FMetalShaderDebugInfoCooker()
{
}

#if PLATFORM_MAC || PLATFORM_IOS
#pragma mark - FDerivedDataPluginInterface Interface -
#endif
const TCHAR* FMetalShaderDebugInfoCooker::GetPluginName() const
{
	return TEXT("FMetalShaderDebugInfo");
}

const TCHAR* FMetalShaderDebugInfoCooker::GetVersionString() const
{
	static FString Version = FString::Printf(TEXT("%u"), (uint32)GetMetalFormatVersion(Job.ShaderFormat));
	return *Version;
}

FString FMetalShaderDebugInfoCooker::GetPluginSpecificCacheKeySuffix() const
{
	FString CompilerVersion = GetMetalCompilerVersion(MetalShaderFormatToLegacyShaderPlatform(Job.ShaderFormat));

	FString VersionedName = FString::Printf(TEXT("%s%u%u%s%s%s%s%s%s"), *Job.ShaderFormat.GetPlainNameString(), Job.SourceCRCLen, Job.SourceCRC, *Job.Hash.ToString(), *Job.CompilerVersion, *Job.MinOSVersion, *Job.DebugInfo, *Job.MathMode, *Job.Standard);
	// get rid of some not so filename-friendly characters ('=',' ' -> '_')
	VersionedName = VersionedName.Replace(TEXT("="), TEXT("_")).Replace(TEXT(" "), TEXT("_"));

	return VersionedName;
}

bool FMetalShaderDebugInfoCooker::IsBuildThreadsafe() const
{
	return false;
}

bool FMetalShaderDebugInfoCooker::Build(TArray<uint8>& OutData)
{
	bool bSucceeded = false;

	uint32 CodeSize = FCStringAnsi::Strlen(TCHAR_TO_UTF8(*Job.MetalCode)) + 1;

	int32 CompressedSize = FCompression::CompressMemoryBound(NAME_Zlib, CodeSize);
	Output.CompressedData.SetNum(CompressedSize);

	if (FCompression::CompressMemory(NAME_Zlib, Output.CompressedData.GetData(), CompressedSize, TCHAR_TO_UTF8(*Job.MetalCode), CodeSize))
	{
		Output.UncompressedSize = CodeSize;
		Output.CompressedData.SetNum(CompressedSize);
		Output.CompressedData.Shrink();
		bSucceeded = true;

		FMemoryWriter Ar(OutData);
		Ar << Output;
	}

	return bSucceeded;
}

FMetalShaderBytecodeCooker::FMetalShaderBytecodeCooker(FMetalShaderBytecodeJob& InJob)
	: Job(InJob)
{
}

FMetalShaderBytecodeCooker::~FMetalShaderBytecodeCooker()
{
}

#if PLATFORM_MAC || PLATFORM_IOS
#pragma mark - FDerivedDataPluginInterface Interface - 
#endif
const TCHAR* FMetalShaderBytecodeCooker::GetPluginName() const
{
	return TEXT("MetalShaderBytecode");
}

const TCHAR* FMetalShaderBytecodeCooker::GetVersionString() const
{
	static FString Version = FString::Printf(TEXT("%u"), (uint32)GetMetalFormatVersion(Job.ShaderFormat));
	return *Version;
}

FString FMetalShaderBytecodeCooker::GetPluginSpecificCacheKeySuffix() const
{
	FString CompilerVersion = Job.CompilerVersion;
	FString CompilerPath = GetMetalToolsPath(MetalShaderFormatToLegacyShaderPlatform(Job.ShaderFormat));

	uint64 BuildVersion = 0;
	uint32 XcodeVersion = GetXcodeVersion(BuildVersion);
	
	// PCHs need the modifiction time (in secs. since UTC Epoch) to ensure that the result can be used with the current version of the file
	uint64 ModTime = 0;
	if (Job.bCompileAsPCH)
	{
		ModificationTimeRemoteFile(*Job.InputFile, ModTime);
		
		CompilerVersion += FString::Printf(TEXT("xc%u%llu"), XcodeVersion, BuildVersion);
	}

	FString VersionedName = FString::Printf(TEXT("%s%u%u%llu%s%s%s%s%s%s%s%d%d"), *Job.ShaderFormat.GetPlainNameString(), Job.SourceCRCLen, Job.SourceCRC, ModTime, *Job.Hash.ToString(), *CompilerVersion, *Job.MinOSVersion, *Job.DebugInfo, *Job.MathMode, *Job.Standard, Job.bRetainObjectFile ? TEXT("+O") : TEXT(""), GetTypeHash(CompilerPath), GetTypeHash(Job.Defines));
	// get rid of some not so filename-friendly characters ('=',' ' -> '_')
	VersionedName = VersionedName.Replace(TEXT("="), TEXT("_")).Replace(TEXT(" "), TEXT("_"));

	return VersionedName;
}

bool FMetalShaderBytecodeCooker::IsBuildThreadsafe() const
{
	return false;
}

bool FMetalShaderBytecodeCooker::Build(TArray<uint8>& OutData)
{
	bool bSucceeded = false;

#if PLATFORM_MAC
	// Unset the SDKROOT to avoid problems with the incorrect path being used when compiling with the shared PCH.
	FString SdkRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("SDKROOT"));
	if (SdkRoot.Len() > 0)
	{
		unsetenv("SDKROOT");
	}
#endif

	bool bRemoteBuildingConfigured = IsRemoteBuildingConfigured();

	const FString RemoteFolder = MakeRemoteTempFolder(Job.TmpFolder);
	const FString RemoteInputFile = LocalPathToRemote(Job.InputFile, RemoteFolder);			// Input file to the compiler - Copied from local machine to remote machine
	const FString RemoteInputPCHFile = LocalPathToRemote(Job.InputPCHFile, RemoteFolder);			// Input file to the compiler - Copied from local machine to remote machine
	const FString RemoteObjFile = LocalPathToRemote(Job.OutputObjectFile, RemoteFolder);				// Output from the compiler -> Input file to the archiver
	const FString RemoteOutputFilename = LocalPathToRemote(Job.OutputFile, RemoteFolder);	// Output from the library generator - Copied from remote machine to local machine

	uint32 ShaderPlatform = MetalShaderFormatToLegacyShaderPlatform(Job.ShaderFormat);
	FString MetalPath = GetMetalBinaryPath(ShaderPlatform);
	FString MetalToolsPath = GetMetalToolsPath(ShaderPlatform);
	FString MetalLibPath = MetalToolsPath + TEXT("/metallib");
	
	FString IncludeArgs = Job.IncludeDir.Len() ? FString::Printf(TEXT("-I %s"), *Job.IncludeDir) : TEXT("");
	
	FString MetalParams;
	if (Job.bCompileAsPCH)
	{
		if (Job.InputPCHFile.Len())
		{
			MetalParams = FString::Printf(TEXT("-x metal-header -include-pch %s %s %s %s %s %s %s -o %s"), *Job.InputPCHFile, *Job.MinOSVersion, *Job.MathMode, *Job.Standard, *Job.Defines, *IncludeArgs, *Job.InputFile, *RemoteOutputFilename);
		}
		else
		{
			MetalParams = FString::Printf(TEXT("-x metal-header %s %s %s %s %s %s -o %s"), *Job.MinOSVersion, *Job.MathMode, *Job.Standard, *Job.Defines, *IncludeArgs, *Job.InputFile, *RemoteOutputFilename);
		}
	}
	else
	{
		CopyLocalFileToRemote(Job.InputFile, RemoteInputFile);

		// PCH
		bool bUseSharedPCH = Job.InputPCHFile.Len() && IFileManager::Get().FileExists(*Job.InputPCHFile);
		if (bUseSharedPCH)
        {
            CopyLocalFileToRemote(Job.InputPCHFile, RemoteInputPCHFile);
			MetalParams = FString::Printf(TEXT("-include-pch %s %s %s %s %s -Wno-null-character -fbracket-depth=1024 %s %s %s %s -o %s"), *RemoteInputPCHFile, *Job.MinOSVersion, *Job.DebugInfo, *Job.MathMode, TEXT("-c"), *Job.Standard, *Job.Defines, *IncludeArgs, *RemoteInputFile, *RemoteObjFile);
        }
        else
        {
            MetalParams = FString::Printf(TEXT("%s %s %s %s -Wno-null-character -fbracket-depth=1024 %s %s %s %s -o %s"), *Job.MinOSVersion, *Job.DebugInfo, *Job.MathMode, TEXT("-c"), *Job.Standard, *Job.Defines, *IncludeArgs, *RemoteInputFile, *RemoteObjFile);
        }
	}

	TCHAR const* CompileType = bRemoteBuildingConfigured ? TEXT("remotely") : TEXT("locally");

	bSucceeded = (ExecRemoteProcess(*MetalPath, *MetalParams, &Job.ReturnCode, &Job.Results, &Job.Errors) && Job.ReturnCode == 0);
	if (bSucceeded)
	{
		if (!Job.bCompileAsPCH)
		{
			FString LibraryParams = FString::Printf(TEXT("-o %s %s"), *RemoteOutputFilename, *RemoteObjFile);
			bSucceeded = (ExecRemoteProcess(*MetalLibPath, *LibraryParams, &Job.ReturnCode, &Job.Results, &Job.Errors) && Job.ReturnCode == 0);
			if (bSucceeded)
			{
				if (Job.bRetainObjectFile)
				{
					CopyRemoteFileToLocal(RemoteObjFile, Job.OutputObjectFile);

					bSucceeded = FFileHelper::LoadFileToArray(Output.ObjectFile, *Job.OutputObjectFile);

					if (!bSucceeded)
					{
						Job.Message = FString::Printf(TEXT("Failed to load object file: %s"), *Job.OutputObjectFile);
					}

					RemoveRemoteFile(RemoteObjFile);
				}
			}
			else
			{
				Job.Message = FString::Printf(TEXT("Failed to package into library %s, code: %d, output: %s %s"), CompileType, Job.ReturnCode, *Job.Results, *Job.Errors);
			}
		}

		if (bSucceeded)
		{
			RemoveRemoteFile(RemoteObjFile);
			CopyRemoteFileToLocal(RemoteOutputFilename, Job.OutputFile);
			Output.NativePath = RemoteInputFile;
			bSucceeded = FFileHelper::LoadFileToArray(Output.OutputFile, *Job.OutputFile);

			if (!bSucceeded)
			{
				Job.Message = FString::Printf(TEXT("Failed to load output file: %s"), *Job.OutputFile);
			}
		}
		
		if (!Job.bCompileAsPCH)
		{
			RemoveRemoteFile(RemoteOutputFilename);
		}
	}
	else
	{
		if (Job.bCompileAsPCH)
		{
			Job.Message = FString::Printf(TEXT("Metal Shared PCH generation failed %s to generate %s: %s."), CompileType, *RemoteOutputFilename, *Job.Errors);
		}
		else
		{
			Job.Message = FString::Printf(TEXT("Failed to compile %s to bytecode %s, code: %d, output: %s %s"), CompileType, *RemoteOutputFilename, Job.ReturnCode, *Job.Results, *Job.Errors);
		}
	}

	if (bSucceeded)
	{
		FMemoryWriter Ar(OutData);
		Ar << Output;
	}

#if PLATFORM_MAC
	// Reset the SDKROOT environment we unset earlier.
	if (SdkRoot.Len() > 0)
	{
		setenv("SDKROOT", TCHAR_TO_UTF8(*SdkRoot), 1);
	}
#endif

	return bSucceeded;
}

FMetalShaderOutputCooker::FMetalShaderOutputCooker(const FShaderCompilerInput& _Input, FShaderCompilerOutput& _Output, const FString& _WorkingDirectory, FString _PreprocessedShader, FSHAHash _GUIDHash, uint8 _VersionEnum, uint32 _CCFlags, EHlslCompileTarget _HlslCompilerTarget, EHlslCompileTarget _MetalCompilerTarget, EMetalGPUSemantics _Semantics, EMetalTypeBufferMode _TypeMode, uint32 _MaxUnrollLoops, EHlslShaderFrequency _Frequency, bool _bDumpDebugInfo, FString _Standard, FString _MinOSVersion)
	: Input(_Input)
	, Output(_Output)
	, WorkingDirectory(_WorkingDirectory)
	, PreprocessedShader(_PreprocessedShader)
	, GUIDHash(_GUIDHash)
	, VersionEnum(_VersionEnum)
	, CCFlags(_CCFlags)
	, IABTier(0)
	, HlslCompilerTarget(_HlslCompilerTarget)
	, MetalCompilerTarget(_MetalCompilerTarget)
	, Semantics(_Semantics)
	, TypeMode(_TypeMode)
	, MaxUnrollLoops(_MaxUnrollLoops)
	, Frequency(_Frequency)
	, bDumpDebugInfo(_bDumpDebugInfo)
	, Standard(_Standard)
	, MinOSVersion(_MinOSVersion)
{
	FString const* IABVersion = Input.Environment.GetDefinitions().Find(TEXT("METAL_INDIRECT_ARGUMENT_BUFFERS"));
	if (IABVersion && VersionEnum >= 4)
	{
		if(IABVersion->IsNumeric())
		{
			LexFromString(IABTier, *(*IABVersion));
		}
	}
}

FMetalShaderOutputCooker::~FMetalShaderOutputCooker()
{
}

#if PLATFORM_MAC || PLATFORM_IOS
#pragma mark - FDerivedDataPluginInterface Interface -
#endif

const TCHAR* FMetalShaderOutputCooker::GetPluginName() const
{
	return TEXT("MetalShaderOutput");
}

const TCHAR* FMetalShaderOutputCooker::GetVersionString() const
{
	static FString Version = FString::Printf(TEXT("%u"), (uint32)GetMetalFormatVersion(Input.ShaderFormat));
	return *Version;
}

FString FMetalShaderOutputCooker::GetPluginSpecificCacheKeySuffix() const
{
	FString CachedOutputName;
	{
		FSHAHash Hash;
		FSHA1::HashBuffer(*PreprocessedShader, PreprocessedShader.Len() * sizeof(TCHAR), Hash.Hash);

		uint32 Len = PreprocessedShader.Len();

		uint16 FormatVers = GetMetalFormatVersion(Input.ShaderFormat);

		uint64 Flags = 0;
		for (uint32 Flag : Input.Environment.CompilerFlags)
		{
			Flags |= (1llu << uint64(Flag));
		}

		CachedOutputName = FString::Printf(TEXT("%s-%s_%s-%u_%hu_%llu_%hhu_%d_%s_%s"), *Input.ShaderFormat.GetPlainNameString(), *Input.EntryPointName, *Hash.ToString(), Len, FormatVers, Flags, VersionEnum, IABTier, *GUIDHash.ToString(), *Standard);
	}

	return CachedOutputName;
}

bool FMetalShaderOutputCooker::IsBuildThreadsafe() const
{
	return false;
}

struct FMetalShaderOutputMetaData
{
	TArray<uint8> TypedBufferFormats;
	uint32 InvariantBuffers = 0;
	uint32 TypedBuffers = 0;
	uint32 TypedUAVs = 0;
	uint32 ConstantBuffers = 0;
};

bool FMetalShaderOutputCooker::Build(TArray<uint8>& OutData)
{
	Output.bSucceeded = false;

	std::string MetalSource;
	FString MetalErrors;
	
	bool const bZeroInitialise = Input.Environment.CompilerFlags.Contains(CFLAG_ZeroInitialise);
	bool const bBoundsChecks = Input.Environment.CompilerFlags.Contains(CFLAG_BoundsChecking);

	bool bSwizzleSample = false;
	FString const* Swizzle = Input.Environment.GetDefinitions().Find(TEXT("METAL_SWIZZLE_SAMPLES"));
	if (Swizzle)
	{
		LexFromString(bSwizzleSample, *(*Swizzle));
	}

	bool bAllowFastIntriniscs = false;
	FString const* FastIntrinsics = Input.Environment.GetDefinitions().Find(TEXT("METAL_USE_FAST_INTRINSICS"));
	if (FastIntrinsics)
	{
		LexFromString(bAllowFastIntriniscs, *(*FastIntrinsics));
	}

	bool bForceInvariance = false;
	FString const* UsingWPO = Input.Environment.GetDefinitions().Find(TEXT("USES_WORLD_POSITION_OFFSET"));
	if (UsingWPO && FString("1") == *UsingWPO)
	{
		// WPO requires that we make all multiply/sincos instructions invariant :(
		bForceInvariance = true;
	}
	
	FMetalShaderOutputMetaData OutputData;
	FMetalTessellationOutputs Attribs;
	
	uint32 CRCLen = 0;
	uint32 CRC = 0;
	uint32 SourceLen = 0;
	int32 Result = 0;
	
	struct FMetalResourceTableEntry : FResourceTableEntry
	{
		FString Name;
		uint32 Size;
		uint32 SetIndex;
		bool bUsed;
	};
	TMap<FString, TArray<FMetalResourceTableEntry>> IABs;
	
	FString const* UsingTessellationDefine = Input.Environment.GetDefinitions().Find(TEXT("USING_TESSELLATION"));
	bool bUsingTessellation = ((UsingTessellationDefine != nullptr && FString("1") == *UsingTessellationDefine && Frequency == HSF_VertexShader) || Frequency == HSF_HullShader || Frequency == HSF_DomainShader);
	
	// Its going to take a while to get dxc+SPIRV working...
	bool const bUseSC = Input.Environment.CompilerFlags.Contains(CFLAG_ForceDXC);

#if 1
#if PLATFORM_MAC || PLATFORM_WINDOWS
	if (!bUseSC)
#endif
#endif
	{
		FMetalCodeBackend MetalBackEnd(Attribs, CCFlags, MetalCompilerTarget, VersionEnum, Semantics, TypeMode, MaxUnrollLoops, bZeroInitialise, bBoundsChecks, bAllowFastIntriniscs, bForceInvariance, bSwizzleSample);
		FMetalLanguageSpec MetalLanguageSpec(VersionEnum);

		FHlslCrossCompilerContext CrossCompilerContext(CCFlags, Frequency, HlslCompilerTarget);
		if (CrossCompilerContext.Init(TCHAR_TO_ANSI(*Input.VirtualSourceFilePath), &MetalLanguageSpec))
		{
			char* MetalShaderSource = NULL;
			char* ErrorLog = NULL;

			Result = CrossCompilerContext.Run(
                TCHAR_TO_ANSI(*PreprocessedShader),
				TCHAR_TO_ANSI(*Input.EntryPointName),
				&MetalBackEnd,
				&MetalShaderSource,
				&ErrorLog
            ) ? 1 : 0;
			
			OutputData.TypedBufferFormats = MetalBackEnd.TypedBufferFormats;
			OutputData.InvariantBuffers = MetalBackEnd.InvariantBuffers;
			OutputData.TypedBuffers = MetalBackEnd.TypedBuffers;
			OutputData.TypedUAVs = MetalBackEnd.TypedUAVs;
			OutputData.ConstantBuffers = MetalBackEnd.ConstantBuffers;
			
			CRCLen = MetalShaderSource ? (uint32)FCStringAnsi::Strlen(MetalShaderSource) : 0u;
			CRC = CRCLen ? FCrc::MemCrc_DEPRECATED(MetalShaderSource, CRCLen) : 0u;
			SourceLen = CRCLen;
			if (MetalShaderSource)
			{
				ANSICHAR* Main = FCStringAnsi::Strstr(MetalShaderSource, "Main_00000000_00000000");
				check(Main);
				
				ANSICHAR MainCRC[24];
				int32 NewLen = FCStringAnsi::Snprintf(MainCRC, 24, "Main_%0.8x_%0.8x", CRCLen, CRC);
				FMemory::Memcpy(Main, MainCRC, NewLen);
				
				uint32 Len = FCStringAnsi::Strlen(TCHAR_TO_ANSI(*Input.GetSourceFilename())) + FCStringAnsi::Strlen(TCHAR_TO_ANSI(*Input.DebugGroupName)) + FCStringAnsi::Strlen(TCHAR_TO_ANSI(*Input.EntryPointName)) + FCStringAnsi::Strlen(MetalShaderSource) + 21;
				char* Dest = (char*)malloc(Len);
				FCStringAnsi::Snprintf(Dest, Len, "// ! %s/%s.usf:%s\n%s", (const char*)TCHAR_TO_ANSI(*Input.DebugGroupName), (const char*)TCHAR_TO_ANSI(*Input.GetSourceFilename()), (const char*)TCHAR_TO_ANSI(*Input.EntryPointName), (const char*)MetalShaderSource);
				free(MetalShaderSource);
				MetalShaderSource = Dest;
				SourceLen = (uint32)FCStringAnsi::Strlen(MetalShaderSource);
			}
			
			if (MetalShaderSource)
			{
				MetalSource = MetalShaderSource;
				free(MetalShaderSource);
			}
			if (ErrorLog)
			{
				MetalErrors = ANSI_TO_TCHAR(ErrorLog);
				free(ErrorLog);
			}
		}
	}
#if 1
#if PLATFORM_MAC || PLATFORM_WINDOWS
	else
	{
		ShaderConductor::Compiler::Options Options;
		Options.removeUnusedGlobals = true;
		// Metal is inverted vs. SPIRV for some curious reason
		Options.packMatricesInRowMajor = false;
		Options.enableDebugInfo = false;
		Options.enable16bitTypes = false;
		Options.disableOptimizations = false;
		Options.enableFMAPass = (VersionEnum == 2 || VersionEnum == 3);
		
        ShaderConductor::Compiler::SourceDesc SourceDesc;
		
		std::string SourceData(TCHAR_TO_UTF8(*PreprocessedShader));
		std::string FileName(TCHAR_TO_UTF8(*Input.VirtualSourceFilePath));
		std::string EntryPointName(TCHAR_TO_UTF8(*Input.EntryPointName));
		
        SourceDesc.source = SourceData.c_str();
        SourceDesc.fileName = FileName.c_str();
        SourceDesc.entryPoint = EntryPointName.c_str();
		SourceDesc.numDefines = 0;
		SourceDesc.defines = nullptr;
		
		enum class EMetalTessellationMetadataTags : uint8
		{
			TessellationOutputControlPoints,
			TessellationDomain,
			TessellationInputControlPoints,
			TessellationMaxTessFactor,
			TessellationOutputWinding,
			TessellationPartitioning,
			TessellationPatchesPerThreadGroup,
			TessellationPatchCountBuffer,
			TessellationIndexBuffer,
			TessellationHSOutBuffer,
			TessellationControlPointOutBuffer,
			TessellationHSTFOutBuffer,
			TessellationControlPointIndexBuffer,
			
			Num
		};
		FString TESStrings[(uint8)EMetalTessellationMetadataTags::Num];
        
        switch (Frequency)
        {
            case HSF_VertexShader:
            {
                SourceDesc.stage = ShaderConductor::ShaderStage::VertexShader;
                break;
            }
            case HSF_PixelShader:
            {
				Options.enableFMAPass = false;
                SourceDesc.stage = ShaderConductor::ShaderStage::PixelShader;
                break;
            }
            case HSF_GeometryShader:
            {
                SourceDesc.stage = ShaderConductor::ShaderStage::GeometryShader;
                break;
            }
            case HSF_HullShader:
            {
				Options.enableFMAPass = true;
                SourceDesc.stage = ShaderConductor::ShaderStage::HullShader;
                break;
            }
            case HSF_DomainShader:
            {
                SourceDesc.stage = ShaderConductor::ShaderStage::DomainShader;
                break;
            }
            case HSF_ComputeShader:
            {
				Options.enableFMAPass = false;
                SourceDesc.stage = ShaderConductor::ShaderStage::ComputeShader;
                break;
            }
            default:
            {
                break;
            }
        }
		
		ShaderConductor::Blob* RewriteBlob = nullptr;
		
        // Can't rewrite tessellation shaders because the rewriter doesn't handle HLSL attributes like patchFunction properly and it isn't clear how it should.
        if (!bUsingTessellation)
        {
            ShaderConductor::Compiler::ResultDesc Results = ShaderConductor::Compiler::Rewrite(SourceDesc, Options);
			RewriteBlob = Results.target;
			
			SourceData.clear();
            SourceData.resize(RewriteBlob->Size());
			FCStringAnsi::Strncpy(const_cast<char*>(SourceData.c_str()), (const char*)RewriteBlob->Data(), RewriteBlob->Size());
			
			static const std::string glFragDecl = "Texture2D<float4> gl_LastFragData;";
			size_t glFragDeclIncludePos = SourceData.find(glFragDecl);
			if (glFragDeclIncludePos != std::string::npos)
				SourceData.replace(glFragDeclIncludePos, glFragDecl.length(), "[[vk::input_attachment_index(0)]] SubpassInput<float4> gl_LastFragData;");
			
			static const std::string glFragLoad = "gl_LastFragData.Load(uint3(0, 0, 0), 0)";
			size_t glFragLoadIncludePos = SourceData.find(glFragLoad);
			if (glFragLoadIncludePos != std::string::npos)
				SourceData.replace(glFragLoadIncludePos, glFragLoad.length(), "gl_LastFragData.SubpassLoad()");
			
			SourceDesc.source = SourceData.c_str();
			
            if (bDumpDebugInfo)
            {
                FArchive* FileWriter = IFileManager::Get().CreateFileWriter(*(Input.DumpDebugInfoPath / FPaths::GetBaseFilename(Input.GetSourceFilename()) + TEXT(".dxc.hlsl")));
                if (FileWriter)
                {
                    FileWriter->Serialize(const_cast<void*>(RewriteBlob->Data()), RewriteBlob->Size());
                    FileWriter->Close();
                    delete FileWriter;
                }
            }
        }
		else
		{
			static const std::string glFragDecl = "Texture2D<float4> gl_LastFragData;";
			size_t glFragDeclIncludePos = SourceData.find(glFragDecl);
			if (glFragDeclIncludePos != std::string::npos)
				SourceData.replace(glFragDeclIncludePos, glFragDecl.length(), "[[vk::input_attachment_index(0)]] SubpassInput<float4> gl_LastFragData;");
			
			static const std::string glFragLoad = "gl_LastFragData.Load(uint3(0,0,0),0)";
			size_t glFragLoadIncludePos = SourceData.find(glFragLoad);
			if (glFragLoadIncludePos != std::string::npos)
				SourceData.replace(glFragLoadIncludePos, glFragLoad.length(), "gl_LastFragData.SubpassLoad()");
			
			SourceDesc.source = SourceData.c_str();
		}
		
		Options.removeUnusedGlobals = false;

		ShaderConductor::Compiler::TargetDesc TargetDesc;
		
		FString MetaData = FString::Printf(TEXT("// ! %s/%s.usf:%s\n"), *Input.DebugGroupName, *Input.GetSourceFilename(), *Input.EntryPointName);
		FString EntryPoint = Input.EntryPointName;
		EHlslShaderFrequency Freq = Frequency;
		FString ALNString;
		FString IABString;
		FString UAVString;
		FString SRVString;
		FString SMPString;
		FString UBOString;
		FString GLOString;
		FString PAKString;
		FString INPString;
		FString OUTString;
		FString WKGString;
		uint32 IABOffsetIndex = 0;

		TargetDesc.language = ShaderConductor::ShadingLanguage::SpirV;
		ShaderConductor::Compiler::ResultDesc Results = ShaderConductor::Compiler::Compile(SourceDesc, Options, TargetDesc);
		Result = (Results.hasError) ? 0 : 1;
		uint32 BufferIndices = 0xffffffff;
		if (!Results.hasError)
		{
			// Now perform reflection on the SPIRV and tweak any decorations that we need to.
			// This used to be done via JSON, but that was slow and alloc happy so use SPIRV-Reflect instead.
			spv_reflect::ShaderModule Reflection(Results.target->Size(), Results.target->Data());
			check(Reflection.GetResult() == SPV_REFLECT_RESULT_SUCCESS);
			
			SpvReflectResult SPVRResult = SPV_REFLECT_RESULT_NOT_READY;
			uint32 Count = 0;
			TArray<SpvReflectDescriptorBinding*> Bindings;
			TSet<SpvReflectDescriptorBinding*> Counters;
			TArray<SpvReflectInterfaceVariable*> InputVars;
			TArray<SpvReflectInterfaceVariable*> OutputVars;
			TArray<SpvReflectBlockVariable*> ConstantBindings;
			TArray<SpvReflectExecutionMode*> ExecutionModes;
			
			uint8 UAVIndices = 0xff;
			uint64 TextureIndices = 0xffffffffffffffff;
			uint32 SamplerIndices = 0xffffffff;
			
			TArray<FString> TableNames;
			TMap<FString, FMetalResourceTableEntry> ResourceTable;
			if (IABTier >= 1)
			{
				for (auto Pair : Input.Environment.ResourceTableLayoutHashes)
				{
					TableNames.Add(*Pair.Key);
				}
				
				for (auto Pair : Input.Environment.ResourceTableMap)
				{
					const FResourceTableEntry& Entry = Pair.Value;
					TArray<FMetalResourceTableEntry>& Resources = IABs.FindOrAdd(Entry.UniformBufferName);
					if ((uint32)Resources.Num() <= Entry.ResourceIndex)
					{
						Resources.SetNum(Entry.ResourceIndex + 1);
					}
					FMetalResourceTableEntry NewEntry;
					NewEntry.UniformBufferName = Entry.UniformBufferName;
					NewEntry.Type = Entry.Type;
					NewEntry.ResourceIndex = Entry.ResourceIndex;
					NewEntry.Name = Pair.Key;
					NewEntry.Size = 1;
					NewEntry.bUsed = false;
					Resources[Entry.ResourceIndex] = NewEntry;
				}
				
				for (uint32 i = 0; i < (uint32)TableNames.Num(); )
				{
					if (!IABs.Contains(TableNames[i]))
					{
						TableNames.RemoveAt(i);
					}
					else
					{
						i++;
					}
				}
				
				for (auto Pair : IABs)
				{
					uint32 Index = 0;
					for (uint32 i = 0; i < (uint32)Pair.Value.Num(); i++)
					{
						FMetalResourceTableEntry& Entry = Pair.Value[i];
						switch(Entry.Type)
						{
							case UBMT_UAV:
							case UBMT_RDG_TEXTURE_UAV:
							case UBMT_RDG_BUFFER_UAV:
								Entry.ResourceIndex = Index;
								Entry.Size = 1;
								Index += 2;
								break;
							default:
								Entry.ResourceIndex = Index;
								Index++;
								break;
						}
						for (uint32 j = 0; j < (uint32)TableNames.Num(); j++)
						{
							if (Entry.UniformBufferName == TableNames[j])
							{
								Entry.SetIndex = j;
								break;
							}
						}
						ResourceTable.Add(Entry.Name, Entry);
					}
				}
			}
			
			if(Frequency == HSF_HullShader)
			{
				uint32 PatchSize = 0;
				size_t ThreadSizeIdx = SourceData.find("InputPatch<");
				check(ThreadSizeIdx != std::string::npos);
				char const* String = SourceData.c_str() + ThreadSizeIdx;
				
#if !PLATFORM_WINDOWS
				size_t Found = sscanf(String, "InputPatch< %*s ,  %u >", &PatchSize);
#else
				size_t Found = sscanf_s(String, "InputPatch< %*s ,  %u >", &PatchSize);
#endif
				check(Found == 1);
				
				TESStrings[(uint8)EMetalTessellationMetadataTags::TessellationInputControlPoints] = FString::Printf(TEXT("// @TessellationInputControlPoints: %u\n"), PatchSize);
				
				ThreadSizeIdx = SourceData.find("[maxtessfactor(");
				check(ThreadSizeIdx != std::string::npos);
				String = SourceData.c_str() + ThreadSizeIdx;
				
				uint32 MaxTessFactor = 0;
#if !PLATFORM_WINDOWS
				Found = sscanf(String, "[maxtessfactor(%u)]", &MaxTessFactor);
#else
				Found = sscanf_s(String, "[maxtessfactor(%u)]", &MaxTessFactor);
#endif
				check(Found == 1);
				
				TESStrings[(uint8)EMetalTessellationMetadataTags::TessellationMaxTessFactor] = FString::Printf(TEXT("// @TessellationMaxTessFactor: %u\n"), MaxTessFactor);
				
				TESStrings[(uint8)EMetalTessellationMetadataTags::TessellationPatchesPerThreadGroup] = TEXT("// @TessellationPatchesPerThreadGroup: 1\n");
			}
			
			{
				Count = 0;
				SPVRResult = Reflection.EnumerateExecutionModes(&Count, nullptr);
				check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
				ExecutionModes.SetNum(Count);
				SPVRResult = Reflection.EnumerateExecutionModes(&Count, ExecutionModes.GetData());
				check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
				for (uint32 i = 0; i < Count; i++)
				{
					auto* Mode = ExecutionModes[i];
					switch (Mode->mode) {
						case SpvExecutionModeLocalSize:
						case SpvExecutionModeLocalSizeHint:
							if (Frequency == HSF_ComputeShader)
							{
								check(Mode->operands_count == 3);
								for (uint32 j = 0; j < Mode->operands_count; j++)
								{
									WKGString += FString::Printf(TEXT("%s%u"), WKGString.Len() ? TEXT(", ") : TEXT(""), Mode->operands[j]);
								}
							}
							break;
						case SpvExecutionModeTriangles:
							if (Frequency == HSF_HullShader || Frequency == HSF_DomainShader)
							{
								TESStrings[(uint8)EMetalTessellationMetadataTags::TessellationDomain] = TEXT("// @TessellationDomain: tri\n");
								Attribs.HSTFOutSize = 8; // sizeof(MTLTriangleTessellationFactorsHalf);
							}
							break;
						case SpvExecutionModeQuads:
							if (Frequency == HSF_HullShader || Frequency == HSF_DomainShader)
							{
								TESStrings[(uint8)EMetalTessellationMetadataTags::TessellationDomain] = TEXT("// @TessellationDomain: quad\n");
								Attribs.HSTFOutSize = 12; // sizeof(MTLQuadTessellationFactorsHalf);
							}
							break;
						case SpvExecutionModeIsolines:
							if (Frequency == HSF_HullShader || Frequency == HSF_DomainShader)
							{
								check(0); // Not supported by Metal
							}
							break;
						case SpvExecutionModeOutputVertices:
							if (Frequency == HSF_HullShader || Frequency == HSF_DomainShader)
							{
								check(Mode->operands_count == 1);
								TESStrings[(uint8)EMetalTessellationMetadataTags::TessellationOutputControlPoints] = FString::Printf(TEXT("// @TessellationOutputControlPoints: %d\n"), Mode->operands[0]);
							}
							break;
						case SpvExecutionModeVertexOrderCw:
							if (Frequency == HSF_HullShader || Frequency == HSF_DomainShader)
							{
								TESStrings[(uint8)EMetalTessellationMetadataTags::TessellationOutputWinding] = TEXT("// @TessellationOutputWinding: cw\n");
							}
							break;
						case SpvExecutionModeVertexOrderCcw:
							if (Frequency == HSF_HullShader || Frequency == HSF_DomainShader)
							{
								TESStrings[(uint8)EMetalTessellationMetadataTags::TessellationOutputWinding] = TEXT("// @TessellationOutputWinding: ccw\n");
							}
							break;
						case SpvExecutionModeSpacingEqual:
							if (Frequency == HSF_HullShader || Frequency == HSF_DomainShader)
							{
								TESStrings[(uint8)EMetalTessellationMetadataTags::TessellationPartitioning] = TEXT("// @TessellationPartitioning: integer\n");
							}
							break;
						case SpvExecutionModeSpacingFractionalEven:
							if (Frequency == HSF_HullShader || Frequency == HSF_DomainShader)
							{
								TESStrings[(uint8)EMetalTessellationMetadataTags::TessellationPartitioning] = TEXT("// @TessellationPartitioning: fractional_even\n");
							}
							break;
						case SpvExecutionModeSpacingFractionalOdd:
							if (Frequency == HSF_HullShader || Frequency == HSF_DomainShader)
							{
								TESStrings[(uint8)EMetalTessellationMetadataTags::TessellationPartitioning] = TEXT("// @TessellationPartitioning: fractional_odd\n");
							}
							break;
						case SpvExecutionModeInvocations:
						case SpvExecutionModePixelCenterInteger:
						case SpvExecutionModeOriginUpperLeft:
						case SpvExecutionModeOriginLowerLeft:
						case SpvExecutionModeEarlyFragmentTests:
						case SpvExecutionModePointMode:
						case SpvExecutionModeXfb:
						case SpvExecutionModeDepthReplacing:
						case SpvExecutionModeDepthGreater:
						case SpvExecutionModeDepthLess:
						case SpvExecutionModeDepthUnchanged:
						case SpvExecutionModeInputPoints:
						case SpvExecutionModeInputLines:
						case SpvExecutionModeInputLinesAdjacency:
						case SpvExecutionModeInputTrianglesAdjacency:
						case SpvExecutionModeOutputPoints:
						case SpvExecutionModeOutputLineStrip:
						case SpvExecutionModeOutputTriangleStrip:
						case SpvExecutionModeVecTypeHint:
						case SpvExecutionModeContractionOff:
						case SpvExecutionModeInitializer:
						case SpvExecutionModeFinalizer:
						case SpvExecutionModeSubgroupSize:
						case SpvExecutionModeSubgroupsPerWorkgroup:
						case SpvExecutionModeSubgroupsPerWorkgroupId:
						case SpvExecutionModeLocalSizeId:
						case SpvExecutionModeLocalSizeHintId:
						case SpvExecutionModePostDepthCoverage:
						case SpvExecutionModeStencilRefReplacingEXT:
							break;
							
						default:
							break;
					}
				}
			}
			
			Count = 0;
			SPVRResult = Reflection.EnumerateDescriptorBindings(&Count, nullptr);
			check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
			Bindings.SetNum(Count);
			SPVRResult = Reflection.EnumerateDescriptorBindings(&Count, Bindings.GetData());
			check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
			if (Count > 0)
			{
				TArray<SpvReflectDescriptorBinding*> ResourceBindings;
				TArray<SpvReflectDescriptorBinding*> ArgumentBindings;
				TArray<SpvReflectDescriptorBinding*> UniformBindings;
				TArray<SpvReflectDescriptorBinding*> SamplerBindings;
				TArray<SpvReflectDescriptorBinding*> TextureSRVBindings;
				TArray<SpvReflectDescriptorBinding*> TextureUAVBindings;
				TArray<SpvReflectDescriptorBinding*> TBufferSRVBindings;
				TArray<SpvReflectDescriptorBinding*> TBufferUAVBindings;
				TArray<SpvReflectDescriptorBinding*> SBufferSRVBindings;
				TArray<SpvReflectDescriptorBinding*> SBufferUAVBindings;
				TSet<FString> UsedSets;
				
				// Extract all the bindings first so that we process them in order - this lets us assign UAVs before other resources
				// Which is necessary to match the D3D binding scheme.
				for (auto const& Binding : Bindings)
				{
					if (Binding->resource_type != SPV_REFLECT_RESOURCE_FLAG_CBV && ResourceTable.Contains(UTF8_TO_TCHAR(Binding->name)))
					{
						ResourceBindings.Add(Binding);
						
						FMetalResourceTableEntry Entry = ResourceTable.FindRef(UTF8_TO_TCHAR(Binding->name));
						UsedSets.Add(Entry.UniformBufferName);
						
						continue;
					}
					
					switch(Binding->resource_type)
					{
						case SPV_REFLECT_RESOURCE_FLAG_CBV:
						{
							check(Binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
							if (Binding->accessed)
							{
								if (TableNames.Contains(UTF8_TO_TCHAR(Binding->name)))
								{
									ArgumentBindings.Add(Binding);
								}
								else
								{
									UniformBindings.Add(Binding);
								}
							}
							break;
						}
						case SPV_REFLECT_RESOURCE_FLAG_SAMPLER:
						{
							check(Binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER);
							if (Binding->accessed)
							{
								SamplerBindings.Add(Binding);
							}
							break;
						}
						case SPV_REFLECT_RESOURCE_FLAG_SRV:
						{
							switch(Binding->descriptor_type)
							{
								case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
								{
									if (Binding->accessed)
									{
										TextureSRVBindings.Add(Binding);
									}
									break;
								}
								case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
								{
									if (Binding->accessed)
									{
										TBufferSRVBindings.Add(Binding);
									}
									break;
								}
								case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
								{
									if (Binding->accessed)
									{
										SBufferSRVBindings.Add(Binding);
									}
									break;
								}
								default:
								{
									// check(false);
									break;
								}
							}
							break;
						}
						case SPV_REFLECT_RESOURCE_FLAG_UAV:
						{
							if (Binding->uav_counter_binding)
							{
								Counters.Add(Binding->uav_counter_binding);
							}
							
							switch(Binding->descriptor_type)
							{
								case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
								{
									TextureUAVBindings.Add(Binding);
									break;
								}
								case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
								{
									TBufferUAVBindings.Add(Binding);
									break;
								}
								case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
								{
									if (!Counters.Contains(Binding) || Binding->accessed)
									{
										SBufferUAVBindings.Add(Binding);
									}
									break;
								}
								default:
								{
									// check(false);
									break;
								}
							}
							break;
						}
						default:
						{
							// check(false);
							break;
						}
					}
				}
				
				for (uint32 i = 0; i < (uint32)TableNames.Num(); )
				{
					if (UsedSets.Contains(TableNames[i]))
					{
						IABs.FindChecked(TableNames[i])[0].SetIndex = i;
						i++;
					}
					else
					{
						IABs.Remove(TableNames[i]);
						TableNames.RemoveAt(i);
					}
				}
				
				for (uint32 i = 0; i < (uint32)ArgumentBindings.Num(); )
				{
					FString Name = UTF8_TO_TCHAR(ArgumentBindings[i]->name);
					if (TableNames.Contains(Name))
					{
						auto* ResourceArray = IABs.Find(Name);
						auto const& LastResource = ResourceArray->Last();
						uint32 ResIndex = LastResource.ResourceIndex + LastResource.Size;
						uint32 SetIndex = SPV_REFLECT_SET_NUMBER_DONT_CHANGE;
						for (uint32 j = 0; j < (uint32)TableNames.Num(); j++)
						{
							if (Name == TableNames[j])
							{
								SetIndex = j;
								break;
							}
						}
						
						FMetalResourceTableEntry Entry;
						Entry.UniformBufferName = LastResource.UniformBufferName;
						Entry.Name = Name;
						Entry.ResourceIndex = ResIndex;
						Entry.SetIndex = SetIndex;
						Entry.bUsed = true;
						
						ResourceArray->Add(Entry);
						ResourceTable.Add(Name, Entry);
						
						ResourceBindings.Add(ArgumentBindings[i]);
						
						i++;
					}
					else
					{
						UniformBindings.Add(ArgumentBindings[i]);
						ArgumentBindings.RemoveAt(i);
					}
				}
				
				uint32 GlobalSetId = 32;
				
				for (auto const& Binding : TBufferUAVBindings)
				{
					check(UAVIndices);
					uint32 Index = FPlatformMath::CountTrailingZeros(UAVIndices);
					
					// UAVs always claim all slots so we don't have conflicts as D3D expects 0-7
					BufferIndices &= ~(1 << Index);
					TextureIndices &= ~(1llu << uint64(Index));
					UAVIndices &= ~(1 << Index);
					
					OutputData.TypedUAVs |= (1 << Index);
					OutputData.TypedBuffers |= (1 << Index);
					
					UAVString += FString::Printf(TEXT("%s%s(%u:%u)"), UAVString.Len() ? TEXT(",") : TEXT(""), UTF8_TO_TCHAR(Binding->name), Index, 1);
					
					SPVRResult = Reflection.ChangeDescriptorBindingNumbers(Binding, Index, GlobalSetId);
					check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
				}
				
				for (auto const& Binding : SBufferUAVBindings)
				{
					check(UAVIndices);
					uint32 Index = FPlatformMath::CountTrailingZeros(UAVIndices);
					
					// UAVs always claim all slots so we don't have conflicts as D3D expects 0-7
					BufferIndices &= ~(1 << Index);
					TextureIndices &= ~(1llu << uint64(Index));
					UAVIndices &= ~(1 << Index);
					
					OutputData.InvariantBuffers |= (1 << Index);
					
					UAVString += FString::Printf(TEXT("%s%s(%u:%u)"), UAVString.Len() ? TEXT(",") : TEXT(""), UTF8_TO_TCHAR(Binding->name), Index, 1);
					
					SPVRResult = Reflection.ChangeDescriptorBindingNumbers(Binding, Index, GlobalSetId);
					check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
				}
				
				for (auto const& Binding : TextureUAVBindings)
				{
					check(UAVIndices);
					uint32 Index = FPlatformMath::CountTrailingZeros(UAVIndices);
					
					// UAVs always claim all slots so we don't have conflicts as D3D expects 0-7
					// For texture2d this allows us to emulate atomics with buffers
					BufferIndices &= ~(1 << Index);
					TextureIndices &= ~(1llu << uint64(Index));
					UAVIndices &= ~(1 << Index);
					
					UAVString += FString::Printf(TEXT("%s%s(%u:%u)"), UAVString.Len() ? TEXT(",") : TEXT(""), UTF8_TO_TCHAR(Binding->name), Index, 1);
					
					SPVRResult = Reflection.ChangeDescriptorBindingNumbers(Binding, Index, GlobalSetId);
					check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
				}
				
				IABOffsetIndex = FPlatformMath::CountTrailingZeros(BufferIndices);
				
				TMap<FString, uint32> IABTier1Index;
				if (IABTier == 1)
				{
					for (auto const& Binding : ResourceBindings)
					{
						FMetalResourceTableEntry* Entry = ResourceTable.Find(UTF8_TO_TCHAR(Binding->name));
						auto* ResourceArray = IABs.Find(Entry->UniformBufferName);
						if (!IABTier1Index.Contains(Entry->UniformBufferName))
						{
							IABTier1Index.Add(Entry->UniformBufferName, 0);
						}
						if (Binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER)
						{
							bool bFoundBufferSizes = false;
							for (auto& Resource : *ResourceArray)
							{
								if (Resource.ResourceIndex == 65535)
								{
									bFoundBufferSizes = true;
									break;
								}
							}
							if (!bFoundBufferSizes)
							{
								FMetalResourceTableEntry BufferSizes;
								BufferSizes.UniformBufferName = Entry->UniformBufferName;
								BufferSizes.Name = TEXT("BufferSizes");
								BufferSizes.Type = UBMT_SRV;
								BufferSizes.ResourceIndex = 65535;
								BufferSizes.SetIndex = Entry->SetIndex;
								BufferSizes.Size = 1;
								BufferSizes.bUsed = true;
								ResourceArray->Insert(BufferSizes, 0);
								IABTier1Index[Entry->UniformBufferName] = 1;
							}
						}
					}
				}
				
				for (auto const& Binding : ResourceBindings)
				{
					FMetalResourceTableEntry* Entry = ResourceTable.Find(UTF8_TO_TCHAR(Binding->name));
					
					for (uint32 j = 0; j < (uint32)TableNames.Num(); j++)
					{
						if (Entry->UniformBufferName == TableNames[j])
						{
							Entry->SetIndex = j;
							BufferIndices &= ~(1 << (j + IABOffsetIndex));
							TextureIndices &= ~(1llu << uint64(j + IABOffsetIndex));
							break;
						}
					}
					Entry->bUsed = true;
					
					auto* ResourceArray = IABs.Find(Entry->UniformBufferName);
					uint32 ResourceIndex = Entry->ResourceIndex;
					if (IABTier == 1)
					{
						for (auto& Resource : *ResourceArray)
						{
							Resource.SetIndex = Entry->SetIndex;
							if (Resource.ResourceIndex == Entry->ResourceIndex)
							{
								uint32& Tier1Index = IABTier1Index.FindChecked(Entry->UniformBufferName);
								ResourceIndex = Tier1Index++;
								Resource.bUsed = true;
								break;
							}
						}
						if (Entry->ResourceIndex != 65535)
						{
							SPVRResult = Reflection.ChangeDescriptorBindingNumbers(Binding, ResourceIndex, Entry->SetIndex);
							check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
						}
					}
					else
					{
						for (auto& Resource : *ResourceArray)
						{
							if (Resource.Name == Entry->Name)
							{
								Resource.SetIndex = Entry->SetIndex;
								Resource.bUsed = true;
								break;
							}
						}
                        SPVRResult = Reflection.ChangeDescriptorBindingNumbers(Binding, Entry->ResourceIndex + 1, Entry->SetIndex);
                        check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
					}
				}
				
				for (auto const& Pair : IABs)
				{
					FString Name = Pair.Key;
					auto const& ResourceArray = Pair.Value;
					if (IABString.Len())
					{
						IABString += TEXT(",");
					}
					uint32 SetIndex = ResourceArray[0].SetIndex + IABOffsetIndex;
					IABString += FString::Printf(TEXT("%d["), SetIndex);
					bool bComma = false;
					for (auto const& Resource : ResourceArray)
					{
						if (Resource.bUsed)
						{
							if (bComma)
							{
								IABString += TEXT(",");
							}
							IABString += FString::Printf(TEXT("%u"), (Resource.ResourceIndex == 65535 ? 0 : Resource.ResourceIndex + 1));
							bComma = true;
						}
					}
					IABString += TEXT("]");
					UBOString += FString::Printf(TEXT("%s%s(%u)"), UBOString.Len() ? TEXT(",") : TEXT(""), *Name, SetIndex);
				}
				
				for (auto const& Binding : TBufferSRVBindings)
				{
					check(TextureIndices);
					uint32 Index = FPlatformMath::CountTrailingZeros(TextureIndices);
					
					// No support for 3-component types in dxc/SPIRV/MSL - need to expose my workarounds there too
					BufferIndices &= ~(1 << Index);
					TextureIndices &= ~(1llu << uint64(Index));
					
					OutputData.TypedBuffers |= (1 << Index);
					
					SRVString += FString::Printf(TEXT("%s%s(%u:%u)"), SRVString.Len() ? TEXT(",") : TEXT(""), UTF8_TO_TCHAR(Binding->name), Index, 1);
					
					SPVRResult = Reflection.ChangeDescriptorBindingNumbers(Binding, Index, GlobalSetId);
					check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
				}
				
				for (auto const& Binding : SBufferSRVBindings)
				{
					check(BufferIndices);
					uint32 Index = FPlatformMath::CountTrailingZeros(BufferIndices);
					
					BufferIndices &= ~(1 << Index);
					
					OutputData.InvariantBuffers |= (1 << Index);
					
					SRVString += FString::Printf(TEXT("%s%s(%u:%u)"), SRVString.Len() ? TEXT(",") : TEXT(""), UTF8_TO_TCHAR(Binding->name), Index, 1);
					
					SPVRResult = Reflection.ChangeDescriptorBindingNumbers(Binding, Index, GlobalSetId);
					check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
				}
				
				for (auto const& Binding : UniformBindings)
				{
					check(BufferIndices);
					uint32 Index = FPlatformMath::CountTrailingZeros(BufferIndices);
					BufferIndices &= ~(1 << Index);
					
					OutputData.ConstantBuffers |= (1 << Index);
					
					// Global uniform buffer - handled specially as we care about the internal layout
					if (strstr(Binding->name, "$Globals"))
					{
						TCBDMARangeMap CBRanges;
						GLOString = FString::Printf(TEXT("Globals(%u): "), Index);
						
						FString MbrString;
						for (uint32 i = 0; i < Binding->block.member_count; i++)
						{
							SpvReflectBlockVariable& member = Binding->block.members[i];
							uint32 MbrOffset = member.absolute_offset / sizeof(float);
							uint32 MbrSize = member.size / sizeof(float);
							
							MbrString += FString::Printf(TEXT("%s%s(%u,%u)"), MbrString.Len() > 0 ? TEXT(",") : TEXT(""), UTF8_TO_TCHAR(member.name), MbrOffset, MbrSize);
							
							unsigned DestCBPrecision = TEXT('h');
							unsigned SourceOffset = MbrOffset;
							unsigned DestOffset = MbrOffset;
							unsigned DestSize = MbrSize;
							unsigned DestCBIndex = 0;
							InsertRange(CBRanges, Index, SourceOffset, DestSize, DestCBIndex, DestCBPrecision, DestOffset);
						}
						
						GLOString += MbrString;
						
						for (auto Iter = CBRanges.begin(); Iter != CBRanges.end(); ++Iter)
						{
							TDMARangeList& List = Iter->second;
							for (auto IterList = List.begin(); IterList != List.end(); ++IterList)
							{
								check(IterList->DestCBIndex == 0);
								PAKString += FString::Printf(TEXT("%s%u:%u-%c:%u:%u"), PAKString.Len() ? TEXT(",") : TEXT(""), IterList->SourceCB, IterList->SourceOffset, IterList->DestCBPrecision, IterList->DestOffset, IterList->Size);
							}
						}
					}
					else
					{
						// Regular uniform buffer - we only care about the binding index
						UBOString += FString::Printf(TEXT("%s%s(%u)"), UBOString.Len() ? TEXT(",") : TEXT(""), UTF8_TO_TCHAR(Binding->name), Index);
					}
					
					SPVRResult = Reflection.ChangeDescriptorBindingNumbers(Binding, Index, GlobalSetId);
					check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
				}
				
				for (auto const& Binding : TextureSRVBindings)
				{
					check(TextureIndices);
					uint32 Index = FPlatformMath::CountTrailingZeros64(TextureIndices);
					TextureIndices &= ~(1llu << uint64(Index));
					
					SRVString += FString::Printf(TEXT("%s%s(%u:%u)"), SRVString.Len() ? TEXT(",") : TEXT(""), UTF8_TO_TCHAR(Binding->name), Index, 1);
					
					SPVRResult = Reflection.ChangeDescriptorBindingNumbers(Binding, Index, GlobalSetId);
					check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
				}
				
				for (auto const& Binding : SamplerBindings)
				{
					check(SamplerIndices);
					uint32 Index = FPlatformMath::CountTrailingZeros(SamplerIndices);
					SamplerIndices &= ~(1 << Index);
					
					SMPString += FString::Printf(TEXT("%s%u:%s"), SMPString.Len() ? TEXT(",") : TEXT(""), Index, UTF8_TO_TCHAR(Binding->name));
					
					SPVRResult = Reflection.ChangeDescriptorBindingNumbers(Binding, Index, GlobalSetId);
					check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
				}
			}
			
			if (Frequency == HSF_PixelShader)
			{
				Count = 0;
				SPVRResult = Reflection.EnumerateOutputVariables(&Count, nullptr);
				check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
				OutputVars.SetNum(Count);
				SPVRResult = Reflection.EnumerateOutputVariables(&Count, OutputVars.GetData());
				check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
				if (Count > 0)
				{
					for (auto const& Var : OutputVars)
					{
						if (Var->storage_class == SpvStorageClassOutput && Var->built_in == -1 && strstr(Var->name, "SV_Target"))
						{
							FString TypeQualifier;
							
							auto const type = *Var->type_description;
							uint32_t masked_type = type.type_flags & 0xF;
							
							switch (masked_type) {
								default: checkf(false, TEXT("unsupported component type %d"), masked_type); break;
								case SPV_REFLECT_TYPE_FLAG_BOOL  : TypeQualifier = TEXT("b"); break;
								case SPV_REFLECT_TYPE_FLAG_INT   : TypeQualifier = (type.traits.numeric.scalar.signedness ? TEXT("i") : TEXT("u")); break;
								case SPV_REFLECT_TYPE_FLAG_FLOAT : TypeQualifier = (type.traits.numeric.scalar.width == 32 ? TEXT("f") : TEXT("h")); break;
							}
							
							if (type.type_flags & SPV_REFLECT_TYPE_FLAG_MATRIX)
							{
								TypeQualifier += FString::Printf(TEXT("%d%d"), type.traits.numeric.matrix.row_count, type.traits.numeric.matrix.column_count);
							}
							else if (type.type_flags & SPV_REFLECT_TYPE_FLAG_VECTOR)
							{
								TypeQualifier += FString::Printf(TEXT("%d"), type.traits.numeric.vector.component_count);
							}
							else
							{
								TypeQualifier += TEXT("1");
							}
							
							OUTString += FString::Printf(TEXT("%s%s:SV_Target%d"), OUTString.Len() ? TEXT(",") : TEXT(""), *TypeQualifier, Var->location);
						}
					}
				}
			}
			
			if (Frequency == HSF_VertexShader)
			{
				Count = 0;
				SPVRResult = Reflection.EnumerateInputVariables(&Count, nullptr);
				check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
				InputVars.SetNum(Count);
				SPVRResult = Reflection.EnumerateInputVariables(&Count, InputVars.GetData());
				check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
				if (Count > 0)
				{
					uint32 AssignedInputs = 0;
					
					for (auto const& Var : InputVars)
					{
						if (Var->storage_class == SpvStorageClassInput && Var->built_in == -1)
						{
							unsigned Location = Var->location;
							unsigned SemanticIndex = Location;
							check(Var->semantic);
							unsigned i = (unsigned)strlen(Var->semantic);
							check(i);
							while (isdigit((unsigned char)(Var->semantic[i-1])))
							{
								i--;
							}
							if (i < strlen(Var->semantic))
							{
								SemanticIndex = (unsigned)atoi(Var->semantic + i);
								if (Location != SemanticIndex)
								{
									Location = SemanticIndex;
								}
							}
							
							while ((1 << Location) & AssignedInputs)
							{
								Location++;
							}
							
							if (Location != Var->location)
							{
								SPVRResult = Reflection.ChangeInputVariableLocation(Var, Location);
								check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
							}
							
							uint32 ArrayCount = 1;
							for (uint32 Dim = 0; Dim < Var->array.dims_count; Dim++)
							{
								ArrayCount *= Var->array.dims[Dim];
							}
							
							FString TypeQualifier;

							auto const type = *Var->type_description;
							uint32_t masked_type = type.type_flags & 0xF;
							
							switch (masked_type) {
								default: checkf(false, TEXT("unsupported component type %d"), masked_type); break;
								case SPV_REFLECT_TYPE_FLAG_BOOL  : TypeQualifier = TEXT("b"); break;
								case SPV_REFLECT_TYPE_FLAG_INT   : TypeQualifier = (type.traits.numeric.scalar.signedness ? TEXT("i") : TEXT("u")); break;
								case SPV_REFLECT_TYPE_FLAG_FLOAT : TypeQualifier = (type.traits.numeric.scalar.width == 32 ? TEXT("f") : TEXT("h")); break;
							}
							
							if (type.type_flags & SPV_REFLECT_TYPE_FLAG_MATRIX)
							{
								TypeQualifier += FString::Printf(TEXT("%d%d"), type.traits.numeric.matrix.row_count, type.traits.numeric.matrix.column_count);
							}
							else if (type.type_flags & SPV_REFLECT_TYPE_FLAG_VECTOR)
							{
								TypeQualifier += FString::Printf(TEXT("%d"), type.traits.numeric.vector.component_count);
							}
							else
							{
								TypeQualifier += TEXT("1");
							}
							
							for (uint32 j = 0; j < ArrayCount; j++)
							{
								AssignedInputs |= (1 << (Location + j));
								
								INPString += FString::Printf(TEXT("%s%s:in_ATTRIBUTE%d"), INPString.Len() ? TEXT(",") : TEXT(""), *TypeQualifier, (Location + j));
							}
						}
					}
				}
			}
			
			if (Frequency == HSF_VertexShader && bUsingTessellation)
			{
				Count = 0;
				SPVRResult = Reflection.EnumerateOutputVariables(&Count, nullptr);
				check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
				OutputVars.SetNum(Count);
				SPVRResult = Reflection.EnumerateOutputVariables(&Count, OutputVars.GetData());
				check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
				if (Count > 0)
				{
					
					OutputVars.Sort([](SpvReflectInterfaceVariable& LHS, SpvReflectInterfaceVariable& RHS)  { return LHS.location < RHS.location; });
					uint32 Offset = 0;
					uint32 InitialAlign = 0;
					for (auto const& Var : OutputVars)
					{
						if (Var->storage_class == SpvStorageClassOutput && Var->built_in == -1)
						{
							unsigned Location = Var->location;
							
							uint32 ArrayCount = 1;
							for (uint32 Dim = 0; Dim < Var->array.dims_count; Dim++)
							{
								ArrayCount *= Var->array.dims[Dim];
							}
							
							auto const type = *Var->type_description;
							uint32_t masked_type = type.type_flags & 0xF;
							
							for (uint32 j = 0; j < ArrayCount; j++)
							{
								FMetalAttribute& Attrib = Attribs.HSOut.AddDefaulted_GetRef();
								Attrib.Index = Location + j;
								uint32 ElementSize = 0;
								switch (masked_type) {
									default: checkf(false, TEXT("unsupported component type %d"), masked_type); break;
									case SPV_REFLECT_TYPE_FLAG_BOOL  : Attrib.Type = EMetalComponentType::Bool; ElementSize = 1; break;
									case SPV_REFLECT_TYPE_FLAG_INT   : Attrib.Type = (type.traits.numeric.scalar.signedness ? EMetalComponentType::Int : EMetalComponentType::Uint); ElementSize = 4; break;
									case SPV_REFLECT_TYPE_FLAG_FLOAT : Attrib.Type = (type.traits.numeric.scalar.width == 32 ? EMetalComponentType::Float : EMetalComponentType::Half); if(type.traits.numeric.scalar.width == 32) { ElementSize = 4; } else { ElementSize = 2; } break;
								}
								if (type.type_flags & SPV_REFLECT_TYPE_FLAG_MATRIX)
								{
									Attrib.Components = (type.traits.numeric.matrix.row_count * type.traits.numeric.matrix.column_count);
								}
								else if (type.type_flags & SPV_REFLECT_TYPE_FLAG_VECTOR)
								{
									Attrib.Components = (type.traits.numeric.vector.component_count);
								}
								else
								{
									Attrib.Components = 1;
								}
								
								char const* NumberedSemantic = strstr(Var->name, Var->semantic);
								FString Semantic = FString::Printf(TEXT("%s_%u"), UTF8_TO_TCHAR(NumberedSemantic), j);
								Attrib.Semantic = GetTypeHash(Semantic);
								
								uint32 AlignedComponents = Attrib.Components == 3 ? 4 : Attrib.Components;
								InitialAlign = InitialAlign == 0 ? ElementSize * AlignedComponents : InitialAlign;
								Offset = Align(Offset, ElementSize * AlignedComponents);
								Attrib.Offset = Offset;
								Offset += ElementSize * AlignedComponents;

								if (Input.Environment.CompilerFlags.Contains(CFLAG_KeepDebugInfo))
									ALNString += FString::Printf(TEXT("%sHSOut.%s(%u)"), ALNString.Len() ? TEXT(",") : TEXT(""), *Semantic, Attrib.Offset);
							}
						}
					}
					Attribs.HSOutSize = Align(Offset, 16);
					
					if (Input.Environment.CompilerFlags.Contains(CFLAG_KeepDebugInfo))
						ALNString += FString::Printf(TEXT(" = HSOutSize(%u)"), Attribs.HSOutSize);
				}
			}
			
			if (Frequency == HSF_HullShader)
			{
				Count = 0;
				SPVRResult = Reflection.EnumerateInputVariables(&Count, nullptr);
				check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
				InputVars.SetNum(Count);
				SPVRResult = Reflection.EnumerateInputVariables(&Count, InputVars.GetData());
				check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
				if (Count > 0)
				{
					
					InputVars.Sort([](SpvReflectInterfaceVariable& LHS, SpvReflectInterfaceVariable& RHS)  { return LHS.location < RHS.location; });
					uint32 Offset = 0;
					for (auto const& Var : InputVars)
					{
						if (Var->storage_class == SpvStorageClassInput && Var->built_in == -1)
						{
							unsigned Location = Var->location;
							
							uint32 ArrayCount = 1;
							for (uint32 Dim = 1; Dim < Var->array.dims_count; Dim++)
							{
								ArrayCount *= Var->array.dims[Dim];
							}
							
							auto const type = *Var->type_description;
							uint32_t masked_type = type.type_flags & 0xF;
							
							for (uint32 j = 0; j < ArrayCount; j++)
							{
								FMetalAttribute& Attrib = Attribs.HSIn.AddDefaulted_GetRef();
								Attrib.Index = Location + j;
								uint32 ElementSize = 0;
								switch (masked_type) {
									default: checkf(false, TEXT("unsupported component type %d"), masked_type); break;
									case SPV_REFLECT_TYPE_FLAG_BOOL  : Attrib.Type = EMetalComponentType::Bool; ElementSize = 1; break;
									case SPV_REFLECT_TYPE_FLAG_INT   : Attrib.Type = (type.traits.numeric.scalar.signedness ? EMetalComponentType::Int : EMetalComponentType::Uint); ElementSize = 4; break;
									case SPV_REFLECT_TYPE_FLAG_FLOAT : Attrib.Type = (type.traits.numeric.scalar.width == 32 ? EMetalComponentType::Float : EMetalComponentType::Half); if(type.traits.numeric.scalar.width == 32) { ElementSize = 4; } else { ElementSize = 2; } break;
								}
								if (type.type_flags & SPV_REFLECT_TYPE_FLAG_MATRIX)
								{
									Attrib.Components = (type.traits.numeric.matrix.row_count * type.traits.numeric.matrix.column_count);
								}
								else if (type.type_flags & SPV_REFLECT_TYPE_FLAG_VECTOR)
								{
									Attrib.Components = (type.traits.numeric.vector.component_count);
								}
								else
								{
									Attrib.Components = 1;
								}
								
								char const* NumberedSemantic = strstr(Var->name, Var->semantic);
								FString Semantic = FString::Printf(TEXT("%s_%u"), UTF8_TO_TCHAR(NumberedSemantic), j);
								Attrib.Semantic = GetTypeHash(Semantic);
								
								uint32 AlignedComponents = Attrib.Components == 3 ? 4 : Attrib.Components;
								Offset = Align(Offset, ElementSize * AlignedComponents);
								Attrib.Offset = Offset;
								Offset += ElementSize * AlignedComponents;
								
								if (Input.Environment.CompilerFlags.Contains(CFLAG_KeepDebugInfo))
									ALNString += FString::Printf(TEXT("%sHSIn.%s(%u)"), ALNString.Len() ? TEXT(",") : TEXT(""), *Semantic, Attrib.Offset);
							}
						}
					}
				}
				
				Count = 0;
				SPVRResult = Reflection.EnumerateOutputVariables(&Count, nullptr);
				check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
				OutputVars.SetNum(Count);
				SPVRResult = Reflection.EnumerateOutputVariables(&Count, OutputVars.GetData());
				check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
				if (Count > 0)
				{
					uint32 HSOutOffset = 0;
					uint32 PatchControlPointOffset = 0;
					uint32 HSOutAlign = 0;
					uint32 PatchControlPointAlign = 0;
					
					OutputVars.Sort([](SpvReflectInterfaceVariable& LHS, SpvReflectInterfaceVariable& RHS)  { return LHS.location < RHS.location; });
					
					for (auto const& Var : OutputVars)
					{
						if (Var->storage_class == SpvStorageClassOutput && Var->built_in == -1)
						{
							unsigned Location = Var->location;
							uint32 ArrayCount = 1;
							for (uint32 Dim = 1; Dim < Var->array.dims_count; Dim++)
							{
								ArrayCount *= Var->array.dims[Dim];
							}
							
							auto const type = *Var->type_description;
							uint32_t masked_type = type.type_flags & 0xF;
							
							for (uint32 j = 0; j < ArrayCount; j++)
							{
								FMetalAttribute& Attrib = Var->array.dims_count ? Attribs.PatchControlPointOut.AddDefaulted_GetRef() : Attribs.HSOut.AddDefaulted_GetRef();
								Attrib.Index = Location + j;
								uint32 ElementSize = 0;
								switch (masked_type) {
									default: checkf(false, TEXT("unsupported component type %d"), masked_type); break;
									case SPV_REFLECT_TYPE_FLAG_BOOL  : Attrib.Type = EMetalComponentType::Bool; ElementSize = 1; break;
									case SPV_REFLECT_TYPE_FLAG_INT   : Attrib.Type = (type.traits.numeric.scalar.signedness ? EMetalComponentType::Int : EMetalComponentType::Uint); ElementSize = 4; break;
									case SPV_REFLECT_TYPE_FLAG_FLOAT : Attrib.Type = (type.traits.numeric.scalar.width == 32 ? EMetalComponentType::Float : EMetalComponentType::Half); if(type.traits.numeric.scalar.width == 32) { ElementSize = 4; } else { ElementSize = 2; } break;
								}
								if (type.type_flags & SPV_REFLECT_TYPE_FLAG_MATRIX)
								{
									Attrib.Components = (type.traits.numeric.matrix.row_count * type.traits.numeric.matrix.column_count);
								}
								else if (type.type_flags & SPV_REFLECT_TYPE_FLAG_VECTOR)
								{
									Attrib.Components = (type.traits.numeric.vector.component_count);
								}
								else
								{
									Attrib.Components = 1;
								}
								
								uint32& Offset = Var->array.dims_count ? PatchControlPointOffset : HSOutOffset;
								uint32& InitialAlign = Var->array.dims_count ? PatchControlPointAlign : HSOutAlign;
								TCHAR const* Name = Var->array.dims_count ? TEXT("PatchOut") : TEXT("HSOut");
								
								char const* NumberedSemantic = strstr(Var->name, Var->semantic);
								FString Semantic = FString::Printf(TEXT("%s_%u"), UTF8_TO_TCHAR(NumberedSemantic), j);
								Attrib.Semantic = GetTypeHash(Semantic);
								
								uint32 AlignedComponents = Attrib.Components == 3 ? 4 : Attrib.Components;
								InitialAlign = InitialAlign == 0 ? ElementSize * AlignedComponents : InitialAlign;
								Offset = Align(Offset, ElementSize * AlignedComponents);
								Attrib.Offset = Offset;
								Offset += ElementSize * AlignedComponents;
								
								if (Input.Environment.CompilerFlags.Contains(CFLAG_KeepDebugInfo))
									ALNString += FString::Printf(TEXT("%s%s.%s(%u)"), ALNString.Len() ? TEXT(",") : TEXT(""), Name, *Semantic, Attrib.Offset);
							}
						}
					}
					Attribs.HSOutSize = Align(HSOutOffset, 16);
					Attribs.PatchControlPointOutSize = Align(PatchControlPointOffset, 16);
					
					if (Input.Environment.CompilerFlags.Contains(CFLAG_KeepDebugInfo))
						ALNString += FString::Printf(TEXT(" = HSOutSize(%u), PatchControlPointOutSize(%u)"), Attribs.HSOutSize, Attribs.PatchControlPointOutSize);
				}
			}
			
			if (Frequency == HSF_DomainShader)
			{
				Count = 0;
				SPVRResult = Reflection.EnumerateInputVariables(&Count, nullptr);
				check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
				InputVars.SetNum(Count);
				SPVRResult = Reflection.EnumerateInputVariables(&Count, InputVars.GetData());
				check(SPVRResult == SPV_REFLECT_RESULT_SUCCESS);
				if (Count > 0)
				{
					uint32 HSOutOffset = 0;
					uint32 PatchControlPointOffset = 0;
					uint32 HSOutAlign = 0;
					uint32 PatchControlPointAlign = 0;
					
					InputVars.Sort([](SpvReflectInterfaceVariable& LHS, SpvReflectInterfaceVariable& RHS)  { return LHS.location < RHS.location; });
					for (auto const& Var : InputVars)
					{
						if (Var->storage_class == SpvStorageClassInput && Var->built_in == -1)
						{
							unsigned Location = Var->location;
							
							uint32 ArrayCount = 1;
							for (uint32 Dim = 1; Dim < Var->array.dims_count; Dim++)
							{
								ArrayCount *= Var->array.dims[Dim];
							}
							
							auto const type = *Var->type_description;
							uint32_t masked_type = type.type_flags & 0xF;
							
							for (uint32 j = 0; j < ArrayCount; j++)
							{
								FMetalAttribute& Attrib = Var->array.dims_count ? Attribs.PatchControlPointOut.AddDefaulted_GetRef() : Attribs.HSOut.AddDefaulted_GetRef();
								Attrib.Index = Location + j;
								uint32 ElementSize = 0;
								switch (masked_type) {
									default: checkf(false, TEXT("unsupported component type %d"), masked_type); break;
									case SPV_REFLECT_TYPE_FLAG_BOOL  : Attrib.Type = EMetalComponentType::Bool; ElementSize = 1; break;
									case SPV_REFLECT_TYPE_FLAG_INT   : Attrib.Type = (type.traits.numeric.scalar.signedness ? EMetalComponentType::Int : EMetalComponentType::Uint); ElementSize = 4; break;
									case SPV_REFLECT_TYPE_FLAG_FLOAT : Attrib.Type = (type.traits.numeric.scalar.width == 32 ? EMetalComponentType::Float : EMetalComponentType::Half); if(type.traits.numeric.scalar.width == 32) { ElementSize = 4; } else { ElementSize = 2; } break;
								}
								if (type.type_flags & SPV_REFLECT_TYPE_FLAG_MATRIX)
								{
									Attrib.Components = (type.traits.numeric.matrix.row_count * type.traits.numeric.matrix.column_count);
								}
								else if (type.type_flags & SPV_REFLECT_TYPE_FLAG_VECTOR)
								{
									Attrib.Components = (type.traits.numeric.vector.component_count);
								}
								else
								{
									Attrib.Components = 1;
								}
								
								uint32& Offset = Var->array.dims_count ? PatchControlPointOffset : HSOutOffset;
								uint32& InitialAlign = Var->array.dims_count ? PatchControlPointAlign : HSOutAlign;
								TCHAR const* Name = Var->array.dims_count ? TEXT("PatchOut") : TEXT("HSOut");
								
								char const* NumberedSemantic = strstr(Var->name, Var->semantic);
								FString Semantic = FString::Printf(TEXT("%s_%u"), UTF8_TO_TCHAR(NumberedSemantic), j);
								Attrib.Semantic = GetTypeHash(Semantic);
								
								uint32 AlignedComponents = Attrib.Components == 3 ? 4 : Attrib.Components;
								InitialAlign = InitialAlign == 0 ? ElementSize * AlignedComponents : InitialAlign;
								Offset = Align(Offset, ElementSize * AlignedComponents);
								Attrib.Offset = Offset;
								Offset += ElementSize * AlignedComponents;
								
								if (Input.Environment.CompilerFlags.Contains(CFLAG_KeepDebugInfo))
									ALNString += FString::Printf(TEXT("%s%s.%s(%u)"), ALNString.Len() ? TEXT(",") : TEXT(""), Name, *Semantic, Attrib.Offset);
							}
						}
					}
					Attribs.HSOutSize = Align(HSOutOffset, 16);
					Attribs.PatchControlPointOutSize = Align(PatchControlPointOffset, 16);
					
					if (Input.Environment.CompilerFlags.Contains(CFLAG_KeepDebugInfo))
						ALNString += FString::Printf(TEXT(" = HSOutSize(%u), PatchControlPointOutSize(%u)"), Attribs.HSOutSize, Attribs.PatchControlPointOutSize);
				}
			}
			
			ShaderConductor::Blob* OldData = Results.target;
			Results.target = ShaderConductor::CreateBlob(Reflection.GetCode(), Reflection.GetCodeSize());
			
			FArchive* FileWriter = IFileManager::Get().CreateFileWriter(*(Input.DumpDebugInfoPath / FPaths::GetBaseFilename(Input.GetSourceFilename()) + TEXT(".dxc.spirv")));
			if (FileWriter)
			{
				FileWriter->Serialize((void*)Reflection.GetCode(), Reflection.GetCodeSize());
				FileWriter->Close();
				delete FileWriter;
			}
			
			ShaderConductor::DestroyBlob(OldData);
		}
		
		uint32 SideTableIndex = 0;
		uint32 OutputBufferIndex = 0;
		uint32 IndirectParamsIndex = 0;
		uint32 PatchBufferIndex = 0;
		uint32 TessFactorBufferIndex = 0;
		uint32 HullIndexBuffer = 0;
		if (Result)
		{
			ShaderConductor::DestroyBlob(Results.errorWarningMsg);
			Results.errorWarningMsg = nullptr;
			
			TargetDesc.language = ShaderConductor::ShadingLanguage::Msl;
			
			SideTableIndex = FPlatformMath::CountTrailingZeros(BufferIndices);
			char BufferIdx[3];
			FCStringAnsi::Snprintf(BufferIdx, 3, "%d", SideTableIndex);
			BufferIndices &= ~(1 << SideTableIndex);

			ShaderConductor::MacroDefine Defines[16] =
			{
				{"texel_buffer_texture_width", "0"},
				{"enforce_storge_buffer_bounds", "1"},
				{"buffer_size_buffer_index", BufferIdx},
				{"invariant_float_math", Options.enableFMAPass ? "1" : "0"},
				{"enable_decoration_binding","1"},
			};
			
			TargetDesc.numOptions = 5;
			TargetDesc.options = &Defines[0];
			switch(Semantics)
			{
				case EMetalGPUSemanticsImmediateDesktop:
					TargetDesc.platform = "macOS";
					break;
				case EMetalGPUSemanticsTBDRDesktop:
					TargetDesc.platform = "iOS";
					Defines[TargetDesc.numOptions++] = { "ios_support_base_vertex_instance", "1" };
					Defines[TargetDesc.numOptions++] = { "ios_use_framebuffer_fetch_subpasses", "1" };
					Defines[TargetDesc.numOptions++] = { "emulate_cube_array", "1" };
					break;
				case EMetalGPUSemanticsMobile:
				default:
					TargetDesc.platform = "iOS";
					Defines[TargetDesc.numOptions++] = { "ios_use_framebuffer_fetch_subpasses", "1" };
					Defines[TargetDesc.numOptions++] = { "emulate_cube_array", "1" };
					break;
			}
			
			if (Frequency == HSF_VertexShader && bUsingTessellation)
			{
				Defines[TargetDesc.numOptions++] = { "capture_output_to_buffer", "1" };
			}
			
			char OutputBufferIdx[3];
			char IndirectParamsIdx[3];
			if (Frequency == HSF_VertexShader && bUsingTessellation)
			{
				OutputBufferIndex = FPlatformMath::CountTrailingZeros(BufferIndices);
				BufferIndices &= ~(1 << OutputBufferIndex);
				FCStringAnsi::Snprintf(OutputBufferIdx, 3, "%u", OutputBufferIndex);
				Defines[TargetDesc.numOptions++] = { "shader_output_buffer_index", OutputBufferIdx };
				
				IndirectParamsIndex = FPlatformMath::CountTrailingZeros(BufferIndices);
				BufferIndices &= ~(1 << IndirectParamsIndex);
				FCStringAnsi::Snprintf(IndirectParamsIdx, 3, "%u", IndirectParamsIndex);
				Defines[TargetDesc.numOptions++] = { "indirect_params_buffer_index", IndirectParamsIdx };
				
				TESStrings[(uint8)EMetalTessellationMetadataTags::TessellationHSOutBuffer] = FString::Printf(TEXT("// @TessellationHSOutBuffer: %u\n"), OutputBufferIndex);
				TESStrings[(uint8)EMetalTessellationMetadataTags::TessellationPatchCountBuffer] = FString::Printf(TEXT("// @TessellationPatchCountBuffer: %u\n"), IndirectParamsIndex);
			}
			
			if (Frequency == HSF_DomainShader)
			{
				OutputBufferIndex = FPlatformMath::CountTrailingZeros(BufferIndices);
				BufferIndices &= ~(1 << OutputBufferIndex);
				
				IndirectParamsIndex = FPlatformMath::CountTrailingZeros(BufferIndices);
				BufferIndices &= ~(1 << IndirectParamsIndex);
				
				TESStrings[(uint8)EMetalTessellationMetadataTags::TessellationHSOutBuffer] = FString::Printf(TEXT("// @TessellationHSOutBuffer: %u\n"), OutputBufferIndex);
				TESStrings[(uint8)EMetalTessellationMetadataTags::TessellationControlPointOutBuffer] = FString::Printf(TEXT("// @TessellationControlPointOutBuffer: %u\n"), IndirectParamsIndex);
			}
			
			char PatchBufferIdx[3];
			char TessFactorBufferIdx[3];
			if (Frequency == HSF_HullShader)
			{
				OutputBufferIndex = FPlatformMath::CountTrailingZeros(BufferIndices);
				BufferIndices &= ~(1 << OutputBufferIndex);
				FCStringAnsi::Snprintf(OutputBufferIdx, 3, "%u", OutputBufferIndex);
				Defines[TargetDesc.numOptions++] = { "shader_output_buffer_index", OutputBufferIdx };
				
				IndirectParamsIndex = FPlatformMath::CountTrailingZeros(BufferIndices);
				BufferIndices &= ~(1 << IndirectParamsIndex);
				FCStringAnsi::Snprintf(IndirectParamsIdx, 3, "%u", IndirectParamsIndex);
				Defines[TargetDesc.numOptions++] = { "indirect_params_buffer_index", IndirectParamsIdx };
				
				PatchBufferIndex = FPlatformMath::CountTrailingZeros(BufferIndices);
				BufferIndices &= ~(1 << PatchBufferIndex);
				FCStringAnsi::Snprintf(PatchBufferIdx, 3, "%u", PatchBufferIndex);
				Defines[TargetDesc.numOptions++] = { "shader_patch_output_buffer_index", PatchBufferIdx };
				
				TessFactorBufferIndex = FPlatformMath::CountTrailingZeros(BufferIndices);
				BufferIndices &= ~(1 << TessFactorBufferIndex);
				FCStringAnsi::Snprintf(TessFactorBufferIdx, 3, "%u", TessFactorBufferIndex);
				Defines[TargetDesc.numOptions++] = { "shader_tess_factor_buffer_index", TessFactorBufferIdx };
				Defines[TargetDesc.numOptions++] = { "shader_input_wg_index", "0" };

				HullIndexBuffer = FPlatformMath::CountTrailingZeros(BufferIndices);
				BufferIndices &= ~(1 << HullIndexBuffer);
				
				uint32 HullVertexBuffer = FPlatformMath::CountTrailingZeros(BufferIndices);
				BufferIndices &= ~(1 << HullVertexBuffer);
				
				TESStrings[(uint8)EMetalTessellationMetadataTags::TessellationHSOutBuffer] = FString::Printf(TEXT("// @TessellationHSOutBuffer: %u\n"), PatchBufferIndex);
				TESStrings[(uint8)EMetalTessellationMetadataTags::TessellationPatchCountBuffer] = FString::Printf(TEXT("// @TessellationPatchCountBuffer: %u\n"), IndirectParamsIndex);
				TESStrings[(uint8)EMetalTessellationMetadataTags::TessellationControlPointIndexBuffer] = FString::Printf(TEXT("// @TessellationControlPointIndexBuffer: %u\n"), HullVertexBuffer);
				TESStrings[(uint8)EMetalTessellationMetadataTags::TessellationHSTFOutBuffer] = FString::Printf(TEXT("// @TessellationHSTFOutBuffer: %u\n"), TessFactorBufferIndex);
				TESStrings[(uint8)EMetalTessellationMetadataTags::TessellationControlPointOutBuffer] = FString::Printf(TEXT("// @TessellationControlPointOutBuffer: %u\n"), OutputBufferIndex);
				TESStrings[(uint8)EMetalTessellationMetadataTags::TessellationIndexBuffer] = FString::Printf(TEXT("// @TessellationIndexBuffer: %u\n"), HullIndexBuffer);
			}
			
			char ArgumentBufferOffset[3];
			switch (VersionEnum)
			{
				case 6:
				case 5:
				case 4:
				{
					if (IABTier >= 1)
					{
						FCStringAnsi::Snprintf(ArgumentBufferOffset, 3, "%u", IABOffsetIndex);
						Defines[TargetDesc.numOptions++] = { "argument_buffers", "1" };
						Defines[TargetDesc.numOptions++] = { "argument_buffer_offset", ArgumentBufferOffset };
					}
					Defines[TargetDesc.numOptions++] = { "texture_buffer_native", "1" };
					TargetDesc.version = "20100";
					break;
				}
				case 3:
				{
					TargetDesc.version = "20000";
					break;
				}
				case 2:
				{
					TargetDesc.version = "10200";
					break;
				}
				case 1:
				{
					TargetDesc.version = "10100";
					break;
				}
				case 0:
				default:
				{
					TargetDesc.version = "10000";
					break;
				}
			}
			
			ShaderConductor::Blob* IRBlob = Results.target;
			
			Results = ShaderConductor::Compiler::ConvertBinary(Results, SourceDesc, TargetDesc);
			
			ShaderConductor::DestroyBlob(IRBlob);
		}
		
		Result = (Results.hasError) ? 0 : 1;
		if (!Results.hasError && Results.target)
		{
			MetaData += TEXT("// Compiled by ShaderConductor\n");
			if (INPString.Len())
			{
				MetaData += FString::Printf(TEXT("// @Inputs: %s\n"), *INPString);
			}
			if (OUTString.Len())
			{
				MetaData += FString::Printf(TEXT("// @Outputs: %s\n"), *OUTString);
			}
			if (UBOString.Len())
			{
				MetaData += FString::Printf(TEXT("// @UniformBlocks: %s\n"), *UBOString);
			}
			if (GLOString.Len())
			{
				MetaData += FString::Printf(TEXT("// @PackedUB: %s\n"), *GLOString);
			}
			if (PAKString.Len())
			{
				MetaData += FString::Printf(TEXT("// @PackedUBGlobalCopies: %s\n"), *PAKString);
			}
			if (SRVString.Len())
			{
				MetaData += FString::Printf(TEXT("// @Samplers: %s\n"), *SRVString);
			}
			if (UAVString.Len())
			{
				MetaData += FString::Printf(TEXT("// @UAVs: %s\n"), *UAVString);
			}
			if (SMPString.Len())
			{
				MetaData += FString::Printf(TEXT("// @SamplerStates: %s\n"), *SMPString);
			}
			if (WKGString.Len())
			{
				MetaData += FString::Printf(TEXT("// @NumThreads: %s\n"), *WKGString);
			}
			
			
			if(strstr((const char*)Results.target->Data(), "spvBufferSizeConstants"))
			{
				MetaData += FString::Printf(TEXT("// @SideTable: spvBufferSizeConstants(%d)\n"), SideTableIndex);
			}
			if (IABString.Len())
			{
				MetaData += TEXT("// @ArgumentBuffers: ");
				MetaData += IABString;
				MetaData += TEXT("\n");
			}
			FString TESString;
			for (uint32 i = 0; i < (uint32)EMetalTessellationMetadataTags::Num; i++)
			{
				TESString += TESStrings[i];
			}
			if (TESString.Len())
			{
				MetaData += TESString;
			}
			MetaData += TEXT("\n\n");
			if (ALNString.Len())
			{
				MetaData += TEXT("// Attributes: ");
				MetaData += ALNString;
				MetaData += TEXT("\n\n");
			}
			
			MetalSource = TCHAR_TO_UTF8(*MetaData);
			MetalSource += std::string((const char*)Results.target->Data(), Results.target->Size());
			
			// Tessellation vertex & hull shaders must always use FMA
			if (Options.enableFMAPass)
			{
				std::string FMADefine = std::string("\n"
										"template<typename T>\n"
										"static inline __attribute__((always_inline))\n"
										"T ue4_cross(T x, T y)\n"
										"{\n"
                                        "    metal::float3 fx = metal::float3(x);\n"
                                        "    metal::float3 fy = metal::float3(y);\n"
										"    return T(metal::fma(fx[1], fy[2], -metal::fma(fy[1], fx[2], 0.0)), metal::fma(fx[2], fy[0], -metal::fma(fy[2], fx[0], 0.0)), metal::fma(fx[0], fy[1], -metal::fma(fy[0], fx[1], 0.0)));\n"
										"}\n"
										"#define cross ue4_cross\n\n"
										"using namespace metal;"
										);
				
				std::string IncludeString = "using namespace metal;";
				size_t IncludePos = MetalSource.find(IncludeString);
				if (IncludePos != std::string::npos)
					MetalSource.replace(IncludePos, IncludeString.length(), FMADefine);
			}
			
			if (Frequency == HSF_DomainShader)
			{
				uint32 PatchSize = 0;
				size_t ThreadSizeIdx = SourceData.find("OutputPatch<");
				check(ThreadSizeIdx != std::string::npos);
				char const* String = SourceData.c_str() + ThreadSizeIdx;
				while(!isdigit((unsigned char)*String))
				{
					String++;
				}
#if !PLATFORM_WINDOWS
				size_t Found = sscanf(String, "%u", &PatchSize);
#else
				size_t Found = sscanf_s(String, "%u", &PatchSize);
#endif
				check(Found == 1);
				
				std::string PatchAttr;
				std::string PatchSearch = "[[ patch(triangle, 0) ]]";
				size_t PatchIdx = MetalSource.find(PatchSearch);
				if (PatchIdx != std::string::npos)
				{
					PatchAttr = "[[ patch(triangle, ";
				}
				else
				{
					PatchSearch = "[[ patch(quad, 0) ]]";
					PatchIdx = MetalSource.find(PatchSearch);
					PatchAttr = "[[ patch(quad, ";
				}
				if (PatchIdx != std::string::npos)
				{
					char BufferIdx[3];
					FCStringAnsi::Snprintf(BufferIdx, 3, "%d", PatchSize);
					PatchAttr += BufferIdx;
					PatchAttr += ") ]]";
					MetalSource.replace(PatchIdx, PatchSearch.length(), PatchAttr);
				}
				
				CRCLen = MetalSource.length();
				CRC = FCrc::MemCrc_DEPRECATED(MetalSource.c_str(), CRCLen);

				std::string PatchInputName = EntryPointName + "_patchIn";
				std::string PatchNameDefine = "#define ";
				PatchNameDefine += PatchInputName;
				PatchNameDefine += " ";
				PatchNameDefine += PatchInputName;
				PatchNameDefine += std::to_string(CRC);
				PatchNameDefine += std::to_string(CRCLen);
				PatchNameDefine += "\n";
				
				std::string PatchInput = EntryPointName + "_in";
				std::string PatchDefine = "#define ";
				PatchDefine += PatchInput;
				PatchDefine += " ";
				PatchDefine += PatchInput;
				PatchDefine += std::to_string(CRC);
				PatchDefine += std::to_string(CRCLen);
				PatchDefine += "\n";
				
				size_t Pos = MetalSource.find("#include ");
				MetalSource.insert(Pos, PatchNameDefine);
				MetalSource.insert(Pos, PatchDefine);
			}
			
			CRCLen = MetalSource.length();
			CRC = FCrc::MemCrc_DEPRECATED(MetalSource.c_str(), CRCLen);
			
			ANSICHAR MainCRC[25];
			int32 NewLen = FCStringAnsi::Snprintf(MainCRC, 25, "Main_%0.8x_%0.8x(", CRCLen, CRC);
			
			std::string MainEntryPoint = EntryPointName + "(";
			size_t Pos;
			do
			{
				Pos = MetalSource.find(MainEntryPoint);
				if (Pos != std::string::npos)
					MetalSource.replace(Pos, MainEntryPoint.length(), MainCRC);
			} while(Pos != std::string::npos);
		}
		
		// Version 6 means Tier 2 IABs for now.
		if (IABTier >= 2)
		{
			char BufferIdx[3];
			for (auto& IAB : IABs)
			{
				uint32 Index = IAB.Value[0].SetIndex;
				FMemory::Memzero(BufferIdx);
				FCStringAnsi::Snprintf(BufferIdx, 3, "%d", Index);
				std::string find_str = "struct spvDescriptorSetBuffer";
				find_str += BufferIdx;
				size_t Pos = MetalSource.find(find_str);
				if (Pos != std::string::npos)
				{
					size_t StartPos = MetalSource.find("{", Pos);
					size_t EndPos = MetalSource.find("}", StartPos);
					std::string IABName(TCHAR_TO_UTF8(*IAB.Key));
					size_t UBPos = MetalSource.find("constant type_" + IABName + "*");
					
					std::string Declaration = find_str + "\n{\n\tconstant uint* spvBufferSizeConstants [[id(0)]];\n";
					for (FMetalResourceTableEntry& Entry : IAB.Value)
					{
						std::string EntryString;
						std::string Name(TCHAR_TO_UTF8(*Entry.Name));
						switch(Entry.Type)
						{
							case UBMT_TEXTURE:
							case UBMT_RDG_TEXTURE:
							case UBMT_RDG_TEXTURE_SRV:
							case UBMT_SRV:
							case UBMT_SAMPLER:
							case UBMT_RDG_BUFFER:
							case UBMT_RDG_BUFFER_SRV:
							case UBMT_UAV:
							case UBMT_RDG_TEXTURE_UAV:
							case UBMT_RDG_BUFFER_UAV:
							{
								size_t EntryPos = MetalSource.find(Name + " [[id(");
								if (EntryPos != std::string::npos)
								{
									while(MetalSource[--EntryPos] != '\n') {}
									while(MetalSource[++EntryPos] != '\n')
									{
										EntryString += MetalSource[EntryPos];
									}
									EntryString += "\n";
								}
								else
								{
									switch(Entry.Type)
									{
										case UBMT_TEXTURE:
										case UBMT_RDG_TEXTURE:
										case UBMT_RDG_TEXTURE_SRV:
										case UBMT_SRV:
										{
											std::string typeName = "texture_buffer<float, access::read>";
											int32 NameIndex = PreprocessedShader.Find(Entry.Name + ";");
											int32 DeclIndex = NameIndex;
											if (DeclIndex > 0)
											{
												while(PreprocessedShader[--DeclIndex] != TEXT('\n')) {}
												FString Decl = PreprocessedShader.Mid(DeclIndex, NameIndex - DeclIndex);
												TCHAR const* Types[] = { TEXT("ByteAddressBuffer<"), TEXT("StructuredBuffer<"), TEXT("Buffer<"), TEXT("Texture2DArray"), TEXT("TextureCubeArray"), TEXT("Texture2D"), TEXT("Texture3D"), TEXT("TextureCube") };
												char const* NewTypes[] = { "device void*", "device void*", "texture_buffer<float, access::read>", "texture2d_array<float>", "texturecube_array<float>", "texture2d<float>", "texture3d<float>", "texturecube<float>" };
												for (uint32 i = 0; i < 8; i++)
												{
													if (Decl.Contains(Types[i]))
													{
														typeName = NewTypes[i];
														break;
													}
												}
											}
											
											FCStringAnsi::Snprintf(BufferIdx, 3, "%d", Entry.ResourceIndex + 1);
											EntryString = "\t";
											EntryString += typeName;
											EntryString += " ";
											EntryString += Name;
											EntryString += " [[id(";
											EntryString += BufferIdx;
											EntryString += ")]];\n";
											break;
										}
										case UBMT_SAMPLER:
										{
											FCStringAnsi::Snprintf(BufferIdx, 3, "%d", Entry.ResourceIndex + 1);
											EntryString = "\tsampler ";
											EntryString += Name;
											EntryString += " [[id(";
											EntryString += BufferIdx;
											EntryString += ")]];\n";
											break;
										}
										case UBMT_RDG_BUFFER:
										case UBMT_RDG_BUFFER_SRV:
										{
											FCStringAnsi::Snprintf(BufferIdx, 3, "%d", Entry.ResourceIndex + 1);
											EntryString = "\tdevice void* ";
											EntryString += Name;
											EntryString += " [[id(";
											EntryString += BufferIdx;
											EntryString += ")]];\n";
											break;
										}
										case UBMT_UAV:
										case UBMT_RDG_TEXTURE_UAV:
										{
											std::string typeName = "texture_buffer<float, access::read_write>";
											int32 NameIndex = PreprocessedShader.Find(Entry.Name + ";");
											int32 DeclIndex = NameIndex;
											if (DeclIndex > 0)
											{
												while(PreprocessedShader[--DeclIndex] != TEXT('\n')) {}
												FString Decl = PreprocessedShader.Mid(DeclIndex, NameIndex - DeclIndex);
												TCHAR const* Types[] = { TEXT("ByteAddressBuffer<"), TEXT("StructuredBuffer<"), TEXT("Buffer<"), TEXT("Texture2DArray"), TEXT("TextureCubeArray"), TEXT("Texture2D"), TEXT("Texture3D"), TEXT("TextureCube") };
												char const* NewTypes[] = { "device void*", "device void*", "texture_buffer<float, access::read_write>", "texture2d_array<float, access::read_write>", "texturecube_array<float, access::read_write>", "texture2d<float, access::read_write>", "texture3d<float, access::read_write>", "texturecube<float, access::read_write>" };
												for (uint32 i = 0; i < 8; i++)
												{
													if (Decl.Contains(Types[i]))
													{
														typeName = NewTypes[i];
														break;
													}
												}
											}
											
											FCStringAnsi::Snprintf(BufferIdx, 3, "%d", Entry.ResourceIndex + 1);
											EntryString = "\t";
											EntryString += typeName;
											EntryString += " ";
											EntryString += Name;
											EntryString += " [[id(";
											EntryString += BufferIdx;
											EntryString += ")]];\n";
											
											FCStringAnsi::Snprintf(BufferIdx, 3, "%d", Entry.ResourceIndex + 2);
											EntryString = "\tdevice void* ";
											EntryString += Name;
											EntryString += "_atomic [[id(";
											EntryString += BufferIdx;
											EntryString += ")]];\n";
											break;
										}
										case UBMT_RDG_BUFFER_UAV:
										{
											FCStringAnsi::Snprintf(BufferIdx, 3, "%d", Entry.ResourceIndex + 1);
											EntryString = "\ttexture_buffer<float, access::read_write> ";
											EntryString += Name;
											EntryString += " [[id(";
											EntryString += BufferIdx;
											EntryString += ")]];\n";
											
											FCStringAnsi::Snprintf(BufferIdx, 3, "%d", Entry.ResourceIndex + 2);
											EntryString = "\tdevice void* ";
											EntryString += Name;
											EntryString += "_atomic [[id(";
											EntryString += BufferIdx;
											EntryString += ")]];\n";
											break;
										}
										default:
											break;
									}
								}
								Declaration += EntryString;
								break;
							}
							default:
							{
								break;
							}
						}
					}
					if (UBPos < EndPos)
					{
						size_t UBEnd = MetalSource.find(";", UBPos);
						std::string UBStr = MetalSource.substr(UBPos, (UBEnd - UBPos));
						Declaration += "\t";
						Declaration += UBStr;
						Declaration += ";\n";
					}
					else
					{
						Declaration += "\tconstant void* uniformdata [[id(";
						FMemory::Memzero(BufferIdx);
						FCStringAnsi::Snprintf(BufferIdx, 3, "%d", IAB.Value.Num() + 1);
						Declaration += BufferIdx;
						Declaration += ")]];\n";
					}
					
					Declaration += "}";
					
 					MetalSource.replace(Pos, (EndPos - Pos) + 1, Declaration);
				}
			}
		}
		
		if (Results.errorWarningMsg)
		{
			std::string ErrorText((const char*)Results.errorWarningMsg->Data(), Results.errorWarningMsg->Size());
			
			TArray<FString> ErrorLines;
			MetalErrors = ANSI_TO_TCHAR((char const*)ErrorText.c_str());
			MetalErrors.ParseIntoArray(ErrorLines, TEXT("\n"), true);
			MetalErrors = TEXT("");
			for (int32 LineIndex = 0; LineIndex < ErrorLines.Num(); ++LineIndex)
			{
				MetalErrors += Input.VirtualSourceFilePath + TEXT("(0): ") + ErrorLines[LineIndex] + TEXT("\n");
			}
			ShaderConductor::DestroyBlob(Results.errorWarningMsg);
			Results.errorWarningMsg = nullptr;
		}
		
		if (Results.target)
		{
			ShaderConductor::DestroyBlob(Results.target);
			Results.target = nullptr;
		}
		
		if (RewriteBlob)
		{
			ShaderConductor::DestroyBlob(RewriteBlob);
		}
	}
#endif
#endif

	if (bDumpDebugInfo)
	{
		if (MetalSource.length() > 0u)
		{
			FArchive* FileWriter = IFileManager::Get().CreateFileWriter(*(Input.DumpDebugInfoPath / FPaths::GetBaseFilename(Input.GetSourceFilename()) + TEXT(".metal")));
			if (FileWriter)
			{
				FileWriter->Serialize(const_cast<char*>(MetalSource.c_str()), MetalSource.length());
				FileWriter->Close();
				delete FileWriter;
			}
		}
	}

	if (Result != 0)
	{
		Output.Target = Input.Target;
		BuildMetalShaderOutput(Output, Input, GUIDHash, CCFlags, MetalSource.c_str(), MetalSource.length(), CRCLen, CRC, VersionEnum, *Standard, *MinOSVersion, TypeMode, Output.Errors, Attribs, OutputData.TypedBuffers, OutputData.InvariantBuffers, OutputData.TypedUAVs, OutputData.ConstantBuffers, OutputData.TypedBufferFormats, bAllowFastIntriniscs);

		FMemoryWriter Ar(OutData);
		Ar << Output;
	}
	else
	{
		TArray<FString> ErrorLines;
		MetalErrors.ParseIntoArray(ErrorLines, TEXT("\n"), true);
		bool const bDirectCompile = FParse::Param(FCommandLine::Get(), TEXT("directcompile"));
		for (int32 LineIndex = 0; LineIndex < ErrorLines.Num(); ++LineIndex)
		{
			const FString& Line = ErrorLines[LineIndex];
			Output.Errors.Add(FShaderCompilerError(*Line));
			CrossCompiler::ParseHlslccError(Output.Errors, Line, false);
			if (bDirectCompile)
			{
				UE_LOG(LogShaders, Error, TEXT("%s"), *Line);
			}
		}
	}

	return Output.bSucceeded;
}
