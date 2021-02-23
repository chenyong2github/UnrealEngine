// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	OpenGLShaders.cpp: OpenGL shader RHI implementation.
=============================================================================*/

#include "OpenGLShaders.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "OpenGLDrvPrivate.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "SceneUtils.h"
#include "PsoLruCache.h"

#define CHECK_FOR_GL_SHADERS_TO_REPLACE 0

#if PLATFORM_WINDOWS
#include <mmintrin.h>
#endif
#include "SceneUtils.h"

static TAutoConsoleVariable<int32> CVarEnableLRU(
	TEXT("r.OpenGL.EnableProgramLRUCache"),
	0,
	TEXT("OpenGL program LRU cache.\n")
	TEXT("For use only when driver only supports a limited number of active GL programs.\n")
	TEXT("0: disable LRU. (default)\n")
	TEXT("1: When the LRU cache limits are reached, the least recently used GL program(s) will be deleted to make space for new/more recent programs. Expect hitching if requested shader is not in LRU cache."),
	ECVF_RenderThreadSafe | ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarLRUMaxProgramCount(
	TEXT("r.OpenGL.ProgramLRUCount"),
	700,
	TEXT("OpenGL LRU maximum occupancy.\n")
	TEXT("Limit the maximum number of active shader programs at any one time.\n")
	TEXT("0: disable LRU.\n")
	TEXT("Non-Zero: Maximum number of active shader programs, if reached least, recently used shader programs will deleted. "),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarLRUMaxProgramBinarySize(
	TEXT("r.OpenGL.ProgramLRUBinarySize"),
	35*1024*1024,
	TEXT("OpenGL LRU maximum binary shader size.\n")
	TEXT("Limit the maximum number of active shader programs at any one time.\n")
	TEXT("0: disable LRU. (default)\n")
	TEXT("Non-Zero: Maximum number of bytes active shader programs may use. If reached, least recently used shader programs will deleted."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarStoreCompressedBinaries(
	TEXT("r.OpenGL.StoreCompressedProgramBinaries"),
	0,
	TEXT(""),
	ECVF_ReadOnly | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLRUKeepProgramBinaryResident(
	TEXT("r.OpenGL.ProgramLRUKeepBinaryResident"),
	0,
	TEXT("OpenGL LRU should keep program binary in memory.\n")
	TEXT("Do not discard the program binary after creation of the GL program.\n")
	TEXT("0: Program binary is discarded after GL program creation and recreated on program eviction. (default)\n")
	TEXT("1: Program binary is retained, this improves eviction and re-creation performance but uses more memory."),
	ECVF_ReadOnly |ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarIgnoreLinkFailure(
	TEXT("r.OpenGL.IgnoreLinkFailure"),
	0,
	TEXT("Ignore OpenGL program link failures.\n")
	TEXT("0: Program link failure generates a fatal error when encountered. (default)\n")
	TEXT("1: Ignore link failures. this may allow a program to continue but could lead to undefined rendering behaviour."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarIgnoreShaderCompileFailure(
	TEXT("r.OpenGL.IgnoreShaderCompileFailure"),
	0,
	TEXT("Ignore OpenGL shader compile failures.\n")
	TEXT("0: Shader compile failure return an error when encountered. (default)\n")
	TEXT("1: Ignore Shader compile failures."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarUseExistingBinaryFileCache(
	TEXT("r.OpenGL.UseExistingBinaryFileCache"),
	1,
	TEXT("When generating a new binary cache (such as when Shader Pipeline Cache Version Guid changes) use the existing binary file cache to speed up generation of the new cache.\n")
	TEXT("0: Always rebuild binary file cache when Pipeline Cache Version Guid changes.\n")
	TEXT("1: When Pipeline Cache Version Guid changes re-use programs from the existing binary cache where possible (default)."),
	ECVF_RenderThreadSafe);

static int32 GMaxShaderLibProcessingTimeMS = 10;
static FAutoConsoleVariableRef CVarMaxShaderLibProcessingTime(
	TEXT("r.OpenGL.MaxShaderLibProcessingTime"),
	GMaxShaderLibProcessingTimeMS,
	TEXT("The maximum time per frame to process shader library requests in milliseconds.\n")
	TEXT("default 10ms. Note: Driver compile time for a single program may exceed this limit."),
	ECVF_RenderThreadSafe
);

#if PLATFORM_ANDROID
bool GOpenGLShaderHackLastCompileSuccess = false;
#endif

#define VERIFY_GL_SHADER_LINK 1
#define VERIFY_GL_SHADER_COMPILE 1

static bool ReportShaderCompileFailures()
{
	bool bReportCompileFailures = true;
#if PLATFORM_ANDROID
	const FString * ConfigRulesReportGLShaderCompileFailures = FAndroidMisc::GetConfigRulesVariable(TEXT("ReportGLShaderCompileFailures"));
	bReportCompileFailures = ConfigRulesReportGLShaderCompileFailures == nullptr || ConfigRulesReportGLShaderCompileFailures->Equals("true", ESearchCase::IgnoreCase);
#endif

#if VERIFY_GL_SHADER_COMPILE
	return bReportCompileFailures;
#else
	return false;
#endif
}

static bool ReportProgramLinkFailures()
{
	bool bReportLinkFailures = true;
#if PLATFORM_ANDROID
	const FString* ConfigRulesReportGLProgramLinkFailures = FAndroidMisc::GetConfigRulesVariable(TEXT("ReportGLProgramLinkFailures"));
	bReportLinkFailures = ConfigRulesReportGLProgramLinkFailures == nullptr || ConfigRulesReportGLProgramLinkFailures->Equals("true", ESearchCase::IgnoreCase);
#endif

#if VERIFY_GL_SHADER_LINK
	return bReportLinkFailures;
#else
	return false;
#endif
}


static uint32 GCurrentDriverProgramBinaryAllocation = 0;
static uint32 GNumPrograms = 0;

static void PrintProgramStats()
{
	FPlatformMisc::LowLevelOutputDebugStringf(TEXT(" --- Programs Num: %d, Size: %d \n"), GNumPrograms, GCurrentDriverProgramBinaryAllocation);
}

static FAutoConsoleCommand ConsoleCommandPrintProgramStats(
								TEXT("r.OpenGL.PrintProgramStats"),
								TEXT("Print to log current program binary stats"),
								FConsoleCommandDelegate::CreateStatic(PrintProgramStats)
);

static void SetNewProgramStats(GLuint Program)
{
	VERIFY_GL_SCOPE();

#if STATS | VERIFY_GL_SHADER_LINK
	GLint BinaryLength = 0;
	glGetProgramiv(Program, GL_PROGRAM_BINARY_LENGTH, &BinaryLength);
#endif

#if STATS
	INC_MEMORY_STAT_BY(STAT_OpenGLProgramBinaryMemory, BinaryLength);
	INC_DWORD_STAT(STAT_OpenGLProgramCount);
#endif
	
	GNumPrograms++;
#if VERIFY_GL_SHADER_LINK
	GCurrentDriverProgramBinaryAllocation += BinaryLength;
#endif
}

static void SetDeletedProgramStats(GLuint Program)
{
	VERIFY_GL_SCOPE();
#if STATS | VERIFY_GL_SHADER_LINK
	GLint BinaryLength = 0;
	glGetProgramiv(Program, GL_PROGRAM_BINARY_LENGTH, &BinaryLength);
#endif

#if STATS
	DEC_MEMORY_STAT_BY(STAT_OpenGLProgramBinaryMemory, BinaryLength);
	DEC_DWORD_STAT(STAT_OpenGLProgramCount);
#endif

#if VERIFY_GL_SHADER_LINK
	GCurrentDriverProgramBinaryAllocation -= BinaryLength;
#endif
	GNumPrograms--;
}

// Create any resources that are required by internal ogl rhi functions.
void FOpenGLDynamicRHI::SetupRecursiveResources()
{
	auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	{
		TShaderMapRef<FNULLPS> PixelShader(ShaderMap);
		PixelShader.GetPixelShader();
	}
}

const uint32 SizeOfFloat4 = 16;
const uint32 NumFloatsInFloat4 = 4;

FORCEINLINE void FOpenGLShaderParameterCache::FRange::MarkDirtyRange(uint32 NewStartVector, uint32 NewNumVectors)
{
	if (NumVectors > 0)
	{
		uint32 High = StartVector + NumVectors;
		uint32 NewHigh = NewStartVector + NewNumVectors;
		
		uint32 MaxVector = FMath::Max(High, NewHigh);
		uint32 MinVector = FMath::Min(StartVector, NewStartVector);
		
		StartVector = MinVector;
		NumVectors = (MaxVector - MinVector) + 1;
	}
	else
	{
		StartVector = NewStartVector;
		NumVectors = NewNumVectors;
	}
}

/**
 * Verify that an OpenGL program has linked successfully.
 */
static bool VerifyLinkedProgram(GLuint Program)
{
	SCOPE_CYCLE_COUNTER(STAT_OpenGLShaderLinkVerifyTime);

	GLint LinkStatus = 0;
	glGetProgramiv(Program, GL_LINK_STATUS, &LinkStatus);
	if (LinkStatus != GL_TRUE)
	{
		if (ReportProgramLinkFailures())
		{
			GLenum LastGLError = glGetError();
			GLint LogLength;
			ANSICHAR DefaultLog[] = "No log";
			ANSICHAR *CompileLog = DefaultLog;
			glGetProgramiv(Program, GL_INFO_LOG_LENGTH, &LogLength);
			if (LogLength > 1)
			{
				CompileLog = (ANSICHAR *)FMemory::Malloc(LogLength);
				glGetProgramInfoLog(Program, LogLength, NULL, CompileLog);
			}
			UE_LOG(LogRHI, Error, TEXT("Failed to link program. Current total programs: %d program binary bytes, last gl error 0x%X, drvalloc %d\n  log:\n%s"),
				GNumPrograms,
				LastGLError,
				GCurrentDriverProgramBinaryAllocation,
				ANSI_TO_TCHAR(CompileLog));

			if (LogLength > 1)
			{
				FMemory::Free(CompileLog);
			}
		}
		else
		{
			UE_LOG(LogRHI, Error, TEXT("Failed to link program. Current total programs:%d"), GNumPrograms);
		}
		// if we're required to ignore link failure then we return true here.
		return CVarIgnoreLinkFailure.GetValueOnAnyThread() == 1;
	}
	return true;
}


// Verify a program has created successfully, the non-SSO case will log errors and return back success status.
// TODO: SupportsSeparateShaderObjects case.
static bool VerifyProgramPipeline(GLuint Program)
{
	VERIFY_GL_SCOPE();
	bool bOK = true;
	// Don't try and validate SSOs here - the draw state matters to SSOs and it definitely can't be guaranteed to be valid at this stage
	if ( FOpenGL::SupportsSeparateShaderObjects() )
	{
#if DEBUG_GL_SHADERS
		bOK = FOpenGL::IsProgramPipeline(Program);
#endif
	}
	else
	{
		bOK = VerifyLinkedProgram(Program);
	}
	return bOK;
}

// ============================================================================================================================

class FOpenGLCompiledShaderKey
{
public:
	FOpenGLCompiledShaderKey(
		GLenum InTypeEnum,
		uint32 InCodeSize,
		uint32 InCodeCRC
		)
		: TypeEnum(InTypeEnum)
		, CodeSize(InCodeSize)
		, CodeCRC(InCodeCRC)
	{}

	friend bool operator ==(const FOpenGLCompiledShaderKey& A,const FOpenGLCompiledShaderKey& B)
	{
		return A.TypeEnum == B.TypeEnum && A.CodeSize == B.CodeSize && A.CodeCRC == B.CodeCRC;
	}

	friend uint32 GetTypeHash(const FOpenGLCompiledShaderKey &Key)
	{
		return GetTypeHash(Key.TypeEnum) ^ GetTypeHash(Key.CodeSize) ^ GetTypeHash(Key.CodeCRC);
	}

private:
	GLenum TypeEnum;
	uint32 CodeSize;
	uint32 CodeCRC;
};

struct FLibraryShaderCacheValue
{
	FOpenGLCodeHeader* Header;
	uint32 ShaderCrc;
	GLuint GLShader;
	TArray<FUniformBufferStaticSlot> StaticSlots;

#if DEBUG_GL_SHADERS
	TArray<ANSICHAR> GlslCode;
	const ANSICHAR*  GlslCodeString; // make it easier in VS to see shader code in debug mode; points to begin of GlslCode
#endif
};

typedef TMap<FSHAHash, FLibraryShaderCacheValue> FOpenGLCompiledLibraryShaderCache;

static FOpenGLCompiledLibraryShaderCache& GetOpenGLCompiledLibraryShaderCache()
{
	static FOpenGLCompiledLibraryShaderCache CompiledShaderCache;
	return CompiledShaderCache;
}


typedef TMap<FOpenGLCompiledShaderKey,GLuint> FOpenGLCompiledShaderCache;

static FOpenGLCompiledShaderCache& GetOpenGLCompiledShaderCache()
{
	static FOpenGLCompiledShaderCache CompiledShaderCache;
	return CompiledShaderCache;
}

// ============================================================================================================================


static const TCHAR* ShaderNameFromShaderType(GLenum ShaderType)
{
	switch(ShaderType)
	{
		case GL_VERTEX_SHADER: return TEXT("vertex");
		case GL_FRAGMENT_SHADER: return TEXT("fragment");
		case GL_GEOMETRY_SHADER: return TEXT("geometry");
		case GL_TESS_CONTROL_SHADER: return TEXT("hull");
		case GL_TESS_EVALUATION_SHADER: return TEXT("domain");
		case GL_COMPUTE_SHADER: return TEXT("compute");
		default: return NULL;
	}
}

// ============================================================================================================================

namespace
{
	inline void AppendCString(TArray<ANSICHAR> & Dest, const ANSICHAR * Source)
	{
		if (Dest.Num() > 0)
		{
			Dest.Insert(Source, FCStringAnsi::Strlen(Source), Dest.Num() - 1);;
		}
		else
		{
			Dest.Append(Source, FCStringAnsi::Strlen(Source) + 1);
		}
	}

	inline void ReplaceCString(TArray<ANSICHAR> & Dest, const ANSICHAR * Source, const ANSICHAR * Replacement)
	{
		int32 SourceLen = FCStringAnsi::Strlen(Source);
		int32 ReplacementLen = FCStringAnsi::Strlen(Replacement);
		int32 FoundIndex = 0;
		for (const ANSICHAR * FoundPointer = FCStringAnsi::Strstr(Dest.GetData(), Source);
			nullptr != FoundPointer;
			FoundPointer = FCStringAnsi::Strstr(Dest.GetData()+FoundIndex, Source))
		{
			FoundIndex = FoundPointer - Dest.GetData();
			Dest.RemoveAt(FoundIndex, SourceLen);
			Dest.Insert(Replacement, ReplacementLen, FoundIndex);
		}
	}

	inline const ANSICHAR * CStringEndOfLine(const ANSICHAR * Text)
	{
		const ANSICHAR * LineEnd = FCStringAnsi::Strchr(Text, '\n');
		if (nullptr == LineEnd)
		{
			LineEnd = Text + FCStringAnsi::Strlen(Text);
		}
		return LineEnd;
	}

	inline bool CStringIsBlankLine(const ANSICHAR * Text)
	{
		while (!FCharAnsi::IsLinebreak(*Text))
		{
			if (!FCharAnsi::IsWhitespace(*Text))
			{
				return false;
			}
			++Text;
		}
		return true;
	}

	inline int CStringCountOccurances(TArray<ANSICHAR> & Source, const ANSICHAR * TargetString)
	{
		int32 TargetLen = FCStringAnsi::Strlen(TargetString);
		int Count = 0;
		int32 FoundIndex = 0;
		for (const ANSICHAR * FoundPointer = FCStringAnsi::Strstr(Source.GetData(), TargetString);
			nullptr != FoundPointer;
			FoundPointer = FCStringAnsi::Strstr(Source.GetData() + FoundIndex, TargetString))
		{
			FoundIndex = FoundPointer - Source.GetData();
			FoundIndex += TargetLen;
			Count++;
		}
		return Count;
	}

	inline bool MoveHashLines(TArray<ANSICHAR> & Dest, TArray<ANSICHAR> & Source)
	{
		// Walk through the lines to find the first non-# line...
		const ANSICHAR * LineStart = Source.GetData();
		for (bool FoundNonHashLine = false; !FoundNonHashLine;)
		{
			const ANSICHAR * LineEnd = CStringEndOfLine(LineStart);
			if (LineStart[0] != '#' && !CStringIsBlankLine(LineStart))
			{
				FoundNonHashLine = true;
			}
			else if (LineEnd[0] == '\n')
			{
				LineStart = LineEnd + 1;
			}
			else
			{
				LineStart = LineEnd;
			}
		}
		// Copy the hash lines over, if we found any. And delete from
		// the source.
		if (LineStart > Source.GetData())
		{
			int32 LineLength = LineStart - Source.GetData();
			if (Dest.Num() > 0)
			{
				Dest.Insert(Source.GetData(), LineLength, Dest.Num() - 1);
			}
			else
			{
				Dest.Append(Source.GetData(), LineLength);
				Dest.Append("", 1);
			}
			if (Dest.Last(1) != '\n')
			{
				Dest.Insert("\n", 1, Dest.Num() - 1);
			}
			Source.RemoveAt(0, LineStart - Source.GetData());
			return true;
		}
		return false;
	}
}

// make some anon ns functions available to platform extensions
void PE_AppendCString(TArray<ANSICHAR> & Dest, const ANSICHAR * Source)
{
	AppendCString(Dest, Source);
}

void PE_ReplaceCString(TArray<ANSICHAR> & Dest, const ANSICHAR * Source, const ANSICHAR * Replacement)
{
	ReplaceCString(Dest, Source, Replacement);
}


FOpenGLProgramBinaryCache::FPreviousGLProgramBinaryCacheInfo::FPreviousGLProgramBinaryCacheInfo() : NumberOfOldEntriesReused(0) {}
FOpenGLProgramBinaryCache::FPreviousGLProgramBinaryCacheInfo::FPreviousGLProgramBinaryCacheInfo(FOpenGLProgramBinaryCache::FPreviousGLProgramBinaryCacheInfo&&) = default;
FOpenGLProgramBinaryCache::FPreviousGLProgramBinaryCacheInfo& FOpenGLProgramBinaryCache::FPreviousGLProgramBinaryCacheInfo::operator = (FOpenGLProgramBinaryCache::FPreviousGLProgramBinaryCacheInfo&&) = default;
FOpenGLProgramBinaryCache::FPreviousGLProgramBinaryCacheInfo::~FPreviousGLProgramBinaryCacheInfo() = default;


inline uint32 GetTypeHash(FAnsiCharArray const& CharArray)
{
	return FCrc::MemCrc32(CharArray.GetData(), CharArray.Num() * sizeof(ANSICHAR));
}

// Helper to compile a shader 
// returns true if shader was compiled without any errors or errors should be ignored
static bool CompileCurrentShader(const GLuint Resource, const FAnsiCharArray& GlslCode)
{
	VERIFY_GL_SCOPE();
	const ANSICHAR * GlslCodeString = GlslCode.GetData();
	int32 GlslCodeLength = GlslCode.Num() - 1;

	glShaderSource(Resource, 1, (const GLchar**)&GlslCodeString, &GlslCodeLength);
	glCompileShader(Resource);

	// Verify that an OpenGL shader has compiled successfully.
	SCOPE_CYCLE_COUNTER(STAT_OpenGLShaderCompileVerifyTime);
	
	if (FOpenGL::SupportsSeparateShaderObjects() && glIsProgram(Resource))
	{
		bool const bCompiledOK = VerifyLinkedProgram(Resource);
#if DEBUG_GL_SHADERS
		if (!bCompiledOK && GlslCodeString)
		{
			UE_LOG(LogRHI,Error,TEXT("Shader:\n%s"), ANSI_TO_TCHAR(GlslCodeString));
		}
#endif
		return bCompiledOK;
	}
	else
	{
		GLint CompileStatus;
		glGetShaderiv(Resource, GL_COMPILE_STATUS, &CompileStatus);
		if (CompileStatus != GL_TRUE)
		{
			if (ReportShaderCompileFailures())
			{
				GLint LogLength;
				ANSICHAR DefaultLog[] = "No log";
				ANSICHAR *CompileLog = DefaultLog;
				glGetShaderiv(Resource, GL_INFO_LOG_LENGTH, &LogLength);
#if PLATFORM_ANDROID
				if ( LogLength == 0 )
				{
					// make it big anyway
					// there was a bug in android 2.2 where glGetShaderiv would return 0 even though there was a error message
					// https://code.google.com/p/android/issues/detail?id=9953
					LogLength = 4096;
				}
#endif
				if (LogLength > 1)
				{
					CompileLog = (ANSICHAR *)FMemory::Malloc(LogLength);
					glGetShaderInfoLog(Resource, LogLength, NULL, CompileLog);
				}

#if DEBUG_GL_SHADERS
				if (GlslCodeString)
				{
					UE_LOG(LogRHI,Error,TEXT("Shader:\n%s"),ANSI_TO_TCHAR(GlslCodeString));
				}
#endif
				UE_LOG(LogRHI,Error,TEXT("Failed to compile shader. Compile log:\n%s"), ANSI_TO_TCHAR(CompileLog));
				if (LogLength > 1)
				{
					FMemory::Free(CompileLog);
				}
			}
			// if we're required to ignore compile failure then we return true here, it will end with link failure.
			return CVarIgnoreShaderCompileFailure.GetValueOnAnyThread() == 1;
		}
	}
	return true;
}


// Set the shader hash for FRHIShaders only.
template<typename TRHIType>
static typename TEnableIf<TPointerIsConvertibleFromTo<TRHIType, const FRHIShader>::Value, void>::Type SetShaderHash(const FSHAHash& Hash, TRHIType* Shader)
{
	Shader->SetHash(Hash);
}

template<typename TRHIType>
static typename TEnableIf<!TPointerIsConvertibleFromTo<TRHIType, const FRHIShader>::Value, void>::Type SetShaderHash(const FSHAHash& Hash, TRHIType* Shader)
{
	// Cannot set the shader hash on a non-FRHIShader type.
	checkNoEntry();
}



/**
 * Compiles an OpenGL shader using the given GLSL microcode.
 * @returns the compiled shader upon success.
 */
template <typename ShaderType>
ShaderType* CompileOpenGLShader(TArrayView<const uint8> InShaderCode, const FSHAHash& LibraryHash, FRHIShader* RHIShader = nullptr)
{
	SCOPE_CYCLE_COUNTER(STAT_OpenGLShaderCompileTime);
	VERIFY_GL_SCOPE();

	ShaderType* Shader = nullptr;
	{
		FLibraryShaderCacheValue *Val = GetOpenGLCompiledLibraryShaderCache().Find(LibraryHash);
		if (Val)
		{
			Shader = new ShaderType();
			Shader->Resource = Val->GLShader;
			Shader->Bindings = Val->Header->Bindings;
			Shader->UniformBuffersCopyInfo = Val->Header->UniformBuffersCopyInfo;
			Shader->StaticSlots = Val->StaticSlots;
			if (FOpenGL::SupportsSeparateShaderObjects())
			{
				FSHAHash Hash;
				// Just use the CRC - if it isn't being cached & logged we'll be dependent on the CRC alone anyway
				FMemory::Memcpy(Hash.Hash, &Val->ShaderCrc, sizeof(uint32));
				if (RHIShader)
				{
					SetShaderHash(Hash, RHIShader);
				}
				else
				{
					SetShaderHash(Hash, Shader);
				}
			}
#if DEBUG_GL_SHADERS
			Shader->GlslCode = Val->GlslCode;
			Shader->GlslCodeString = (ANSICHAR*)Shader->GlslCode.GetData();
#endif
			return Shader;
		}
	}

	FShaderCodeReader ShaderCode(InShaderCode);

	const GLenum TypeEnum = ShaderType::TypeEnum;
	FMemoryReaderView Ar(InShaderCode, true);

	Ar.SetLimitSize(ShaderCode.GetActualShaderCodeSize());

	FOpenGLCodeHeader Header = { 0 };

	Ar << Header;
	// Suppress static code analysis warning about a potential comparison of two constants
	CA_SUPPRESS(6326);
	if (Header.GlslMarker != 0x474c534c
		|| (TypeEnum == GL_VERTEX_SHADER && Header.FrequencyMarker != 0x5653)
		|| (TypeEnum == GL_FRAGMENT_SHADER && Header.FrequencyMarker != 0x5053)
		|| (TypeEnum == GL_GEOMETRY_SHADER && Header.FrequencyMarker != 0x4753)
		|| (TypeEnum == GL_COMPUTE_SHADER && Header.FrequencyMarker != 0x4353 && FOpenGL::SupportsComputeShaders())
		|| (TypeEnum == GL_TESS_CONTROL_SHADER && Header.FrequencyMarker != 0x4853 && FOpenGL::SupportsTessellation()) /* hull shader*/
		|| (TypeEnum == GL_TESS_EVALUATION_SHADER && Header.FrequencyMarker != 0x4453 && FOpenGL::SupportsTessellation()) /* domain shader*/
		)
	{
		UE_LOG(LogRHI,Fatal,
			TEXT("Corrupt shader bytecode. GlslMarker=0x%08x FrequencyMarker=0x%04x"),
			Header.GlslMarker,
			Header.FrequencyMarker
			);
		return nullptr;
	}

	int32 CodeOffset = Ar.Tell();

	// The code as given to us.
	FAnsiCharArray GlslCodeOriginal;
	AppendCString(GlslCodeOriginal, (ANSICHAR*)InShaderCode.GetData() + CodeOffset);
	uint32 GlslCodeOriginalCRC = FCrc::MemCrc_DEPRECATED(GlslCodeOriginal.GetData(), GlslCodeOriginal.Num());

	// The amended code we actually compile.
	FAnsiCharArray GlslCode;

	// Find the existing compiled shader in the cache.
	FOpenGLCompiledShaderKey Key(TypeEnum, GlslCodeOriginal.Num(), GlslCodeOriginalCRC);
	GLuint Resource = GetOpenGLCompiledShaderCache().FindRef(Key);
	if (!Resource)
	{
#if CHECK_FOR_GL_SHADERS_TO_REPLACE
		{
			// 1. Check for specific file
			FString PotentialShaderFileName = FString::Printf(TEXT("%s-%d-0x%x.txt"), ShaderNameFromShaderType(TypeEnum), GlslCodeOriginal.Num(), GlslCodeOriginalCRC);
			FString PotentialShaderFile = FPaths::ProfilingDir();
			PotentialShaderFile *= PotentialShaderFileName;

			UE_LOG( LogRHI, Log, TEXT("Looking for shader file '%s' for potential replacement."), *PotentialShaderFileName );

			int64 FileSize = IFileManager::Get().FileSize(*PotentialShaderFile);
			if( FileSize > 0 )
			{
				FArchive* Ar = IFileManager::Get().CreateFileReader(*PotentialShaderFile);
				if( Ar != NULL )
				{
					UE_LOG(LogRHI, Log, TEXT("Replacing %s shader with length %d and CRC 0x%x with the one from a file."), (TypeEnum == GL_VERTEX_SHADER) ? TEXT("vertex") : ((TypeEnum == GL_FRAGMENT_SHADER) ? TEXT("fragment") : TEXT("geometry")), GlslCodeOriginal.Num(), GlslCodeOriginalCRC);

					// read in the file
					GlslCodeOriginal.Empty();
					GlslCodeOriginal.AddUninitialized(FileSize + 1);
					Ar->Serialize(GlslCodeOriginal.GetData(), FileSize);
					delete Ar;
					GlslCodeOriginal[FileSize] = 0;
				}
			}
		}
#endif

		Resource = FOpenGL::CreateShader(TypeEnum);

		// get a modified version of the shader based on device capabilities to compile (destructive to GlslCodeOriginal copy)
		FOpenGLShaderDeviceCapabilities Capabilities;
		GetCurrentOpenGLShaderDeviceCapabilities(Capabilities);
		GLSLToDeviceCompatibleGLSL(GlslCodeOriginal, Header.ShaderName, TypeEnum, Capabilities, GlslCode);

		// Save the code and defer compilation if our device supports program binaries and we're not checking for shader compatibility.
		const bool bDeferredCompilation = FOpenGLProgramBinaryCache::DeferShaderCompilation(Resource, GlslCode);
		// deferred compilation is not supported for SeparateShaderObjects
		check(!bDeferredCompilation || !Capabilities.bSupportsSeparateShaderObjects);

		if (!bDeferredCompilation)
		{
			const bool bSuccessfullyCompiled = CompileCurrentShader(Resource, GlslCode);
			
			if (Capabilities.bSupportsSeparateShaderObjects && bSuccessfullyCompiled)
			{
				ANSICHAR Buf[32] = {0};
				// Create separate shader program
				GLuint SeparateResource = FOpenGL::CreateProgram();
				FOpenGL::ProgramParameter( SeparateResource, GL_PROGRAM_SEPARABLE, GL_TRUE );
				glAttachShader(SeparateResource, Resource);
				
				glLinkProgram(SeparateResource);
				VerifyLinkedProgram(SeparateResource);
			
	#if ENABLE_UNIFORM_BUFFER_LAYOUT_VERIFICATION
				void VerifyUniformBufferLayouts(GLuint Program);
				VerifyUniformBufferLayouts(SeparateResource);
	#endif // #if ENABLE_UNIFORM_BUFFER_LAYOUT_VERIFICATION
			
				Resource = SeparateResource;
			}
		}

		// Cache it; (always caching will prevent multiple attempts to compile a failed shader)
		GetOpenGLCompiledShaderCache().Add(Key, Resource);
	}

	Shader = new ShaderType();
	Shader->Resource = Resource;
	Shader->Bindings = Header.Bindings;
	Shader->UniformBuffersCopyInfo = Header.UniformBuffersCopyInfo;
	Shader->StaticSlots.Reserve(Header.Bindings.ShaderResourceTable.ResourceTableLayoutHashes.Num());

	for (uint32 LayoutHash : Header.Bindings.ShaderResourceTable.ResourceTableLayoutHashes)
	{
		if (const FShaderParametersMetadata* Metadata = FindUniformBufferStructByLayoutHash(LayoutHash))
		{
			Shader->StaticSlots.Add(Metadata->GetLayout().StaticSlot);
		}
		else
		{
			Shader->StaticSlots.Add(MAX_UNIFORM_BUFFER_STATIC_SLOTS);
		}
	}

	checkf(Shader->StaticSlots.Num() == Shader->Bindings.ShaderResourceTable.ResourceTableLayoutHashes.Num(), TEXT("StaticSlots %d, Bindings %d"),
		Shader->StaticSlots.Num(), Shader->Bindings.ShaderResourceTable.ResourceTableLayoutHashes.Num());

	if (FOpenGL::SupportsSeparateShaderObjects())
	{
		FSHAHash Hash;
		// Just use the CRC - if it isn't being cached & logged we'll be dependent on the CRC alone anyway
		FMemory::Memcpy(Hash.Hash, &GlslCodeOriginalCRC, sizeof(uint32));
		if (RHIShader)
		{
			SetShaderHash(Hash, RHIShader);
		}
		else
		{
			SetShaderHash(Hash, Shader);
		}
	}

#if DEBUG_GL_SHADERS
	Shader->GlslCode = GlslCode;
	Shader->GlslCodeString = (ANSICHAR*)Shader->GlslCode.GetData();
#endif
	if (LibraryHash != FSHAHash() && !GetOpenGLCompiledLibraryShaderCache().Contains(LibraryHash))
	{
		FLibraryShaderCacheValue Val;
		Val.GLShader = Resource;
		Val.Header = new FOpenGLCodeHeader;
		*Val.Header = Header;
		Val.ShaderCrc = GlslCodeOriginalCRC;
		Val.StaticSlots = Shader->StaticSlots;
#if DEBUG_GL_SHADERS
		Val.GlslCode = GlslCode;
		Val.GlslCodeString = (ANSICHAR*)Shader->GlslCode.GetData();
#endif
		GetOpenGLCompiledLibraryShaderCache().Add(LibraryHash, Val);
	}

	return Shader;
}

void OPENGLDRV_API GetCurrentOpenGLShaderDeviceCapabilities(FOpenGLShaderDeviceCapabilities& Capabilities)
{
	FMemory::Memzero(Capabilities);

#if PLATFORM_DESKTOP
	Capabilities.TargetPlatform = EOpenGLShaderTargetPlatform::OGLSTP_Desktop;
	if (FOpenGL::IsAndroidGLESCompatibilityModeEnabled())
	{
		Capabilities.TargetPlatform = EOpenGLShaderTargetPlatform::OGLSTP_Android;
		Capabilities.bSupportsShaderFramebufferFetch = FOpenGL::SupportsShaderFramebufferFetch();
		Capabilities.bRequiresARMShaderFramebufferFetchDepthStencilUndef = false;
		Capabilities.MaxVaryingVectors = FOpenGL::GetMaxVaryingVectors();
	}

#elif PLATFORM_ANDROID
	#if PLATFORM_LUMINGL4
		Capabilities.TargetPlatform = EOpenGLShaderTargetPlatform::OGLSTP_Desktop;
	#else
		Capabilities.TargetPlatform = EOpenGLShaderTargetPlatform::OGLSTP_Android;
		Capabilities.bSupportsShaderFramebufferFetch = FOpenGL::SupportsShaderFramebufferFetch();
		Capabilities.bRequiresARMShaderFramebufferFetchDepthStencilUndef = FOpenGL::RequiresARMShaderFramebufferFetchDepthStencilUndef();
		Capabilities.MaxVaryingVectors = FOpenGL::GetMaxVaryingVectors();
		Capabilities.bRequiresDisabledEarlyFragmentTests = FOpenGL::RequiresDisabledEarlyFragmentTests();
	#endif // PLATFORM_LUMINGL4
#elif PLATFORM_IOS
	Capabilities.TargetPlatform = EOpenGLShaderTargetPlatform::OGLSTP_iOS;
#else
	FOpenGL::PE_GetCurrentOpenGLShaderDeviceCapabilities(Capabilities); // platform extension
#endif
	Capabilities.MaxRHIShaderPlatform = GMaxRHIShaderPlatform;
	Capabilities.bSupportsSeparateShaderObjects = FOpenGL::SupportsSeparateShaderObjects();
}

void OPENGLDRV_API GLSLToDeviceCompatibleGLSL(FAnsiCharArray& GlslCodeOriginal, const FString& ShaderName, GLenum TypeEnum, const FOpenGLShaderDeviceCapabilities& Capabilities, FAnsiCharArray& GlslCode)
{
	if (FOpenGL::PE_GLSLToDeviceCompatibleGLSL(GlslCodeOriginal, ShaderName, TypeEnum, Capabilities, GlslCode))
	{
		return; // platform extension overrides
	}

	// Whether we need to emit mobile multi-view code or not.
	const bool bEmitMobileMultiView = (FCStringAnsi::Strstr(GlslCodeOriginal.GetData(), "gl_ViewID_OVR") != nullptr);

	// Whether we need to emit texture external code or not.
	const bool bEmitTextureExternal = (FCStringAnsi::Strstr(GlslCodeOriginal.GetData(), "samplerExternalOES") != nullptr);

	FAnsiCharArray GlslCodeAfterExtensions;
	const ANSICHAR* GlslPlaceHolderAfterExtensions = "// end extensions";
	bool bGlslCodeHasExtensions = CStringCountOccurances(GlslCodeOriginal, GlslPlaceHolderAfterExtensions) == 1;
	
	if (Capabilities.TargetPlatform == EOpenGLShaderTargetPlatform::OGLSTP_Android)
	{
		const ANSICHAR* ES310Version = "#version 310 es";

		// @todo Lumin hack: This is needed for AEP on Lumin, so that some shaders compile that need version 320
		#if PLATFORM_LUMINGL4
			AppendCString(GlslCode, "#version 320 es\n");
			ReplaceCString(GlslCodeOriginal, ES310Version, "");
		#else
			AppendCString(GlslCode, ES310Version);
			AppendCString(GlslCode, "\n");
			ReplaceCString(GlslCodeOriginal, ES310Version, "");
		#endif
	}

	if (TypeEnum == GL_FRAGMENT_SHADER && Capabilities.bRequiresDisabledEarlyFragmentTests)
	{
		ReplaceCString(GlslCodeOriginal, "layout(early_fragment_tests) in;", "");
	}

	// The incoming glsl may have preprocessor code that is dependent on defines introduced via the engine.
	// This is the place to insert such engine preprocessor defines, immediately after the glsl version declaration.

	if (bEmitTextureExternal)
	{
		// remove comment so MoveHashLines works as intended
		ReplaceCString(GlslCodeOriginal, "// Uses samplerExternalOES", "");

		MoveHashLines(GlslCode, GlslCodeOriginal);

		if (GSupportsImageExternal)
		{
			AppendCString(GlslCode, "\n\n");

#if PLATFORM_ANDROID && !PLATFORM_LUMINGL4
			FOpenGL::EImageExternalType ImageExternalType = FOpenGL::GetImageExternalType();
			switch (ImageExternalType)
			{
				case FOpenGL::EImageExternalType::ImageExternal100:
					AppendCString(GlslCode, "#extension GL_OES_EGL_image_external : require\n");
					break;

				case FOpenGL::EImageExternalType::ImageExternal300:
					AppendCString(GlslCode, "#extension GL_OES_EGL_image_external : require\n");
					break;

				case FOpenGL::EImageExternalType::ImageExternalESSL300:
					// GL_OES_EGL_image_external_essl3 is only compatible with ES 3.x
					AppendCString(GlslCode, "#extension GL_OES_EGL_image_external_essl3 : require\n");
					break;
			}
#else
			AppendCString(GlslCode, "#extension GL_OES_EGL_image_external : require\n");
#endif
			AppendCString(GlslCode, "\n\n");
		}
		else
		{
			// Strip out texture external for devices that don't support it.
			AppendCString(GlslCode, "#define samplerExternalOES sampler2D\n");
		}
	}

	if (bEmitMobileMultiView)
	{
		MoveHashLines(GlslCode, GlslCodeOriginal);

		if (GSupportsMobileMultiView)
		{
			AppendCString(GlslCode, "\n\n");
			AppendCString(GlslCode, "#extension GL_OVR_multiview2 : enable\n");
			AppendCString(GlslCode, "\n\n");
		}
		else
		{
			// Strip out multi-view for devices that don't support it.
			AppendCString(GlslCode, "#define gl_ViewID_OVR 0\n");
		}
	}

	// Move version tag & extensions before beginning all other operations
	MoveHashLines(GlslCode, GlslCodeOriginal);

	// OpenGL SM5 shader platforms require location declarations for the layout, but don't necessarily use SSOs
	if (Capabilities.TargetPlatform == EOpenGLShaderTargetPlatform::OGLSTP_Desktop)
	{
		AppendCString(GlslCode, "#extension GL_ARB_separate_shader_objects : enable\n");
		AppendCString(GlslCode, "#define INTERFACE_BLOCK(Pos, Interp, Modifiers, Semantic, PreType, PostType) layout(location=Pos) Interp Modifiers struct { PreType PostType; }\n");
	}
	else
	{
		AppendCString(GlslCode, "#define INTERFACE_BLOCK(Pos, Interp, Modifiers, Semantic, PreType, PostType) layout(location=Pos) Modifiers Semantic { PreType PostType; }\n");
	}

	if (Capabilities.TargetPlatform == EOpenGLShaderTargetPlatform::OGLSTP_Desktop)
	{
		// If we're running <= featurelevel es3.1 shaders then enable this extension which adds support for uintBitsToFloat etc.
		if ((FCStringAnsi::Strstr(GlslCode.GetData(), "#version 150") != nullptr))
		{
			AppendCString(GlslCode, "\n\n");
			AppendCString(GlslCode, "#extension GL_ARB_gpu_shader5 : enable\n");
			AppendCString(GlslCode, "\n\n");
		}
	}

	if (ShaderName.IsEmpty() == false)
	{
		AppendCString(GlslCode, "// ");
		AppendCString(GlslCode, TCHAR_TO_ANSI(ShaderName.GetCharArray().GetData()));
		AppendCString(GlslCode, "\n");
	}

	if (bEmitMobileMultiView && GSupportsMobileMultiView && TypeEnum == GL_VERTEX_SHADER)
	{
		AppendCString(GlslCode, "\n\n");
		AppendCString(GlslCode, "layout(num_views = 2) in;\n");
		AppendCString(GlslCode, "\n\n");
	}

	if (TypeEnum != GL_COMPUTE_SHADER)
	{
		if (FOpenGL::SupportsClipControl())
		{
			AppendCString(GlslCode, "#define HLSLCC_DX11ClipSpace 0 \n");
		}
		else
		{
			AppendCString(GlslCode, "#define HLSLCC_DX11ClipSpace 1 \n");
		}
	}

	// Append the possibly edited shader to the one we will compile.
	// This is to make it easier to debug as we can see the whole
	// shader source.
	AppendCString(GlslCode, "\n\n");
	AppendCString(GlslCode, GlslCodeOriginal.GetData());

	if (bGlslCodeHasExtensions && GlslCodeAfterExtensions.Num() > 0)
	{
		// the initial code has an #extension chunk. replace the placeholder line
		ReplaceCString(GlslCode, GlslPlaceHolderAfterExtensions, GlslCodeAfterExtensions.GetData());
	}
}

/**
 * Helper for constructing strings of the form XXXXX##.
 * @param Str - The string to build.
 * @param Offset - Offset into the string at which to set the number.
 * @param Index - Number to set. Must be in the range [0,100).
 */
static ANSICHAR* SetIndex(ANSICHAR* Str, int32 Offset, int32 Index)
{
	check(Index >= 0 && Index < 100);

	Str += Offset;
	if (Index >= 10)
	{
		*Str++ = '0' + (ANSICHAR)(Index / 10);
	}
	*Str++ = '0' + (ANSICHAR)(Index % 10);
	*Str = '\0';
	return Str;
}

template<typename RHIType, typename TOGLProxyType>
RHIType* CreateProxyShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	if (ShouldRunGLRenderContextOpOnThisThread(RHICmdList))
	{
		return new TOGLProxyType([&](RHIType* OwnerRHI)
		{
			return CompileOpenGLShader<typename TOGLProxyType::ContainedGLType>(Code, Hash, OwnerRHI);
		});
	}
	else
	{
		// take a copy of the code for RHIT version.
		TArray<uint8> CodeCopy(Code);
		return new TOGLProxyType([Code = MoveTemp(CodeCopy), Hash](RHIType* OwnerRHI)
		{
			return CompileOpenGLShader<typename TOGLProxyType::ContainedGLType>(Code, Hash, OwnerRHI);
		});
	}
}

FVertexShaderRHIRef FOpenGLDynamicRHI::RHICreateVertexShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return CreateProxyShader<FRHIVertexShader, FOpenGLVertexShaderProxy>(Code, Hash);
}

FPixelShaderRHIRef FOpenGLDynamicRHI::RHICreatePixelShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return CreateProxyShader<FRHIPixelShader, FOpenGLPixelShaderProxy>(Code, Hash);
}

FGeometryShaderRHIRef FOpenGLDynamicRHI::RHICreateGeometryShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	return CreateProxyShader<FRHIGeometryShader, FOpenGLGeometryShaderProxy>(Code, Hash);
}

FHullShaderRHIRef FOpenGLDynamicRHI::RHICreateHullShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	check(GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5);
	return CreateProxyShader<FRHIHullShader, FOpenGLHullShaderProxy>(Code, Hash);
}

FDomainShaderRHIRef FOpenGLDynamicRHI::RHICreateDomainShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	check(GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5);
	return CreateProxyShader<FRHIDomainShader, FOpenGLDomainShaderProxy>(Code, Hash);
}

static void MarkShaderParameterCachesDirty(FOpenGLShaderParameterCache* ShaderParameters, bool UpdateCompute)
{
	VERIFY_GL_SCOPE();
	const int32 StageStart = UpdateCompute  ? CrossCompiler::SHADER_STAGE_COMPUTE : CrossCompiler::SHADER_STAGE_VERTEX;
	const int32 StageEnd = UpdateCompute ? CrossCompiler::NUM_SHADER_STAGES : CrossCompiler::NUM_NON_COMPUTE_SHADER_STAGES;
	for (int32 Stage = StageStart; Stage < StageEnd; ++Stage)
	{
		ShaderParameters[Stage].MarkAllDirty();
	}
}

void FOpenGLDynamicRHI::BindUniformBufferBase(FOpenGLContextState& ContextState, int32 NumUniformBuffers, FUniformBufferRHIRef* BoundUniformBuffers, uint32 FirstUniformBuffer, bool ForceUpdate)
{
	SCOPE_CYCLE_COUNTER_DETAILED(STAT_OpenGLUniformBindTime);
	VERIFY_GL_SCOPE();
	checkSlow(IsInRenderingThread() || IsInRHIThread());
	check(!GUseEmulatedUniformBuffers);

	for (int32 BufferIndex = 0; BufferIndex < NumUniformBuffers; ++BufferIndex)
	{
		GLuint Buffer = 0;
		uint32 Offset = 0;
		uint32 Size = ZERO_FILLED_DUMMY_UNIFORM_BUFFER_SIZE;
		int32 BindIndex = FirstUniformBuffer + BufferIndex;
		if (IsValidRef(BoundUniformBuffers[BufferIndex]))
		{
			FRHIUniformBuffer* UB = BoundUniformBuffers[BufferIndex].GetReference();
			Buffer = ((FOpenGLUniformBuffer*)UB)->Resource;
			Size = ((FOpenGLUniformBuffer*)UB)->GetSize();
#if SUBALLOCATED_CONSTANT_BUFFER
			Offset = ((FOpenGLUniformBuffer*)UB)->Offset;
#endif
		}
		else
		{
			if (PendingState.ZeroFilledDummyUniformBuffer == 0)
			{
				void* ZeroBuffer = FMemory::Malloc(ZERO_FILLED_DUMMY_UNIFORM_BUFFER_SIZE);
				FMemory::Memzero(ZeroBuffer,ZERO_FILLED_DUMMY_UNIFORM_BUFFER_SIZE);
				FOpenGL::GenBuffers(1, &PendingState.ZeroFilledDummyUniformBuffer);
				check(PendingState.ZeroFilledDummyUniformBuffer != 0);
				CachedBindUniformBuffer(ContextState,PendingState.ZeroFilledDummyUniformBuffer);
				glBufferData(GL_UNIFORM_BUFFER, ZERO_FILLED_DUMMY_UNIFORM_BUFFER_SIZE, ZeroBuffer, GL_STATIC_DRAW);
				FMemory::Free(ZeroBuffer);
				IncrementBufferMemory(GL_UNIFORM_BUFFER, false, ZERO_FILLED_DUMMY_UNIFORM_BUFFER_SIZE);
			}

			Buffer = PendingState.ZeroFilledDummyUniformBuffer;
		}

		if (ForceUpdate || (Buffer != 0 && ContextState.UniformBuffers[BindIndex] != Buffer)|| ContextState.UniformBufferOffsets[BindIndex] != Offset)
		{
			FOpenGL::BindBufferRange(GL_UNIFORM_BUFFER, BindIndex, Buffer, Offset, Size);
			ContextState.UniformBuffers[BindIndex] = Buffer;
			ContextState.UniformBufferOffsets[BindIndex] = Offset;
			ContextState.UniformBufferBound = Buffer;	// yes, calling glBindBufferRange also changes uniform buffer binding.
		}
	}
}

// ============================================================================================================================

struct FOpenGLUniformName
{
	FOpenGLUniformName()
	{
		FMemory::Memzero(Buffer);
	}
	
	ANSICHAR Buffer[10];
	
	friend bool operator ==(const FOpenGLUniformName& A,const FOpenGLUniformName& B)
	{
		return FMemory::Memcmp(A.Buffer, B.Buffer, sizeof(A.Buffer)) == 0;
	}
	
	friend uint32 GetTypeHash(const FOpenGLUniformName &Key)
	{
		return FCrc::MemCrc32(Key.Buffer, sizeof(Key.Buffer));
	}
};

static TMap<GLuint, TMap<FOpenGLUniformName, int64>>& GetOpenGLUniformBlockLocations()
{
	static TMap<GLuint, TMap<FOpenGLUniformName, int64>> UniformBlockLocations;
	return UniformBlockLocations;
}

static TMap<GLuint, TMap<int64, int64>>& GetOpenGLUniformBlockBindings()
{
	static TMap<GLuint, TMap<int64, int64>> UniformBlockBindings;
	return UniformBlockBindings;
}

static GLuint GetOpenGLProgramUniformBlockIndex(GLuint Program, const FOpenGLUniformName& UniformBlockName)
{
	TMap<FOpenGLUniformName, int64>& Locations = GetOpenGLUniformBlockLocations().FindOrAdd(Program);
	int64* Location = Locations.Find(UniformBlockName);
	if(Location)
	{
		return *Location;
	}
	else
	{
		int64& Loc = Locations.Emplace(UniformBlockName);
		Loc = (int64)FOpenGL::GetUniformBlockIndex(Program, UniformBlockName.Buffer);
		return Loc;
	}
}

static void GetOpenGLProgramUniformBlockBinding(GLuint Program, GLuint UniformBlockIndex, GLuint UniformBlockBinding)
{
	TMap<int64, int64>& Bindings = GetOpenGLUniformBlockBindings().FindOrAdd(Program);
	int64* Bind = static_cast<int64 *>(Bindings.Find(UniformBlockIndex));
	if(!Bind)
	{
		Bind = &(Bindings.Emplace(UniformBlockIndex));
		check(Bind);
		*Bind = -1;
	}
	check(Bind);
	if(*Bind != static_cast<int64>(UniformBlockBinding))
	{
		*Bind = static_cast<int64>(UniformBlockBinding);
		FOpenGL::UniformBlockBinding(Program, UniformBlockIndex, UniformBlockBinding);
	}
}

// ============================================================================================================================

int32 GEvictOnBSSDestructLatency = 0;
static FAutoConsoleVariableRef CVarEvictOnBssDestructLatency(
	TEXT("r.OpenGL.EvictOnBSSDestruct.Latency"),
	GEvictOnBSSDestructLatency,
	TEXT(""),
	ECVF_ReadOnly | ECVF_RenderThreadSafe
);

class FOpenGLLinkedProgram
{
public:
	FOpenGLLinkedProgramConfiguration Config;

	struct FPackedUniformInfo
	{
		GLint	Location;
		uint8	ArrayType;	// OGL_PACKED_ARRAYINDEX_TYPE
		uint8	Index;		// OGL_PACKED_INDEX_TYPE
	};

	// Holds information needed per stage regarding packed uniform globals and uniform buffers
	struct FStagePackedUniformInfo
	{
		// Packed Uniform Arrays (regular globals); array elements per precision/type
		TArray<FPackedUniformInfo>			PackedUniformInfos;

		// Packed Uniform Buffers; outer array is per Uniform Buffer; inner array is per precision/type
		TArray<TArray<FPackedUniformInfo>>	PackedUniformBufferInfos;

		// Holds the unique ID of the last uniform buffer uploaded to the program; since we don't reuse uniform buffers
		// (can't modify existing ones), we use this as a check for dirty/need to mem copy on Mobile
		TArray<uint32>						LastEmulatedUniformBufferSet;
	};
	FStagePackedUniformInfo	StagePackedUniformInfo[CrossCompiler::NUM_SHADER_STAGES];

	GLuint		Program;
	bool		bUsingTessellation;
	bool		bDrawn;
	bool		bConfigIsInitalized;

	int32		MaxTextureStage;
	TBitArray<>	TextureStageNeeds;
	int32		MaxUAVUnitUsed;
	TBitArray<>	UAVStageNeeds;

	TArray<FOpenGLBindlessSamplerInfo> Samplers;

	// TODO: This should be stored within the lru.
	class FLRUInfo
	{
	public:
		FLRUInfo() : EvictBucket(-2) {}
		// ID to LRU (if used) allows quick access when updating LRU status.
		FSetElementId LRUNode;
		// cached binary used to create this program.
		TArray<uint8> CachedProgramBinary;

		// < 0 if not pending eviction. Bucket index if pending eviction.
		int32 EvictBucket;
	} LRUInfo;

private:
	FOpenGLLinkedProgram()
	: Program(0), bUsingTessellation(false), bDrawn(false), bConfigIsInitalized(false), MaxTextureStage(-1), MaxUAVUnitUsed(-1)
	{
		TextureStageNeeds.Init( false, FOpenGL::GetMaxCombinedTextureImageUnits() );
		UAVStageNeeds.Init( false, FOpenGL::GetMaxCombinedUAVUnits() );
	}

public:

	FOpenGLLinkedProgram(const FOpenGLProgramKey& ProgramKeyIn)
		: FOpenGLLinkedProgram()
	{
		Config.ProgramKey = ProgramKeyIn;
	}

	FOpenGLLinkedProgram(const FOpenGLProgramKey& ProgramKeyIn, GLuint ProgramIn)
		: FOpenGLLinkedProgram()
	{
		// Add a program without a valid config. (partially initialized)
		Program = ProgramIn;

		// The key is required as the program could be evicted before being bound (set bss).
		Config.ProgramKey = ProgramKeyIn;
	}

	FOpenGLLinkedProgram(const FOpenGLLinkedProgramConfiguration& ConfigIn, GLuint ProgramIn, bool bUsingTessellationIn)
		: FOpenGLLinkedProgram()
	{
		SetConfig(ConfigIn);
		Program = ProgramIn;
		bUsingTessellation = bUsingTessellationIn;
	}

	~FOpenGLLinkedProgram()
	{
		DeleteGLResources();
	}

	void DeleteGLResources()
	{
		VERIFY_GL_SCOPE();
		SetDeletedProgramStats(Program);
		FOpenGL::DeleteProgramPipelines(1, &Program);

		if (!FOpenGL::SupportsSeparateShaderObjects())
		{
			GetOpenGLUniformBlockLocations().Remove(Program);
			GetOpenGLUniformBlockBindings().Remove(Program);
		}
		Program = 0;

		for (int Stage = 0; Stage < CrossCompiler::NUM_SHADER_STAGES; Stage++)
		{
			StagePackedUniformInfo[Stage].PackedUniformInfos.Empty();
			StagePackedUniformInfo[Stage].PackedUniformBufferInfos.Empty();
			StagePackedUniformInfo[Stage].LastEmulatedUniformBufferSet.Empty();
		}
	}

	// Rebind the uniform blocks when changing the separable shader pipeline as different stages will have different uniform block arrangements. Does nothing for non-separable GLs.
	void VerifyUniformBlockBindings( int Stage, uint32 FirstUniformBuffer );

	void ConfigureShaderStage( int Stage, uint32 FirstUniformBuffer );

	// Make sure GlobalArrays (created from shader reflection) matches our info (from the cross compiler)
	static inline void SortPackedUniformInfos(const TArray<FPackedUniformInfo>& ReflectedUniformInfos, const TArray<CrossCompiler::FPackedArrayInfo>& PackedGlobalArrays, TArray<FPackedUniformInfo>& OutPackedUniformInfos)
	{
		check(OutPackedUniformInfos.Num() == 0);
		OutPackedUniformInfos.Empty(PackedGlobalArrays.Num());
		for (int32 Index = 0; Index < PackedGlobalArrays.Num(); ++Index)
		{
			auto& PackedArray = PackedGlobalArrays[Index];
			FPackedUniformInfo OutInfo = {-1, PackedArray.TypeName, CrossCompiler::PACKED_TYPEINDEX_MAX};

			// Find this Global Array in the reflection list
			for (int32 FindIndex = 0; FindIndex < ReflectedUniformInfos.Num(); ++FindIndex)
			{
				auto& ReflectedInfo = ReflectedUniformInfos[FindIndex];
				if (ReflectedInfo.ArrayType == PackedArray.TypeName)
				{
					OutInfo = ReflectedInfo;
					break;
				}
			}

			OutPackedUniformInfos.Add(OutInfo);
		}
	}

	void SetConfig(const FOpenGLLinkedProgramConfiguration& ConfigIn)
	{
		Config = ConfigIn;
		bConfigIsInitalized = true;
	}
};

static bool bMeasureEviction = false;
class FDelayedEvictionContainer
{
public:

	FDelayedEvictionContainer()
	{
		Init();
	}

	void Add(FOpenGLLinkedProgram* LinkedProgram);

	FORCEINLINE_DEBUGGABLE static void OnProgramTouched(FOpenGLLinkedProgram* LinkedProgram)
	{
		if(LinkedProgram->LRUInfo.EvictBucket >=0 )
		{
			FDelayedEvictionContainer::Get().Remove(LinkedProgram);
			INC_DWORD_STAT(STAT_OpenGLShaderLRUEvictionDelaySavedCount);
		}
	}

	void Tick();

	void Init();

	void Remove(FOpenGLLinkedProgram* RemoveMe);

	FORCEINLINE_DEBUGGABLE static FDelayedEvictionContainer & Get()
	{
		static FDelayedEvictionContainer DelayedEvictionContainer;
		return DelayedEvictionContainer;
	}
private:
	class FDelayEvictBucket
	{
	public:
		FDelayEvictBucket() : NumToFreePerTick(0) {}
		int32 NumToFreePerTick;
		TSet<FOpenGLLinkedProgram*> ProgramsToEvict;
	};

	TArray<FDelayEvictBucket> Buckets;

	int32 TotalBuckets;
	int32 TimePerBucket;
	int32 CurrentBucketTickCount;
	int32 NewProgramBucket;
	int32 EvictBucketIndex;
};

static void ConfigureStageStates(FOpenGLLinkedProgram* LinkedProgram)
{
	const FOpenGLLinkedProgramConfiguration &Config = LinkedProgram->Config;

	if (Config.Shaders[CrossCompiler::SHADER_STAGE_VERTEX].Resource)
	{
		LinkedProgram->ConfigureShaderStage(
			CrossCompiler::SHADER_STAGE_VERTEX,
			OGL_FIRST_UNIFORM_BUFFER
		);
		check(LinkedProgram->StagePackedUniformInfo[CrossCompiler::SHADER_STAGE_VERTEX].PackedUniformInfos.Num() <= Config.Shaders[CrossCompiler::SHADER_STAGE_VERTEX].Bindings.PackedGlobalArrays.Num());
	}

	if (Config.Shaders[CrossCompiler::SHADER_STAGE_PIXEL].Resource)
	{
		LinkedProgram->ConfigureShaderStage(
			CrossCompiler::SHADER_STAGE_PIXEL,
			OGL_FIRST_UNIFORM_BUFFER +
			Config.Shaders[CrossCompiler::SHADER_STAGE_VERTEX].Bindings.NumUniformBuffers
		);
		check(LinkedProgram->StagePackedUniformInfo[CrossCompiler::SHADER_STAGE_PIXEL].PackedUniformInfos.Num() <= Config.Shaders[CrossCompiler::SHADER_STAGE_PIXEL].Bindings.PackedGlobalArrays.Num());
	}

	if (Config.Shaders[CrossCompiler::SHADER_STAGE_GEOMETRY].Resource)
	{
		LinkedProgram->ConfigureShaderStage(
			CrossCompiler::SHADER_STAGE_GEOMETRY,
			OGL_FIRST_UNIFORM_BUFFER +
			Config.Shaders[CrossCompiler::SHADER_STAGE_VERTEX].Bindings.NumUniformBuffers +
			Config.Shaders[CrossCompiler::SHADER_STAGE_PIXEL].Bindings.NumUniformBuffers
		);
		check(LinkedProgram->StagePackedUniformInfo[CrossCompiler::SHADER_STAGE_GEOMETRY].PackedUniformInfos.Num() <= Config.Shaders[CrossCompiler::SHADER_STAGE_GEOMETRY].Bindings.PackedGlobalArrays.Num());
	}

	if (Config.Shaders[CrossCompiler::SHADER_STAGE_HULL].Resource)
	{
		LinkedProgram->ConfigureShaderStage(
			CrossCompiler::SHADER_STAGE_HULL,
			OGL_FIRST_UNIFORM_BUFFER +
			Config.Shaders[CrossCompiler::SHADER_STAGE_VERTEX].Bindings.NumUniformBuffers +
			Config.Shaders[CrossCompiler::SHADER_STAGE_PIXEL].Bindings.NumUniformBuffers +
			Config.Shaders[CrossCompiler::SHADER_STAGE_GEOMETRY].Bindings.NumUniformBuffers
		);
	}

	if (Config.Shaders[CrossCompiler::SHADER_STAGE_DOMAIN].Resource)
	{
		LinkedProgram->ConfigureShaderStage(
			CrossCompiler::SHADER_STAGE_DOMAIN,
			OGL_FIRST_UNIFORM_BUFFER +
			Config.Shaders[CrossCompiler::SHADER_STAGE_VERTEX].Bindings.NumUniformBuffers +
			Config.Shaders[CrossCompiler::SHADER_STAGE_PIXEL].Bindings.NumUniformBuffers +
			Config.Shaders[CrossCompiler::SHADER_STAGE_GEOMETRY].Bindings.NumUniformBuffers +
			Config.Shaders[CrossCompiler::SHADER_STAGE_HULL].Bindings.NumUniformBuffers
		);
	}

	if (Config.Shaders[CrossCompiler::SHADER_STAGE_COMPUTE].Resource)
	{
		LinkedProgram->ConfigureShaderStage(
			CrossCompiler::SHADER_STAGE_COMPUTE,
			OGL_FIRST_UNIFORM_BUFFER
		);
		check(LinkedProgram->StagePackedUniformInfo[CrossCompiler::SHADER_STAGE_COMPUTE].PackedUniformInfos.Num() <= Config.Shaders[CrossCompiler::SHADER_STAGE_COMPUTE].Bindings.PackedGlobalArrays.Num());
	}
}

static bool CreateGLProgramFromUncompressedBinary(GLuint& ProgramOUT, const TArray<uint8>& ProgramBinary)
{
	VERIFY_GL_SCOPE();
	GLuint GLProgramName = 0;
	FOpenGL::GenProgramPipelines(1, &GLProgramName);
	int32 BinarySize = ProgramBinary.Num();
	//UE_LOG(LogRHI, Log, TEXT("CreateGLProgramFromBinary : gen program %x, size: %d"), GLProgramName, BinarySize);

	check(BinarySize);

	const uint8* ProgramBinaryPtr = ProgramBinary.GetData();

	// BinaryFormat is stored at the start of ProgramBinary array
	FOpenGL::ProgramBinary(GLProgramName, ((GLenum*)ProgramBinaryPtr)[0], ProgramBinaryPtr + sizeof(GLenum), BinarySize - sizeof(GLenum));
	//	UE_LOG(LogRHI, Warning, TEXT("LRU: CreateFromBinary %d, binary format: %x, BinSize: %d"), GLProgramName, ((GLenum*)ProgramBinaryPtr)[0], BinarySize - sizeof(GLenum));

	ProgramOUT = GLProgramName;
	return VerifyLinkedProgram(GLProgramName);
}

struct FCompressedProgramBinaryHeader
{
	static const uint32 NotCompressed = 0xFFFFFFFF;
	uint32 UncompressedSize;
};

static bool UncompressCompressedBinaryProgram(const TArray<uint8>& CompressedProgramBinary, TArray<uint8>& UncompressedProgramBinaryOUT )
{
	if (ensure(CompressedProgramBinary.Num() > sizeof(FCompressedProgramBinaryHeader)))
	{
		FCompressedProgramBinaryHeader* Header = (FCompressedProgramBinaryHeader*)CompressedProgramBinary.GetData();

		if (Header->UncompressedSize == FCompressedProgramBinaryHeader::NotCompressed)
		{
			const uint32 ProgramSize = CompressedProgramBinary.Num() - sizeof(FCompressedProgramBinaryHeader);
			UncompressedProgramBinaryOUT.SetNumUninitialized(ProgramSize);
			FMemory::Memcpy(UncompressedProgramBinaryOUT.GetData(), CompressedProgramBinary.GetData() + sizeof(FCompressedProgramBinaryHeader), ProgramSize);
			return true;
		}
		else
		{
			UncompressedProgramBinaryOUT.AddUninitialized(Header->UncompressedSize);

			if (Header->UncompressedSize > 0
				&& FCompression::UncompressMemory(NAME_Zlib, UncompressedProgramBinaryOUT.GetData(), UncompressedProgramBinaryOUT.Num(), CompressedProgramBinary.GetData() + sizeof(FCompressedProgramBinaryHeader), CompressedProgramBinary.Num() - sizeof(FCompressedProgramBinaryHeader)))
			{
				return true;
			}
		}
	}
	return false;
}

static bool CreateGLProgramFromCompressedBinary(GLuint& ProgramOUT, const TArray<uint8>& CompressedProgramBinary)
{
	TArray<uint8> UncompressedProgramBinary;

	bool bDecompressSuccess;

	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_DecompressProgramBinary);
		bDecompressSuccess = UncompressCompressedBinaryProgram(CompressedProgramBinary, UncompressedProgramBinary);
	}

	if(bDecompressSuccess)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_CreateProgramFromBinary);
		return CreateGLProgramFromUncompressedBinary(ProgramOUT, UncompressedProgramBinary);
	}
	return false;
}

static bool GetUncompressedProgramBinaryFromGLProgram(GLuint Program, TArray<uint8>& ProgramBinaryOUT)
{
	VERIFY_GL_SCOPE();

	// pull binary from linked program
	GLint BinaryLength = -1;
	glGetProgramiv(Program, GL_PROGRAM_BINARY_LENGTH, &BinaryLength);
	if(BinaryLength > 0)
	{
		ProgramBinaryOUT.SetNumUninitialized(BinaryLength + sizeof(GLenum));
		uint8* ProgramBinaryPtr = ProgramBinaryOUT.GetData();
		// BinaryFormat is stored at the start of ProgramBinary array
		FOpenGL::GetProgramBinary(Program, BinaryLength, &BinaryLength, (GLenum*)ProgramBinaryPtr, ProgramBinaryPtr + sizeof(GLenum));
		return true;
	}
	return false;
}

static bool GetCompressedProgramBinaryFromGLProgram(GLuint Program, TArray<uint8>& ProgramBinaryOUT)
{
	// get uncompressed binary
	TArray<uint8> UncompressedProgramBinary;
	if (GetUncompressedProgramBinaryFromGLProgram(Program, UncompressedProgramBinary))
	{
		int32 CompressedSize = FCompression::CompressMemoryBound(NAME_Zlib, UncompressedProgramBinary.Num());
		uint32 CompressedHeaderSize = sizeof(FCompressedProgramBinaryHeader);
		ProgramBinaryOUT.AddUninitialized(CompressedSize + CompressedHeaderSize);
		bool bSuccess = FCompression::CompressMemory(NAME_Zlib, ProgramBinaryOUT.GetData() + CompressedHeaderSize, CompressedSize, UncompressedProgramBinary.GetData(), UncompressedProgramBinary.Num());
		if(bSuccess)
		{
			ProgramBinaryOUT.SetNum(CompressedSize + CompressedHeaderSize);
			ProgramBinaryOUT.Shrink();
			FCompressedProgramBinaryHeader* Header = (FCompressedProgramBinaryHeader*)ProgramBinaryOUT.GetData();
			Header->UncompressedSize = UncompressedProgramBinary.Num();
		}
		else
		{
			// failed, store the uncompressed version.
			UE_LOG(LogRHI, Log, TEXT("Storing binary program uncompressed (%d, %d, %d)"), UncompressedProgramBinary.Num(), ProgramBinaryOUT.Num(), CompressedSize);
			ProgramBinaryOUT.SetNumUninitialized(UncompressedProgramBinary.Num() + CompressedHeaderSize);
			FCompressedProgramBinaryHeader* Header = (FCompressedProgramBinaryHeader*)ProgramBinaryOUT.GetData();
			Header->UncompressedSize = FCompressedProgramBinaryHeader::NotCompressed;
			FMemory::Memcpy(ProgramBinaryOUT.GetData() + sizeof(FCompressedProgramBinaryHeader), UncompressedProgramBinary.GetData(), UncompressedProgramBinary.Num());
		}
		return true;
	}
	return false;
}

static bool GetProgramBinaryFromGLProgram(GLuint Program, TArray<uint8>& ProgramBinaryOUT)
{
	if (CVarStoreCompressedBinaries.GetValueOnAnyThread())
	{
		return GetCompressedProgramBinaryFromGLProgram(Program, ProgramBinaryOUT);
	}
	else
	{
		return GetUncompressedProgramBinaryFromGLProgram(Program, ProgramBinaryOUT);
	}
}

static bool CreateGLProgramFromBinary(GLuint& ProgramOUT, const TArray<uint8>& ProgramBinary)
{
	SCOPE_CYCLE_COUNTER(STAT_OpenGLCreateProgramFromBinaryTime)
	
	if (CVarStoreCompressedBinaries.GetValueOnAnyThread())
	{
		return CreateGLProgramFromCompressedBinary(ProgramOUT, ProgramBinary);
	}
	else
	{
		return CreateGLProgramFromUncompressedBinary(ProgramOUT, ProgramBinary);
	}
}

static int32 GetProgramBinarySize(GLuint Program)
{
	GLint BinaryLength = -1;
	glGetProgramiv(Program, GL_PROGRAM_BINARY_LENGTH, &BinaryLength);
	check(BinaryLength > 0);
	return BinaryLength;
}

void ConfigureGLProgramStageStates(FOpenGLLinkedProgram* LinkedProgram)
{
	ensure(VerifyProgramPipeline(LinkedProgram->Program));
	FOpenGL::BindProgramPipeline(LinkedProgram->Program);
	ConfigureStageStates(LinkedProgram);
}

class FGLProgramCacheLRU
{
	class FEvictedGLProgram
	{
		FOpenGLLinkedProgram* LinkedProgram;

		FORCEINLINE_DEBUGGABLE TArray<uint8>& GetProgramBinary()
		{
			return LinkedProgram->LRUInfo.CachedProgramBinary;
		}

	public:

		// Create an evicted program with the program binary provided.
		FEvictedGLProgram(const FOpenGLProgramKey& ProgramKey, TArray<uint8>&& ProgramBinaryIn)
		{
			LinkedProgram = new FOpenGLLinkedProgram(ProgramKey);

			GetProgramBinary() = MoveTemp(ProgramBinaryIn);

			INC_MEMORY_STAT_BY(STAT_OpenGLShaderLRUProgramMemory, GetProgramBinary().Num());
		}

		FEvictedGLProgram(FOpenGLLinkedProgram* InLinkedProgram)
			: LinkedProgram(InLinkedProgram)
		{
			bool bCreateProgramBinary = CVarLRUKeepProgramBinaryResident.GetValueOnAnyThread() == 0 || LinkedProgram->LRUInfo.CachedProgramBinary.Num() == 0;

			if( bCreateProgramBinary )
			{
			// build offline binary:
				GetProgramBinaryFromGLProgram(LinkedProgram->Program, GetProgramBinary());
				INC_MEMORY_STAT_BY(STAT_OpenGLShaderLRUProgramMemory, GetProgramBinary().Num());
			}

			if(bMeasureEviction)
			{
				QUICK_SCOPE_CYCLE_COUNTER(STAT_EvictFromLRU_DeleteGLResource);
				// Remove existing GL program and associated data.
				LinkedProgram->DeleteGLResources();
			}
			else
			{
				LinkedProgram->DeleteGLResources();
			}
		}

		void RestoreGLProgramFromBinary()
		{
			check(LinkedProgram->Program == 0);
			bool bSuccess = CreateGLProgramFromBinary(LinkedProgram->Program, GetProgramBinary());
			if(bSuccess)
			{
				if(CVarLRUKeepProgramBinaryResident.GetValueOnAnyThread() == 0)
				{
					DEC_MEMORY_STAT_BY(STAT_OpenGLShaderLRUProgramMemory, GetProgramBinary().Num());
					GetProgramBinary().Empty();
				}
			}
			else
			{
				uint32 ProgramCRC = FCrc::MemCrc32(GetProgramBinary().GetData(), GetProgramBinary().Num());
				UE_LOG(LogRHI, Log, TEXT("[%s, %d, %d, crc 0x%X]"), *LinkedProgram->Config.ProgramKey.ToString(), LinkedProgram->Program, GetProgramBinary().Num(), ProgramCRC );
				// dump first 32 bytes..
				if (GetProgramBinary().Num() >= 32)
				{
					const uint32* MemPtr = (const uint32*)GetProgramBinary().GetData();
					for (int32 Dump = 0; Dump < 8; Dump++)
					{
						UE_LOG(LogRHI, Log, TEXT("[%d :  0x%08X]"), Dump, *MemPtr++);
					}
				}
				RHIGetPanicDelegate().ExecuteIfBound(FName("FailedBinaryProgramCreate"));
				UE_LOG(LogRHI, Fatal, TEXT("RestoreGLProgramFromBinary : Failed to restore GL program from binary data! [%s]"), *LinkedProgram->Config.ProgramKey.ToString());
			}
		}

		FOpenGLLinkedProgram* GetLinkedProgram()
		{
			return LinkedProgram;
		}
	};
	typedef TMap<FOpenGLProgramKey, FEvictedGLProgram> FOpenGLEvictedProgramsMap;
	typedef TPsoLruCache<FOpenGLProgramKey, FOpenGLLinkedProgram* > FOpenGLProgramLRUCache;
	const int LRUCapacity = 2048;
	int32 LRUBinaryMemoryUse;

	// Find linked program within the evicted container.
	// no attempt to promote to LRU or create the GL object is made.
	FOpenGLLinkedProgram* FindEvicted(const FOpenGLProgramKey& ProgramKey)
	{
		FEvictedGLProgram* FoundEvicted = EvictedPrograms.Find(ProgramKey);
		if (FoundEvicted)
		{
			FOpenGLLinkedProgram* LinkedProgram = FoundEvicted->GetLinkedProgram();
			return LinkedProgram;
		}
		return nullptr;
	}

	FOpenGLLinkedProgram* FindEvictedAndUpdateLRU(const FOpenGLProgramKey& ProgramKey)
	{
		// Missed LRU cache, check evicted cache and add back to LRU
		FEvictedGLProgram* FoundEvicted = EvictedPrograms.Find(ProgramKey);
		if (FoundEvicted)
		{
			SCOPE_CYCLE_COUNTER(STAT_OpenGLShaderLRUMissTime);
			INC_DWORD_STAT(STAT_OpenGLShaderLRUMissCount);

			// UE_LOG(LogRHI, Warning, TEXT("LRU: found and recovered EVICTED program %s"), *ProgramKey.ToString());
			FoundEvicted->RestoreGLProgramFromBinary();
			FOpenGLLinkedProgram* LinkedProgram = FoundEvicted->GetLinkedProgram();

			// Remove from the evicted program map.
			EvictedPrograms.Remove(ProgramKey);

			// Add this back to the LRU
			Add(ProgramKey, LinkedProgram);

			DEC_DWORD_STAT(STAT_OpenGLShaderLRUEvictedProgramCount);

			// reconfigure the new program:
			ConfigureGLProgramStageStates(LinkedProgram);

			SetNewProgramStats(LinkedProgram->Program);

			return LinkedProgram;
		}

		// nope.
		return nullptr;
	}

	void EvictFromLRU(FOpenGLLinkedProgram* LinkedProgram)
	{
		SCOPE_CYCLE_COUNTER(STAT_OpenGLShaderLRUEvictTime);
		LinkedProgram->LRUInfo.LRUNode = FSetElementId();

		if (LinkedProgram->LRUInfo.EvictBucket >= 0)
		{
			// remove it from the delayed eviction container since we're evicting now.
			FDelayedEvictionContainer::Get().Remove(LinkedProgram);
		}

		DEC_DWORD_STAT(STAT_OpenGLShaderLRUProgramCount);

		if (bMeasureEviction)
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT__EvictFromLRU_GetBinarySize);
			LRUBinaryMemoryUse -= GetProgramBinarySize(LinkedProgram->Program);
		}
		else
		{
			LRUBinaryMemoryUse -= GetProgramBinarySize(LinkedProgram->Program);
		}

		checkf(!EvictedPrograms.Contains(LinkedProgram->Config.ProgramKey), TEXT("Program is already in the evicted program list: %s"), *LinkedProgram->Config.ProgramKey.ToString());
		//UE_LOG(LogRHI, Warning, TEXT("LRU: Evicting program %d"), LinkedProgram->Program);
		FEvictedGLProgram& test = EvictedPrograms.Emplace(LinkedProgram->Config.ProgramKey, FEvictedGLProgram(LinkedProgram));
		INC_DWORD_STAT(STAT_OpenGLShaderLRUEvictedProgramCount);
	}

public:

	bool IsEvicted(const FOpenGLProgramKey& ProgramKey)
	{
		return FindEvicted(ProgramKey) != nullptr;
	}

	void EvictLeastRecentFromLRU()
	{
		EvictFromLRU(LRU.RemoveLeastRecent());
	}

	void EvictMostRecentFromLRU()
	{
		EvictFromLRU(LRU.RemoveMostRecent());
	}

	void EvictProgramFromLRU(const FOpenGLProgramKey& ProgramKey)
	{
		FOpenGLLinkedProgram* RemovedLinkedProgram = nullptr;
		if(LRU.Remove(ProgramKey, RemovedLinkedProgram))
		{
			INC_DWORD_STAT(STAT_OpenGLShaderLRUScopeEvictedProgramCount);
			EvictFromLRU(RemovedLinkedProgram);
		}
	}

	FGLProgramCacheLRU() : LRUBinaryMemoryUse(0), LRU(LRUCapacity)
	{
	}

	bool IsLRUAtCapacity() const
	{
		return LRU.Num() == CVarLRUMaxProgramCount.GetValueOnAnyThread() || LRU.Num() == LRU.Max() || LRUBinaryMemoryUse > CVarLRUMaxProgramBinarySize.GetValueOnAnyThread();
	}

	void Add(const FOpenGLProgramKey& ProgramKey, FOpenGLLinkedProgram* LinkedProgram)
	{
		// Remove least recently used programs until we reach our limit.
		// note that a single large shader could evict multiple smaller shaders.
		checkf(!LRU.Contains(ProgramKey), TEXT("Program is already in the LRU program list: %s"), *ProgramKey.ToString());
		checkf(!IsEvicted(ProgramKey), TEXT("Program is already in the evicted program list: %s"), *ProgramKey.ToString());

		// UE_LOG(LogRHI, Warning, TEXT("LRU: adding program %s (%d)"), *ProgramKey.ToString(), LinkedProgram->Program);

		while (IsLRUAtCapacity())
		{
			EvictLeastRecentFromLRU();
		}

		LinkedProgram->LRUInfo.LRUNode = LRU.Add(ProgramKey, LinkedProgram);
		FDelayedEvictionContainer::OnProgramTouched(LinkedProgram);
		LRUBinaryMemoryUse += GetProgramBinarySize(LinkedProgram->Program);
		INC_DWORD_STAT(STAT_OpenGLShaderLRUProgramCount);
	}

	void AddAsEvicted(const FOpenGLProgramKey& ProgramKey, TArray<uint8>&& ProgramBinary)
	{
		checkf(!LRU.Contains(ProgramKey), TEXT("Program is already in the LRU program list: %s"), *ProgramKey.ToString());
		checkf(!IsEvicted(ProgramKey), TEXT("Program is already in the evicted program list: %s"), *ProgramKey.ToString());

		FEvictedGLProgram& test = EvictedPrograms.Emplace(ProgramKey, FEvictedGLProgram(ProgramKey, MoveTemp(ProgramBinary)));

		// UE_LOG(LogRHI, Warning, TEXT("LRU: adding EVICTED program %s"), *ProgramKey.ToString());

		INC_DWORD_STAT(STAT_OpenGLShaderLRUEvictedProgramCount);
	}

	FOpenGLLinkedProgram* Find(const FOpenGLProgramKey& ProgramKey, bool bFindAndCreateEvictedProgram)
	{
		// if it's in LRU pop to top.
		FOpenGLLinkedProgram *const * Found = LRU.FindAndTouch(ProgramKey);
		if (Found)
		{
			check((*Found)->LRUInfo.LRUNode.IsValidId());
			//UE_LOG(LogRHI, Warning, TEXT("LRU: ::Find program %d exists in LRU!"), (*Found)->Program);
			return *Found;
		}

		if( bFindAndCreateEvictedProgram )
		{
			return FindEvictedAndUpdateLRU(ProgramKey);
		}
		else
		{
			return FindEvicted(ProgramKey);
		}
	}

	FORCEINLINE_DEBUGGABLE void Touch(FOpenGLLinkedProgram* LinkedProgram)
	{
		if(LinkedProgram->LRUInfo.LRUNode.IsValidId())
		{
			LRU.MarkAsRecent(LinkedProgram->LRUInfo.LRUNode);
		}
		else
		{
			// This must find the program.
			ensure(FindEvictedAndUpdateLRU(LinkedProgram->Config.ProgramKey));
		}
		FDelayedEvictionContainer::OnProgramTouched(LinkedProgram);
	}

	void Empty()
	{
		// delete all FOpenGLLinkedPrograms from evicted container
		for(FOpenGLEvictedProgramsMap::TIterator It(EvictedPrograms); It; ++It )
		{
			FOpenGLLinkedProgram* LinkedProgram = It.Value().GetLinkedProgram();
			delete LinkedProgram;
		}
		EvictedPrograms.Empty();

		// delete all FOpenGLLinkedPrograms from LRU
		for (FOpenGLProgramLRUCache::TIterator It(LRU); It; ++It)
		{
			delete It.Value();
		}
		LRU.Empty(LRUCapacity);
	}

	void EnumerateLinkedPrograms(TFunction<void(FOpenGLLinkedProgram*)> EnumFunc)
	{
		// delete all FOpenGLLinkedPrograms from evicted container
		for (FOpenGLEvictedProgramsMap::TIterator It(EvictedPrograms); It; ++It)
		{
			EnumFunc( It.Value().GetLinkedProgram());
		}
		// delete all FOpenGLLinkedPrograms from LRU
		for (FOpenGLProgramLRUCache::TIterator It(LRU); It; ++It)
		{
			EnumFunc(It.Value());
		}
	}

	FOpenGLProgramLRUCache LRU;
	FOpenGLEvictedProgramsMap EvictedPrograms;
};

typedef TMap<FOpenGLProgramKey, FOpenGLLinkedProgram*> FOpenGLProgramsMap;

// FGLProgramCache is a K/V store that holds on to all FOpenGLLinkedProgram created.
// It is implemented by either a TMap or an LRU cache that will limit the number of active GL programs at any one time.
// (LRU is used only to work around the mali driver's maximum shader heap size.)
class FGLProgramCache
{
	FGLProgramCacheLRU ProgramCacheLRU;
	FOpenGLProgramsMap ProgramCache;
	bool bUseLRUCache;
public:
	FGLProgramCache()
	{
		if(CVarEnableLRU.GetValueOnAnyThread() && !FOpenGL::SupportsProgramBinary())
		{
			UE_LOG(LogRHI, Warning, TEXT("Requesting OpenGL program LRU cache, but program binary is not supported by driver. Falling back to non-lru cache."));
		}

		bUseLRUCache = CVarEnableLRU.GetValueOnAnyThread() == 1 && FOpenGL::SupportsProgramBinary() && !FOpenGL::SupportsSeparateShaderObjects();
		UE_LOG(LogRHI, Log, TEXT("Using OpenGL program LRU cache: %d"), bUseLRUCache ? 1 : 0);
	}

	FORCEINLINE_DEBUGGABLE bool IsUsingLRU() const
	{
		return bUseLRUCache;
	}

	FORCEINLINE_DEBUGGABLE void Touch(FOpenGLLinkedProgram* LinkedProgram)
	{
		if (bUseLRUCache)
		{
			ProgramCacheLRU.Touch(LinkedProgram);
		}
	}

	FORCEINLINE_DEBUGGABLE FOpenGLLinkedProgram* Find(const FOpenGLProgramKey& ProgramKey, bool bFindAndCreateEvictedProgram)
	{
		if (bUseLRUCache)
		{
			return ProgramCacheLRU.Find(ProgramKey, bFindAndCreateEvictedProgram);
		}
		else
		{
			FOpenGLLinkedProgram** FoundProgram = ProgramCache.Find(ProgramKey);
			return FoundProgram ? *FoundProgram : nullptr;
		}
	}

	FORCEINLINE_DEBUGGABLE void Add(const FOpenGLProgramKey& ProgramKey, FOpenGLLinkedProgram* LinkedProgram)
	{
		if (bUseLRUCache)
		{
			ProgramCacheLRU.Add(ProgramKey, LinkedProgram);
		}
		else
		{
			check(!ProgramCache.Contains(ProgramKey));
			ProgramCache.Add(ProgramKey, LinkedProgram);
		}
	}

	void Empty()
	{
		if (bUseLRUCache)
		{
			ProgramCacheLRU.Empty();
		}
		else
		{
			// delete all FOpenGLLinkedPrograms from ProgramCache
			for (FOpenGLProgramsMap::TIterator It(ProgramCache); It; ++It)
			{
				delete It.Value();
			}
			ProgramCache.Empty();
		}
	}

	bool IsLRUAtCapacity() const
	{
		if (bUseLRUCache)
		{
			ProgramCacheLRU.IsLRUAtCapacity();
		}

		return false;
	}

	void EvictMostRecent()
	{
		check(IsUsingLRU());
		if( ProgramCacheLRU.LRU.Num() )
		{
			ProgramCacheLRU.EvictMostRecentFromLRU();
		}
	}

	void EvictProgram(const FOpenGLProgramKey& ProgramKey)
	{
		check(IsUsingLRU());
		ProgramCacheLRU.EvictProgramFromLRU(ProgramKey);
	}

	void AddAsEvicted(const FOpenGLProgramKey& ProgramKey, TArray<uint8>&& ProgramBinary)
	{
		check(IsUsingLRU());
		ProgramCacheLRU.AddAsEvicted(ProgramKey, MoveTemp(ProgramBinary));
	}

	bool IsEvicted(const FOpenGLProgramKey& ProgramKey)
	{
		check(IsUsingLRU());
		return ProgramCacheLRU.IsEvicted(ProgramKey);
	}

	void EnumerateLinkedPrograms(TFunction<void(FOpenGLLinkedProgram*)> EnumFunc)
	{
		if (bUseLRUCache)
		{
			ProgramCacheLRU.EnumerateLinkedPrograms(EnumFunc);
		}
		else
		{
			// all programs are retained in map.
			for (FOpenGLProgramsMap::TIterator It(ProgramCache); It; ++It)
			{
				EnumFunc(It.Value());
			}
		}
	}
};

static FGLProgramCache& GetOpenGLProgramsCache()
{
	static FGLProgramCache ProgramsCache;
	return ProgramsCache;
}


void FDelayedEvictionContainer::Init()
{
	const int32 EvictLatencyTicks = GEvictOnBSSDestructLatency;
	const int32 NumLatencyBuckets = 3;
	TotalBuckets = NumLatencyBuckets + 1;
	Buckets.SetNum(TotalBuckets);
	TimePerBucket = (EvictLatencyTicks)/(NumLatencyBuckets-1);
	CurrentBucketTickCount = TimePerBucket;
	NewProgramBucket = 0;
	EvictBucketIndex = 1;
}

void FDelayedEvictionContainer::Add(FOpenGLLinkedProgram* LinkedProgram)
{
	if (GEvictOnBSSDestructLatency == 0)
	{
		GetOpenGLProgramsCache().EvictProgram(LinkedProgram->Config.ProgramKey);
		return;
	}

	checkf(!GetOpenGLProgramsCache().IsEvicted(LinkedProgram->Config.ProgramKey), TEXT("FDelayedEvictionContainer::Add is already evicted! [%s], %d"), *LinkedProgram->Config.ProgramKey.ToString(), LinkedProgram->LRUInfo.EvictBucket);

	if (LinkedProgram->LRUInfo.EvictBucket >=0 )
	{
		Remove(LinkedProgram);
	}
	Buckets[NewProgramBucket].ProgramsToEvict.Add(LinkedProgram);
	LinkedProgram->LRUInfo.EvictBucket = NewProgramBucket;
}

void FDelayedEvictionContainer::Remove(FOpenGLLinkedProgram* RemoveMe)
{
	if (GEvictOnBSSDestructLatency == 0)
	{
		return;
	}
	check(RemoveMe->LRUInfo.EvictBucket >= 0);
	ensure( Buckets[RemoveMe->LRUInfo.EvictBucket].ProgramsToEvict.Remove(RemoveMe) == 1 );
	RemoveMe->LRUInfo.EvictBucket = -1;
}

void FDelayedEvictionContainer::Tick()
{
	if (GEvictOnBSSDestructLatency == 0)
	{
		return;
	}

	FDelayEvictBucket& EvictionBucket = Buckets[EvictBucketIndex];

	const int32 NumToFree = EvictionBucket.ProgramsToEvict.Num();
	if (NumToFree)
	{
		auto It = EvictionBucket.ProgramsToEvict.CreateIterator();
		for (int32 i= FMath::Min(EvictionBucket.NumToFreePerTick, NumToFree)-1;i>=0;i--)
		{
			FOpenGLLinkedProgram* LinkedProgram = *It;
			It.RemoveCurrent();
			++It;
			bMeasureEviction = true;
			check(LinkedProgram->LRUInfo.EvictBucket == EvictBucketIndex);
			LinkedProgram->LRUInfo.EvictBucket = -3; // Mark EvictBucket to indicated evicted from ProgramsToEvict, Prevent EvictProgram from attempting to remove again.
			GetOpenGLProgramsCache().EvictProgram(LinkedProgram->Config.ProgramKey);
			bMeasureEviction = false;
		}
	}

	if (--CurrentBucketTickCount == 0)
	{
		check(EvictionBucket.ProgramsToEvict.Num() == 0);
		EvictBucketIndex = (EvictBucketIndex+1) % Buckets.Num();
		NewProgramBucket = (NewProgramBucket+1) % Buckets.Num();
		CurrentBucketTickCount = TimePerBucket;
		Buckets[EvictBucketIndex].NumToFreePerTick = (Buckets[EvictBucketIndex].ProgramsToEvict.Num() -1)/TimePerBucket + 1;
	}
}

// This short queue preceding released programs cache is here because usually the programs are requested again
// very shortly after they're released, so looking through recently released programs first provides tangible
// performance improvement.

#define LAST_RELEASED_PROGRAMS_CACHE_COUNT 10

static FOpenGLLinkedProgram* StaticLastReleasedPrograms[LAST_RELEASED_PROGRAMS_CACHE_COUNT] = { 0 };
static int32 StaticLastReleasedProgramsIndex = 0;

// ============================================================================================================================

static int32 CountSetBits(const TBitArray<>& Array)
{
	int32 Result = 0;
	for (TBitArray<>::FConstIterator BitIt(Array); BitIt; ++BitIt)
	{
		Result += BitIt.GetValue();
	}
	return Result;
}

FORCEINLINE_DEBUGGABLE void FOpenGLLinkedProgram::VerifyUniformBlockBindings( int Stage, uint32 FirstUniformBuffer )
{
	if ( FOpenGL::SupportsSeparateShaderObjects() && FOpenGL::SupportsUniformBuffers() )
	{
		VERIFY_GL_SCOPE();
		FOpenGLUniformName Name;
		Name.Buffer[0] = CrossCompiler::ShaderStageIndexToTypeName(Stage);
		Name.Buffer[1] = 'b';
		
		GLuint StageProgram = Config.Shaders[Stage].Resource;

		for (int32 BufferIndex = 0; BufferIndex < Config.Shaders[Stage].Bindings.NumUniformBuffers; ++BufferIndex)
		{
			SetIndex(Name.Buffer, 2, BufferIndex);
			GLint Location = GetOpenGLProgramUniformBlockIndex(StageProgram, Name);
			if (Location >= 0)
			{
				GetOpenGLProgramUniformBlockBinding(StageProgram, Location, FirstUniformBuffer + BufferIndex);
			}
		}
	}
}

void FOpenGLLinkedProgram::ConfigureShaderStage( int Stage, uint32 FirstUniformBuffer )
{
	static const GLint FirstTextureUnit[CrossCompiler::NUM_SHADER_STAGES] =
	{
		FOpenGL::GetFirstVertexTextureUnit(),
		FOpenGL::GetFirstPixelTextureUnit(),
		FOpenGL::GetFirstGeometryTextureUnit(),
		FOpenGL::GetFirstHullTextureUnit(),
		FOpenGL::GetFirstDomainTextureUnit(),
		FOpenGL::GetFirstComputeTextureUnit()
	};
	static const GLint FirstUAVUnit[CrossCompiler::NUM_SHADER_STAGES] =
	{
		OGL_UAV_NOT_SUPPORTED_FOR_GRAPHICS_UNIT,
		FOpenGL::GetFirstPixelUAVUnit(),
		OGL_UAV_NOT_SUPPORTED_FOR_GRAPHICS_UNIT,
		OGL_UAV_NOT_SUPPORTED_FOR_GRAPHICS_UNIT,
		OGL_UAV_NOT_SUPPORTED_FOR_GRAPHICS_UNIT,
		FOpenGL::GetFirstComputeUAVUnit()
	};
	
	// verify that only CS and PS uses UAVs
	check(!(Stage == CrossCompiler::SHADER_STAGE_COMPUTE || Stage == CrossCompiler::SHADER_STAGE_PIXEL) ? (CountSetBits(UAVStageNeeds) == 0) : true);

	SCOPE_CYCLE_COUNTER(STAT_OpenGLShaderBindParameterTime);
	VERIFY_GL_SCOPE();

	FOpenGLUniformName Name;
	Name.Buffer[0] = CrossCompiler::ShaderStageIndexToTypeName(Stage);

	GLuint StageProgram = FOpenGL::SupportsSeparateShaderObjects() ? Config.Shaders[Stage].Resource : Program;
	
	// Bind Global uniform arrays (vu_h, pu_i, etc)
	{
		Name.Buffer[1] = 'u';
		Name.Buffer[2] = '_';
		Name.Buffer[3] = 0;
		Name.Buffer[4] = 0;

		TArray<FPackedUniformInfo> PackedUniformInfos;
		for (uint8 Index = 0; Index < CrossCompiler::PACKED_TYPEINDEX_MAX; ++Index)
		{
			uint8 ArrayIndexType = CrossCompiler::PackedTypeIndexToTypeName(Index);
			Name.Buffer[3] = ArrayIndexType;
			GLint Location = glGetUniformLocation(StageProgram, Name.Buffer);
			if ((int32)Location != -1)
			{
				FPackedUniformInfo Info = {Location, ArrayIndexType, Index};
				PackedUniformInfos.Add(Info);
			}
		}

		SortPackedUniformInfos(PackedUniformInfos, Config.Shaders[Stage].Bindings.PackedGlobalArrays, StagePackedUniformInfo[Stage].PackedUniformInfos);
	}

	// Bind uniform buffer packed arrays (vc0_h, pc2_i, etc)
	{
		Name.Buffer[1] = 'c';
		Name.Buffer[2] = 0;
		Name.Buffer[3] = 0;
		Name.Buffer[4] = 0;
		Name.Buffer[5] = 0;
		Name.Buffer[6] = 0;

		check(StagePackedUniformInfo[Stage].PackedUniformBufferInfos.Num() == 0);
		int32 NumUniformBuffers = Config.Shaders[Stage].Bindings.NumUniformBuffers;
		StagePackedUniformInfo[Stage].PackedUniformBufferInfos.SetNum(NumUniformBuffers);
		int32 NumPackedUniformBuffers = Config.Shaders[Stage].Bindings.PackedUniformBuffers.Num();
		check(NumPackedUniformBuffers <= NumUniformBuffers);

		for (int32 UB = 0; UB < NumPackedUniformBuffers; ++UB)
		{
			const TArray<CrossCompiler::FPackedArrayInfo>& PackedInfo = Config.Shaders[Stage].Bindings.PackedUniformBuffers[UB];
			TArray<FPackedUniformInfo>& PackedBuffers = StagePackedUniformInfo[Stage].PackedUniformBufferInfos[UB];

			ANSICHAR* Str = SetIndex(Name.Buffer, 2, UB);
			*Str++ = '_';
			Str[1] = 0;
			for (uint8 Index = 0; Index < PackedInfo.Num(); ++Index)
			{
				Str[0] = PackedInfo[Index].TypeName;
				GLint Location = glGetUniformLocation(StageProgram, Name.Buffer); // This could be -1 if optimized out
				FPackedUniformInfo Info = {Location, PackedInfo[Index].TypeName,  PackedInfo[Index].TypeIndex};
				PackedBuffers.Add(Info);
			}
		}
	}

	// Reserve and setup Space for Emulated Uniform Buffers
	StagePackedUniformInfo[Stage].LastEmulatedUniformBufferSet.Empty(Config.Shaders[Stage].Bindings.NumUniformBuffers);
	StagePackedUniformInfo[Stage].LastEmulatedUniformBufferSet.AddZeroed(Config.Shaders[Stage].Bindings.NumUniformBuffers);

	// Bind samplers.
	Name.Buffer[1] = 's';
	Name.Buffer[2] = 0;
	Name.Buffer[3] = 0;
	Name.Buffer[4] = 0;
	int32 LastFoundIndex = -1;
	for (int32 SamplerIndex = 0; SamplerIndex < Config.Shaders[Stage].Bindings.NumSamplers; ++SamplerIndex)
	{
		SetIndex(Name.Buffer, 2, SamplerIndex);
		GLint Location = glGetUniformLocation(StageProgram, Name.Buffer);
		if (Location == -1)
		{
			if (LastFoundIndex != -1)
			{
				// It may be an array of samplers. Get the initial element location, if available, and count from it.
				SetIndex(Name.Buffer, 2, LastFoundIndex);
				int32 OffsetOfArraySpecifier = (LastFoundIndex>9)?4:3;
				int32 ArrayIndex = SamplerIndex-LastFoundIndex;
				Name.Buffer[OffsetOfArraySpecifier] = '[';
				ANSICHAR* EndBracket = SetIndex(Name.Buffer, OffsetOfArraySpecifier+1, ArrayIndex);
				*EndBracket++ = ']';
				*EndBracket = 0;
				Location = glGetUniformLocation(StageProgram, Name.Buffer);
			}
		}
		else
		{
			LastFoundIndex = SamplerIndex;
		}

		if (Location != -1)
		{
			if ( OpenGLConsoleVariables::bBindlessTexture == 0 || !FOpenGL::SupportsBindlessTexture())
			{
				// Non-bindless, setup the unit info
				FOpenGL::ProgramUniform1i(StageProgram, Location, FirstTextureUnit[Stage] + SamplerIndex);
				TextureStageNeeds[ FirstTextureUnit[Stage] + SamplerIndex ] = true;
				MaxTextureStage = FMath::Max( MaxTextureStage, FirstTextureUnit[Stage] + SamplerIndex);
			}
			else
			{
				//Bindless, save off the slot information
				FOpenGLBindlessSamplerInfo Info;
				Info.Handle = Location;
				Info.Slot = FirstTextureUnit[Stage] + SamplerIndex;
				Samplers.Add(Info);
			}
		}
	}

	// Bind UAVs/images.
	Name.Buffer[1] = 'i';
	Name.Buffer[2] = 0;
	Name.Buffer[3] = 0;
	Name.Buffer[4] = 0;
	int32 LastFoundUAVIndex = -1;
	for (int32 UAVIndex = 0; UAVIndex < Config.Shaders[Stage].Bindings.NumUAVs; ++UAVIndex)
	{
		SetIndex(Name.Buffer, 2, UAVIndex);
		GLint Location = glGetUniformLocation(StageProgram, Name.Buffer);
		if (Location == -1)
		{
			if (LastFoundUAVIndex != -1)
			{
				// It may be an array of UAVs. Get the initial element location, if available, and count from it.
				SetIndex(Name.Buffer, 2, LastFoundUAVIndex);
				int32 OffsetOfArraySpecifier = (LastFoundUAVIndex>9)?4:3;
				int32 ArrayIndex = UAVIndex-LastFoundUAVIndex;
				Name.Buffer[OffsetOfArraySpecifier] = '[';
				ANSICHAR* EndBracket = SetIndex(Name.Buffer, OffsetOfArraySpecifier+1, ArrayIndex);
				*EndBracket++ = ']';
				*EndBracket = '\0';
				Location = glGetUniformLocation(StageProgram, Name.Buffer);
			}
		}
		else
		{
			LastFoundUAVIndex = UAVIndex;
		}

		if (Location != -1)
		{
			// compute shaders have layout(binding) for images
			// glUniform1i(Location, FirstUAVUnit[Stage] + UAVIndex);
			
			UAVStageNeeds[ FirstUAVUnit[Stage] + UAVIndex ] = true;
			MaxUAVUnitUsed = FMath::Max(MaxUAVUnitUsed, FirstUAVUnit[Stage] + UAVIndex);
		}
	}

	// Bind uniform buffers.
	if (FOpenGL::SupportsUniformBuffers())
	{
		Name.Buffer[1] = 'b';
		Name.Buffer[2] = 0;
		Name.Buffer[3] = 0;
		Name.Buffer[4] = 0;
		for (int32 BufferIndex = 0; BufferIndex < Config.Shaders[Stage].Bindings.NumUniformBuffers; ++BufferIndex)
		{
			SetIndex(Name.Buffer, 2, BufferIndex);
			GLint Location = GetOpenGLProgramUniformBlockIndex(StageProgram, Name);
			if (Location >= 0)
			{
				GetOpenGLProgramUniformBlockBinding(StageProgram, Location, FirstUniformBuffer + BufferIndex);
			}
		}
	}
}

#if ENABLE_UNIFORM_BUFFER_LAYOUT_VERIFICATION

#define ENABLE_UNIFORM_BUFFER_LAYOUT_NAME_MANGLING_CL1862097 1
/*
	As of CL 1862097 uniform buffer names are mangled to avoid collisions between variables referenced
	in different shaders of the same program

	layout(std140) uniform _vb0
	{
	#define View View_vb0
	anon_struct_0000 View;
	};

	layout(std140) uniform _vb1
	{
	#define Primitive Primitive_vb1
	anon_struct_0001 Primitive;
	};
*/
	

struct UniformData
{
	UniformData(uint32 InOffset, uint32 InArrayElements)
		: Offset(InOffset)
		, ArrayElements(InArrayElements)
	{
	}
	uint32 Offset;
	uint32 ArrayElements;

	bool operator == (const UniformData& RHS) const
	{
		return	Offset == RHS.Offset &&	ArrayElements == RHS.ArrayElements;
	}
	bool operator != (const UniformData& RHS) const
	{
		return	!(*this == RHS);
	}
};
#if ENABLE_UNIFORM_BUFFER_LAYOUT_NAME_MANGLING_CL1862097
static void VerifyUniformLayout(const FString& BlockName, const TCHAR* UniformName, const UniformData& GLSLUniform)
#else
static void VerifyUniformLayout(const TCHAR* UniformName, const UniformData& GLSLUniform)
#endif //#if ENABLE_UNIFORM_BUFFER_LAYOUT_NAME_MANGLING_CL1862097
{
	static TMap<FString, UniformData> Uniforms;

	if(!Uniforms.Num())
	{
		for (TLinkedList<FShaderParametersMetadata*>::TIterator StructIt(FShaderParametersMetadata::GetStructList()); StructIt; StructIt.Next())
		{
#if ENABLE_UNIFORM_BUFFER_LAYOUT_DUMP
			UE_LOG(LogRHI, Log, TEXT("UniformBufferStruct %s %s %d"),
				StructIt->GetStructTypeName(),
				StructIt->GetShaderVariableName(),
				StructIt->GetSize()
				);
#endif  // #if ENABLE_UNIFORM_BUFFER_LAYOUT_DUMP
			const TArray<FShaderParametersMetadata::FMember>& StructMembers = StructIt->GetMembers();
			for(int32 MemberIndex = 0;MemberIndex < StructMembers.Num();++MemberIndex)
			{
				const FShaderParametersMetadata::FMember& Member = StructMembers[MemberIndex];

				FString BaseTypeName;
				switch(Member.GetBaseType())
				{
					case UBMT_NESTED_STRUCT:  BaseTypeName = TEXT("struct");  break;
					case UBMT_INT32:   BaseTypeName = TEXT("int"); break;
					case UBMT_UINT32:  BaseTypeName = TEXT("uint"); break;
					case UBMT_FLOAT32: BaseTypeName = TEXT("float"); break;
					case UBMT_TEXTURE: BaseTypeName = TEXT("texture"); break;
					case UBMT_SAMPLER: BaseTypeName = TEXT("sampler"); break;
					default:           UE_LOG(LogShaders, Fatal,TEXT("Unrecognized uniform buffer struct member base type."));
				};
#if ENABLE_UNIFORM_BUFFER_LAYOUT_DUMP
				UE_LOG(LogRHI, Log, TEXT("  +%d %s%dx%d %s[%d]"),
					Member.GetOffset(),
					*BaseTypeName,
					Member.GetNumRows(),
					Member.GetNumColumns(),
					Member.GetName(),
					Member.GetNumElements()
					);
#endif // #if ENABLE_UNIFORM_BUFFER_LAYOUT_DUMP
				FString CompositeName = FString(StructIt->GetShaderVariableName()) + TEXT("_") + Member.GetName();

				// GLSL returns array members with a "[0]" suffix
				if(Member.GetNumElements())
				{
					CompositeName += TEXT("[0]");
				}

				check(!Uniforms.Contains(CompositeName));
				Uniforms.Add(CompositeName, UniformData(Member.GetOffset(), Member.GetNumElements()));
			}
		}
	}

#if ENABLE_UNIFORM_BUFFER_LAYOUT_NAME_MANGLING_CL1862097
	/* unmangle the uniform name by stripping the block name from it
	
	layout(std140) uniform _vb0
	{
	#define View View_vb0
		anon_struct_0000 View;
	};
	*/
	FString RequestedUniformName(UniformName);
	RequestedUniformName = RequestedUniformName.Replace(*BlockName, TEXT(""));
	if(RequestedUniformName.StartsWith(TEXT("."), ESearchCase::CaseSensitive))
	{
		RequestedUniformName.RightChopInline(1, false);
	}
#else
	FString RequestedUniformName = UniformName;
#endif

	const UniformData* FoundUniform = Uniforms.Find(RequestedUniformName);

	// MaterialTemplate uniform buffer does not have an entry in the FShaderParametersMetadatas list, so skipping it here
	if(!(RequestedUniformName.StartsWith("Material_") || RequestedUniformName.StartsWith("MaterialCollection")))
	{
		if(!FoundUniform || (*FoundUniform != GLSLUniform))
		{
			UE_LOG(LogRHI, Fatal, TEXT("uniform buffer member %s in the GLSL source doesn't match it's declaration in it's FShaderParametersMetadata"), *RequestedUniformName);
		}
	}
}

static void VerifyUniformBufferLayouts(GLuint Program)
{
	GLint NumBlocks = 0;
	glGetProgramiv(Program, GL_ACTIVE_UNIFORM_BLOCKS, &NumBlocks);

#if ENABLE_UNIFORM_BUFFER_LAYOUT_DUMP
	UE_LOG(LogRHI, Log, TEXT("program %d has %d uniform blocks"), Program, NumBlocks);
#endif  // #if ENABLE_UNIFORM_BUFFER_LAYOUT_DUMP

	for(GLint BlockIndex = 0; BlockIndex < NumBlocks; ++BlockIndex)
	{
		const GLsizei BufferSize = 256;
		char Buffer[BufferSize] = {0};
		GLsizei Length = 0;

		GLint ActiveUniforms = 0;
		GLint BlockBytes = 0;

		glGetActiveUniformBlockiv(Program, BlockIndex, GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS, &ActiveUniforms);
		glGetActiveUniformBlockiv(Program, BlockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &BlockBytes);
		glGetActiveUniformBlockName(Program, BlockIndex, BufferSize, &Length, Buffer);

#if ENABLE_UNIFORM_BUFFER_LAYOUT_NAME_MANGLING_CL1862097
		FString BlockName(Buffer);
#endif // #if ENABLE_UNIFORM_BUFFER_LAYOUT_NAME_MANGLING_CL1862097

		FString ReferencedBy;
		{
			GLint ReferencedByVS = 0;
			GLint ReferencedByPS = 0;
			GLint ReferencedByGS = 0;
			GLint ReferencedByHS = 0;
			GLint ReferencedByDS = 0;
			GLint ReferencedByCS = 0;

			glGetActiveUniformBlockiv(Program, BlockIndex, GL_UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER, &ReferencedByVS);
			glGetActiveUniformBlockiv(Program, BlockIndex, GL_UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER, &ReferencedByPS);
#ifdef GL_UNIFORM_BLOCK_REFERENCED_BY_GEOMETRY_SHADER
			glGetActiveUniformBlockiv(Program, BlockIndex, GL_UNIFORM_BLOCK_REFERENCED_BY_GEOMETRY_SHADER, &ReferencedByGS);
#endif
			if (GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5)
			{
#ifdef GL_UNIFORM_BLOCK_REFERENCED_BY_TESS_CONTROL_SHADER
				glGetActiveUniformBlockiv(Program, BlockIndex, GL_UNIFORM_BLOCK_REFERENCED_BY_TESS_CONTROL_SHADER, &ReferencedByHS);
				glGetActiveUniformBlockiv(Program, BlockIndex, GL_UNIFORM_BLOCK_REFERENCED_BY_TESS_EVALUATION_SHADER, &ReferencedByDS);
#endif
			}
			
			if (RHISupportsComputeShaders(GMaxRHIShaderPlatform))
			{
#ifdef GL_UNIFORM_BLOCK_REFERENCED_BY_COMPUTE_SHADER
				glGetActiveUniformBlockiv(Program, BlockIndex, GL_UNIFORM_BLOCK_REFERENCED_BY_COMPUTE_SHADER, &ReferencedByCS);
#endif
			}

			if(ReferencedByVS) {ReferencedBy += TEXT("V");}
			if(ReferencedByHS) {ReferencedBy += TEXT("H");}
			if(ReferencedByDS) {ReferencedBy += TEXT("D");}
			if(ReferencedByGS) {ReferencedBy += TEXT("G");}
			if(ReferencedByPS) {ReferencedBy += TEXT("P");}
			if(ReferencedByCS) {ReferencedBy += TEXT("C");}
		}
#if ENABLE_UNIFORM_BUFFER_LAYOUT_DUMP
		UE_LOG(LogRHI, Log, TEXT("  [%d] uniform block (%s) = %s, %d active uniforms, %d bytes {"),
			BlockIndex,
			*ReferencedBy,
			ANSI_TO_TCHAR(Buffer),
			ActiveUniforms,
			BlockBytes
			);
#endif // #if ENABLE_UNIFORM_BUFFER_LAYOUT_DUMP
		if(ActiveUniforms)
		{
			// the other TArrays copy construct this to get the proper array size
			TArray<GLint> ActiveUniformIndices;
			ActiveUniformIndices.Init(ActiveUniforms);

			glGetActiveUniformBlockiv(Program, BlockIndex, GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES, ActiveUniformIndices.GetData());
			
			TArray<GLint> ActiveUniformOffsets(ActiveUniformIndices);
			glGetActiveUniformsiv(Program, ActiveUniforms, reinterpret_cast<const GLuint*>(ActiveUniformIndices.GetData()), GL_UNIFORM_OFFSET, ActiveUniformOffsets.GetData());

			TArray<GLint> ActiveUniformSizes(ActiveUniformIndices);
			glGetActiveUniformsiv(Program, ActiveUniforms, reinterpret_cast<const GLuint*>(ActiveUniformIndices.GetData()), GL_UNIFORM_SIZE, ActiveUniformSizes.GetData());

			TArray<GLint> ActiveUniformTypes(ActiveUniformIndices);
			glGetActiveUniformsiv(Program, ActiveUniforms, reinterpret_cast<const GLuint*>(ActiveUniformIndices.GetData()), GL_UNIFORM_TYPE, ActiveUniformTypes.GetData());

			TArray<GLint> ActiveUniformArrayStrides(ActiveUniformIndices);
			glGetActiveUniformsiv(Program, ActiveUniforms, reinterpret_cast<const GLuint*>(ActiveUniformIndices.GetData()), GL_UNIFORM_ARRAY_STRIDE, ActiveUniformArrayStrides.GetData());

			extern const TCHAR* GetGLUniformTypeString( GLint UniformType );

			for(GLint i = 0; i < ActiveUniformIndices.Num(); ++i)
			{
				const GLint UniformIndex = ActiveUniformIndices[i];
				GLsizei Size = 0;
				GLenum Type = 0;
				glGetActiveUniform(Program, UniformIndex , BufferSize, &Length, &Size, &Type, Buffer);

#if ENABLE_UNIFORM_BUFFER_LAYOUT_DUMP
				UE_LOG(LogRHI, Log, TEXT("    [%d] +%d %s %s %d elements %d array stride"),
					UniformIndex,
					ActiveUniformOffsets[i],
					GetGLUniformTypeString(ActiveUniformTypes[i]),
					ANSI_TO_TCHAR(Buffer),
					ActiveUniformSizes[i],
					ActiveUniformArrayStrides[i]
				);
#endif // #if ENABLE_UNIFORM_BUFFER_LAYOUT_DUMP
		
				const UniformData GLSLUniform
				(
					ActiveUniformOffsets[i],
					ActiveUniformArrayStrides[i] > 0 ? ActiveUniformSizes[i] : 0 // GLSL has 1 as array size for non-array uniforms, but FShaderParametersMetadata assumes 0
				);
#if ENABLE_UNIFORM_BUFFER_LAYOUT_NAME_MANGLING_CL1862097
				VerifyUniformLayout(BlockName, ANSI_TO_TCHAR(Buffer), GLSLUniform);
#else
				VerifyUniformLayout(ANSI_TO_TCHAR(Buffer), GLSLUniform);
#endif
			}
		}
	}
}
#endif  // #if ENABLE_UNIFORM_BUFFER_LAYOUT_VERIFICATION
#define PROGRAM_BINARY_RETRIEVABLE_HINT             0x8257

/**
 * Link vertex and pixel shaders in to an OpenGL program.
 */
static FOpenGLLinkedProgram* LinkProgram( const FOpenGLLinkedProgramConfiguration& Config, bool bFromPSOFileCache)
{
	ANSICHAR Buf[32] = {0};

	SCOPE_CYCLE_COUNTER(STAT_OpenGLShaderLinkTime);
	VERIFY_GL_SCOPE();

	// ensure that compute shaders are always alone
	check( (Config.Shaders[CrossCompiler::SHADER_STAGE_VERTEX].Resource == 0) != (Config.Shaders[CrossCompiler::SHADER_STAGE_COMPUTE].Resource == 0));
	check( (Config.Shaders[CrossCompiler::SHADER_STAGE_PIXEL].Resource == 0) != (Config.Shaders[CrossCompiler::SHADER_STAGE_COMPUTE].Resource == 0));

	TArray<uint8> CachedProgramBinary;
	GLuint Program = 0;
	bool bShouldLinkProgram = true;
	if (FOpenGLProgramBinaryCache::IsEnabled())
	{
		// Try to create program from a saved binary
		bShouldLinkProgram = !FOpenGLProgramBinaryCache::UseCachedProgram(Program, Config.ProgramKey, CachedProgramBinary);
		if (bShouldLinkProgram)
		{
			// In case there is no saved binary in the cache, compile required shaders we have deferred before
			FOpenGLProgramBinaryCache::CompilePendingShaders(Config);
		}
	}

	if (Program == 0)
	{
		FOpenGL::GenProgramPipelines(1, &Program);
	}

	if (bShouldLinkProgram)
	{
		if (Config.Shaders[CrossCompiler::SHADER_STAGE_VERTEX].Resource)
		{
			FOpenGL::UseProgramStages(Program, GL_VERTEX_SHADER_BIT, Config.Shaders[CrossCompiler::SHADER_STAGE_VERTEX].Resource);
		}
		if (Config.Shaders[CrossCompiler::SHADER_STAGE_PIXEL].Resource)
		{
			FOpenGL::UseProgramStages(Program, GL_FRAGMENT_SHADER_BIT, Config.Shaders[CrossCompiler::SHADER_STAGE_PIXEL].Resource);
		}
		if (Config.Shaders[CrossCompiler::SHADER_STAGE_GEOMETRY].Resource)
		{
			FOpenGL::UseProgramStages(Program, GL_GEOMETRY_SHADER_BIT, Config.Shaders[CrossCompiler::SHADER_STAGE_GEOMETRY].Resource);
		}
		if (Config.Shaders[CrossCompiler::SHADER_STAGE_HULL].Resource)
		{
			FOpenGL::UseProgramStages(Program, GL_TESS_CONTROL_SHADER_BIT, Config.Shaders[CrossCompiler::SHADER_STAGE_HULL].Resource);
		}
		if (Config.Shaders[CrossCompiler::SHADER_STAGE_DOMAIN].Resource)
		{
			FOpenGL::UseProgramStages(Program, GL_TESS_EVALUATION_SHADER_BIT, Config.Shaders[CrossCompiler::SHADER_STAGE_DOMAIN].Resource);
		}
		if (Config.Shaders[CrossCompiler::SHADER_STAGE_COMPUTE].Resource)
		{
			FOpenGL::UseProgramStages(Program, GL_COMPUTE_SHADER_BIT, Config.Shaders[CrossCompiler::SHADER_STAGE_COMPUTE].Resource);
		}
	
		if( !FOpenGL::SupportsSeparateShaderObjects() )
		{
			if(FOpenGLProgramBinaryCache::IsEnabled() || GetOpenGLProgramsCache().IsUsingLRU())
			{
				FOpenGL::ProgramParameter(Program, PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
			}

			// Link.
			glLinkProgram(Program);
		}
	}

	if (VerifyProgramPipeline(Program))
	{
		if(bShouldLinkProgram && !FOpenGL::SupportsSeparateShaderObjects())
		{
			SetNewProgramStats(Program);

			if (FOpenGLProgramBinaryCache::IsEnabled())
			{
				check(CachedProgramBinary.Num() == 0);
				FOpenGLProgramBinaryCache::CacheProgram(Program, Config.ProgramKey, CachedProgramBinary);
			}
		}
	}
	else
	{
		return nullptr;
	}
	
	FOpenGL::BindProgramPipeline(Program);

	bool bUsingTessellation = Config.Shaders[CrossCompiler::SHADER_STAGE_HULL].Resource && Config.Shaders[CrossCompiler::SHADER_STAGE_DOMAIN].Resource;
	FOpenGLLinkedProgram* LinkedProgram = new FOpenGLLinkedProgram(Config, Program, bUsingTessellation);

	if (GetOpenGLProgramsCache().IsUsingLRU() && CVarLRUKeepProgramBinaryResident.GetValueOnAnyThread() && CachedProgramBinary.Num())
	{
		// Store the binary data in LRUInfo, this avoids requesting a program binary from the driver when this program is evicted.
		INC_MEMORY_STAT_BY(STAT_OpenGLShaderLRUProgramMemory, CachedProgramBinary.Num());
		LinkedProgram->LRUInfo.CachedProgramBinary = MoveTemp(CachedProgramBinary);
	}
	ConfigureStageStates(LinkedProgram);

#if ENABLE_UNIFORM_BUFFER_LAYOUT_VERIFICATION
	VerifyUniformBufferLayouts(Program);
#endif // #if ENABLE_UNIFORM_BUFFER_LAYOUT_VERIFICATION
	return LinkedProgram;
}

static bool LinkComputeShader(FRHIComputeShader* ComputeShaderRHI, FOpenGLComputeShader* ComputeShader)
{
	check(ComputeShader);
	check(ComputeShader->Resource != 0);
	check(ComputeShaderRHI->GetHash() != FSHAHash());

	FOpenGLLinkedProgramConfiguration Config;
	Config.Shaders[CrossCompiler::SHADER_STAGE_COMPUTE].Resource = ComputeShader->Resource;
	Config.Shaders[CrossCompiler::SHADER_STAGE_COMPUTE].Bindings = ComputeShader->Bindings;
	Config.ProgramKey.ShaderHashes[CrossCompiler::SHADER_STAGE_COMPUTE] = ComputeShaderRHI->GetHash();

	ComputeShader->LinkedProgram = GetOpenGLProgramsCache().Find(Config.ProgramKey, true);

	if (ComputeShader->LinkedProgram == nullptr)
	{
		ComputeShader->LinkedProgram = LinkProgram(Config, false);
		if(ComputeShader->LinkedProgram == nullptr)
		{
		#if DEBUG_GL_SHADERS
			UE_LOG(LogRHI, Error, TEXT("Compute Shader:\n%s"), ANSI_TO_TCHAR(ComputeShader->GlslCode.GetData()));
		#endif //DEBUG_GL_SHADERS
			checkf(ComputeShader->LinkedProgram, TEXT("Compute shader failed to compile & link."));

			FName LinkFailurePanic = FName("FailedComputeProgramLink");
			RHIGetPanicDelegate().ExecuteIfBound(LinkFailurePanic);
			UE_LOG(LogRHI, Fatal, TEXT("Failed to link compute program [%s]. Current total programs: %d"), *Config.ProgramKey.ToString(), GNumPrograms);
			return false;
		}
		GetOpenGLProgramsCache().Add(Config.ProgramKey, ComputeShader->LinkedProgram);
	}

	return true;
}

FOpenGLLinkedProgram* FOpenGLDynamicRHI::GetLinkedComputeProgram(FRHIComputeShader* ComputeShaderRHI)
{
	VERIFY_GL_SCOPE();
	check(ComputeShaderRHI->GetHash() != FSHAHash());
	FOpenGLComputeShader* ComputeShader = ResourceCast(ComputeShaderRHI);

	FOpenGLLinkedProgramConfiguration Config;

	Config.Shaders[CrossCompiler::SHADER_STAGE_COMPUTE].Resource = ComputeShader->Resource;
	Config.Shaders[CrossCompiler::SHADER_STAGE_COMPUTE].Bindings = ComputeShader->Bindings;
	Config.ProgramKey.ShaderHashes[CrossCompiler::SHADER_STAGE_COMPUTE] = ComputeShaderRHI->GetHash();

	FOpenGLLinkedProgram* LinkedProgram = GetOpenGLProgramsCache().Find(Config.ProgramKey, true);
	if (LinkedProgram == nullptr)
	{
		// Not in the cache. Create and add the program here.
		// We can now link the compute shader, by now the shader hash has been set.
		LinkComputeShader(ComputeShaderRHI, ComputeShader);
		check(ComputeShader->LinkedProgram);
		LinkedProgram = ComputeShader->LinkedProgram;
	}
	else if (!LinkedProgram->bConfigIsInitalized)
	{
		// this has been loaded via binary program cache, properly initialize it here:
		LinkedProgram->SetConfig(Config);
		// We now have the config for this program, we must configure the program for use.
		ConfigureGLProgramStageStates(LinkedProgram);
	}
	check(LinkedProgram->bConfigIsInitalized);
	return LinkedProgram;
}

FComputeShaderRHIRef FOpenGLDynamicRHI::RHICreateComputeShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	check(RHISupportsComputeShaders(GMaxRHIShaderPlatform));
	return CreateProxyShader<FRHIComputeShader, FOpenGLComputeShaderProxy>(Code, Hash);
}

template<class TOpenGLStage>
static FString GetShaderStageSource(TOpenGLStage* Shader)
{
	FString Source;
#if DEBUG_GL_SHADERS
	Source = Shader->GlslCodeString;
#else
	GLsizei NumShaders = 0;
	glGetProgramiv(Shader->Resource, GL_ATTACHED_SHADERS, (GLint*)&NumShaders);
	if(NumShaders > 0)
	{
		GLuint* Shaders = (GLuint*)alloca(sizeof(GLuint)*NumShaders);
		glGetAttachedShaders(Shader->Resource, NumShaders, &NumShaders, Shaders);
		for(int32 i = 0; i < NumShaders; i++)
		{
			GLint Len = 0;
			glGetShaderiv(Shaders[i], GL_SHADER_SOURCE_LENGTH, &Len);
			if(Len > 0)
			{
				ANSICHAR* Code = new ANSICHAR[Len + 1];
				glGetShaderSource(Shaders[i], Len + 1, &Len, Code);
				Source += Code;
				delete [] Code;
			}
		}
	}
#endif
	return Source;
}

// ============================================================================================================================

struct FOpenGLShaderVaryingMapping
{
	FAnsiCharArray Name;
	int32 WriteLoc;
	int32 ReadLoc;
};

typedef TMap<FOpenGLLinkedProgramConfiguration,FOpenGLLinkedProgramConfiguration::ShaderInfo> FOpenGLSeparateShaderObjectCache;

static FOpenGLSeparateShaderObjectCache& GetOpenGLSeparateShaderObjectCache()
{
	static FOpenGLSeparateShaderObjectCache SeparateShaderObjectCache;
	return SeparateShaderObjectCache;
}

template<class TOpenGLStage0, class TOpenGLStage1>
static void BindShaderStage(FOpenGLLinkedProgramConfiguration& Config, CrossCompiler::EShaderStage NextStage, TOpenGLStage0* NextStageShader, const FSHAHash& NextStageHash, CrossCompiler::EShaderStage PrevStage, TOpenGLStage1* PrevStageShader)
{
	check(NextStageShader && PrevStageShader);
	FOpenGLLinkedProgramConfiguration::ShaderInfo& ShaderInfo = Config.Shaders[NextStage];
	FOpenGLLinkedProgramConfiguration::ShaderInfo& PrevInfo = Config.Shaders[PrevStage];

	GLuint NextStageResource = NextStageShader->Resource;
	FOpenGLShaderBindings NextStageBindings = NextStageShader->Bindings;
	
	if ( FOpenGL::SupportsSeparateShaderObjects() )
	{
		FOpenGLLinkedProgramConfiguration SeparableConfig;
		SeparableConfig.Shaders[0] = PrevInfo;
		SeparableConfig.ProgramKey.ShaderHashes[0] = Config.ProgramKey.ShaderHashes[PrevStage];
		SeparableConfig.Shaders[1] = ShaderInfo;
		SeparableConfig.ProgramKey.ShaderHashes[1] = Config.ProgramKey.ShaderHashes[NextStage];

		FOpenGLLinkedProgramConfiguration::ShaderInfo* PrevResource = GetOpenGLSeparateShaderObjectCache().Find(SeparableConfig);
		if(PrevResource)
		{
			PrevInfo.Bindings = PrevResource->Bindings;
			PrevInfo.Resource = PrevResource->Resource;
		}
		else
		{
			FOpenGLShaderBindings& PrevStageBindings = PrevStageShader->Bindings;
			TMap<FAnsiCharArray, int32> PrevStageVaryings;
			for (int32 i = 0; i < PrevStageBindings.OutputVaryings.Num(); i++)
			{
				FAnsiCharArray Name = PrevStageBindings.OutputVaryings[i].Varying;
				if ( Name.Num() >= 4 && (FCStringAnsi::Strncmp(Name.GetData(), "out_", 4) == 0 || FCStringAnsi::Strncmp(Name.GetData(), "var_", 4) == 0) )
				{
					Name.RemoveAt(0, 4);
				}
				PrevStageVaryings.Add(Name, PrevStageBindings.OutputVaryings[i].Location);
			}
			
			bool bInterpolatorMatches = true;
			
			TMap<FAnsiCharArray, int32> NextStageVaryings;
			TArray<FString> InputErrors;
			TArray<FOpenGLShaderVaryingMapping> VaryingMapping;
			for (int32 i = 0; i < NextStageBindings.InputVaryings.Num(); i++)
			{
				FAnsiCharArray Name = NextStageBindings.InputVaryings[i].Varying;
				if ( Name.Num() >= 3 && FCStringAnsi::Strncmp(Name.GetData(), "in_", 3) == 0 )
				{
					Name.RemoveAt(0, 3);
				}
				if ( Name.Num() >= 4 && FCStringAnsi::Strncmp(Name.GetData(), "var_", 4) == 0 )
				{
					Name.RemoveAt(0, 4);
				}
				NextStageVaryings.Add(Name, NextStageBindings.InputVaryings[i].Location);
				if( PrevStageVaryings.Contains(Name) )
				{
					int32& PrevLocation = PrevStageVaryings.FindChecked(Name);
					if(PrevLocation != NextStageBindings.InputVaryings[i].Location)
					{
						if(PrevLocation >= 0 && NextStageBindings.InputVaryings[i].Location >= 0)
						{
							FOpenGLShaderVaryingMapping Pair;
							Pair.Name = Name;
							Pair.WriteLoc = PrevLocation;
							Pair.ReadLoc = NextStageBindings.InputVaryings[i].Location;
							VaryingMapping.Add(Pair);
							UE_LOG(LogRHI,Warning,TEXT("Separate Shader Object Binding Warning: Input %s @ %d of stage 0x%x written by stage 0x%x at wrong location %d"), ANSI_TO_TCHAR(NextStageBindings.InputVaryings[i].Varying.GetData()), NextStageBindings.InputVaryings[i].Location, TOpenGLStage0::TypeEnum, TOpenGLStage1::TypeEnum, PrevLocation);
						}
						else if(NextStageBindings.InputVaryings[i].Location == -1)
						{
							InputErrors.Add(FString::Printf(TEXT("Separate Shader Object Binding Error: Input %s of stage 0x%x written by stage 0x%x at location %d, can't be rewritten."), ANSI_TO_TCHAR(NextStageBindings.InputVaryings[i].Varying.GetData()), TOpenGLStage0::TypeEnum, TOpenGLStage1::TypeEnum, PrevLocation));
						}
						else
						{
							InputErrors.Add(FString::Printf(TEXT("Separate Shader Object Binding Error: Input %s @ %d of stage 0x%x written by stage 0x%x without location, can't be rewritten."), ANSI_TO_TCHAR(NextStageBindings.InputVaryings[i].Varying.GetData()), NextStageBindings.InputVaryings[i].Location, TOpenGLStage0::TypeEnum, TOpenGLStage1::TypeEnum));
						}
						bInterpolatorMatches = false;
					}
				}
				else
				{
					InputErrors.Add(FString::Printf(TEXT("Separate Shader Object Binding Error: Input %s @ %d of stage 0x%x not written by stage 0x%x"), ANSI_TO_TCHAR(NextStageBindings.InputVaryings[i].Varying.GetData()), NextStageBindings.InputVaryings[i].Location, TOpenGLStage0::TypeEnum, TOpenGLStage1::TypeEnum));
					bInterpolatorMatches = false;
				}
			}
			
			TArray<FOpenGLShaderVarying> OutputElimination;
			for (int32 i = 0; i < PrevStageBindings.OutputVaryings.Num(); i++)
			{
				if ( PrevStageBindings.OutputVaryings[i].Location == -1 )
				{
					FAnsiCharArray Name = PrevStageBindings.OutputVaryings[i].Varying;
					if ( Name.Num() >= 4 && (FCStringAnsi::Strncmp(Name.GetData(), "out_", 4) == 0 || FCStringAnsi::Strncmp(Name.GetData(), "var_", 4) == 0) )
					{
						Name.RemoveAt(0, 4);
					}
					if( !NextStageVaryings.Contains(Name) )
					{
						OutputElimination.Add(PrevStageBindings.OutputVaryings[i]);
						UE_LOG(LogRHI,Warning,TEXT("Separate Shader Object Binding Warning: Named output %s of stage 0x%x not read by stage 0x%x"), ANSI_TO_TCHAR(PrevStageBindings.OutputVaryings[i].Varying.GetData()), TOpenGLStage1::TypeEnum, TOpenGLStage0::TypeEnum);
						bInterpolatorMatches = false;
					}
				}
			}
		
			if(!bInterpolatorMatches)
			{
				if(InputErrors.Num() == 0)
				{
					FOpenGLCodeHeader Header;
					Header.GlslMarker = 0x474c534c;
					CA_SUPPRESS(6326);
					switch ((int32)TOpenGLStage1::StaticFrequency)
					{
						case SF_Vertex:
							Header.FrequencyMarker = 0x5653;
							break;
						case SF_Pixel:
							Header.FrequencyMarker = 0x5053;
							break;
						case SF_Geometry:
							Header.FrequencyMarker = 0x4753;
							break;
						case SF_Hull:
							Header.FrequencyMarker = 0x4853;
							break;
						case SF_Domain:
							Header.FrequencyMarker = 0x4453;
							break;
						case SF_Compute:
							Header.FrequencyMarker = 0x4353;
							break;
						default:
							UE_LOG(LogRHI, Fatal, TEXT("Invalid shader frequency: %d"), (int32)TOpenGLStage1::StaticFrequency);
					}
					Header.Bindings = PrevStageShader->Bindings;
					Header.UniformBuffersCopyInfo = PrevStageShader->UniformBuffersCopyInfo;
					
					TArray<FString> PrevLines;
					FString PrevSource = GetShaderStageSource<TOpenGLStage1>(PrevStageShader);
					PrevSource.ParseIntoArrayLines(PrevLines);
					bool const bOutputElimination = OutputElimination.Num() > 0;
					for(FOpenGLShaderVarying Output : OutputElimination)
					{
						for(int32 i = 0; i < PrevLines.Num(); i++)
						{
							if(PrevLines[i].Contains(Output.Varying.GetData()))
							{
								PrevLines[i].Empty();
							}
						}
						for(int32 i = 0; i < Header.Bindings.OutputVaryings.Num(); i++)
						{
							if(Output == Header.Bindings.OutputVaryings[i])
							{
								Header.Bindings.OutputVaryings.RemoveAt(i);
								break;
							}
						}
					}
					OutputElimination.Empty();
					
					bool const bVaryingRemapping = VaryingMapping.Num() > 0;
					
					if (OutputElimination.Num() == 0 && VaryingMapping.Num() == 0 && (bOutputElimination || bVaryingRemapping))
					{
						FString NewPrevSource;
						for(FString Line : PrevLines)
						{
							if(!Line.IsEmpty())
							{
								NewPrevSource += Line + TEXT("\n");
							}
						}
						
						TArray<uint8> Bytes;
						FMemoryWriter Ar(Bytes);
						Ar << Header;
						TArray<ANSICHAR> Chars;
						int32 Len = FCStringAnsi::Strlen(TCHAR_TO_ANSI(*NewPrevSource)) + 1;
						Chars.Append(TCHAR_TO_ANSI(*NewPrevSource), Len);
						Ar.Serialize(Chars.GetData(), Chars.Num());
						
						TRefCountPtr<TOpenGLStage1> NewPrev(CompileOpenGLShader<TOpenGLStage1>(Bytes, FSHAHash()));
						PrevInfo.Bindings = Header.Bindings;
						PrevInfo.Resource = NewPrev->Resource;
					}
					
					bInterpolatorMatches = (OutputElimination.Num() == 0 && VaryingMapping.Num() == 0);
				}
				else
				{
					for(int32 i = 0; i < InputErrors.Num(); i++)
					{
						UE_LOG(LogRHI, Error, TEXT("%s"), *InputErrors[i]);
					}
				}
				
				if(!bInterpolatorMatches)
				{
					FString PrevShaderStageSource = GetShaderStageSource<TOpenGLStage1>(PrevStageShader);
					FString NextShaderStageSource = GetShaderStageSource<TOpenGLStage0>(NextStageShader);
					UE_LOG(LogRHI, Error, TEXT("Separate Shader Object Stage 0x%x:\n%s"), TOpenGLStage1::TypeEnum, *PrevShaderStageSource);
					UE_LOG(LogRHI, Error, TEXT("Separate Shader Object Stage 0x%x:\n%s"), TOpenGLStage0::TypeEnum, *NextShaderStageSource);
				}
			}
			
			GetOpenGLSeparateShaderObjectCache().Add(SeparableConfig, PrevInfo);
		}
	}
	
	ShaderInfo.Bindings = NextStageBindings;
	ShaderInfo.Resource = NextStageResource;
	Config.ProgramKey.ShaderHashes[NextStage] = NextStageHash;
}

// ============================================================================================================================
static FCriticalSection GProgramBinaryCacheCS;

FBoundShaderStateRHIRef FOpenGLDynamicRHI::RHICreateBoundShaderState_OnThisThread(
	FRHIVertexDeclaration* VertexDeclarationRHI,
	FRHIVertexShader* VertexShaderRHI,
	FRHIHullShader* HullShaderRHI,
	FRHIDomainShader* DomainShaderRHI,
	FRHIPixelShader* PixelShaderRHI,
	FRHIGeometryShader* GeometryShaderRHI,
	bool bFromPSOFileCache
	)
{
	check(IsInRenderingThread() || IsInRHIThread());

	FScopeLock Lock(&GProgramBinaryCacheCS);

	VERIFY_GL_SCOPE();

	SCOPE_CYCLE_COUNTER(STAT_OpenGLCreateBoundShaderStateTime);

	if (!PixelShaderRHI)
	{
		// use special null pixel shader when PixelShader was set to NULL
		PixelShaderRHI = TShaderMapRef<FNULLPS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)).GetPixelShader();
	}

	auto CreateConfig = [VertexShaderRHI, HullShaderRHI, DomainShaderRHI, PixelShaderRHI, GeometryShaderRHI]()
	{
		FOpenGLVertexShader* VertexShader = ResourceCast(VertexShaderRHI);
		FOpenGLPixelShader* PixelShader = ResourceCast(PixelShaderRHI);
		FOpenGLHullShader* HullShader = ResourceCast(HullShaderRHI);
		FOpenGLDomainShader* DomainShader = ResourceCast(DomainShaderRHI);
		FOpenGLGeometryShader* GeometryShader = ResourceCast(GeometryShaderRHI);

		FOpenGLLinkedProgramConfiguration Config;

		check(VertexShader);
		check(PixelShader);

		// Fill-in the configuration
		Config.Shaders[CrossCompiler::SHADER_STAGE_VERTEX].Bindings = VertexShader->Bindings;
		Config.Shaders[CrossCompiler::SHADER_STAGE_VERTEX].Resource = VertexShader->Resource;
		Config.ProgramKey.ShaderHashes[CrossCompiler::SHADER_STAGE_VERTEX] = VertexShaderRHI->GetHash();

		if (FOpenGL::SupportsTessellation())
		{
			if (HullShader)
			{
				check(VertexShader);
				BindShaderStage(Config, CrossCompiler::SHADER_STAGE_HULL, HullShader, HullShaderRHI->GetHash(), CrossCompiler::SHADER_STAGE_VERTEX, VertexShader);
			}
			if (DomainShader)
			{
				check(HullShader);
				BindShaderStage(Config, CrossCompiler::SHADER_STAGE_DOMAIN, DomainShader, DomainShaderRHI->GetHash(), CrossCompiler::SHADER_STAGE_HULL, HullShader);
			}
		}

		if (GeometryShader)
		{
			check(DomainShader || VertexShader);
			if (DomainShader)
			{
				BindShaderStage(Config, CrossCompiler::SHADER_STAGE_GEOMETRY, GeometryShader, GeometryShaderRHI->GetHash(), CrossCompiler::SHADER_STAGE_DOMAIN, DomainShader);
			}
			else
			{
				BindShaderStage(Config, CrossCompiler::SHADER_STAGE_GEOMETRY, GeometryShader, GeometryShaderRHI->GetHash(), CrossCompiler::SHADER_STAGE_VERTEX, VertexShader);
			}
		}

		check(DomainShader || GeometryShader || VertexShader);
		if (DomainShader)
		{
			BindShaderStage(Config, CrossCompiler::SHADER_STAGE_PIXEL, PixelShader, PixelShaderRHI->GetHash(), CrossCompiler::SHADER_STAGE_DOMAIN, DomainShader);
		}
		else if (GeometryShader)
		{
			BindShaderStage(Config, CrossCompiler::SHADER_STAGE_PIXEL, PixelShader, PixelShaderRHI->GetHash(), CrossCompiler::SHADER_STAGE_GEOMETRY, GeometryShader);
		}
		else
		{
			BindShaderStage(Config, CrossCompiler::SHADER_STAGE_PIXEL, PixelShader, PixelShaderRHI->GetHash(), CrossCompiler::SHADER_STAGE_VERTEX, VertexShader);
		}
		return Config;
	};

	// Check for an existing bound shader state which matches the parameters
	FCachedBoundShaderStateLink* CachedBoundShaderStateLink = GetCachedBoundShaderState(
		VertexDeclarationRHI,
		VertexShaderRHI,
		PixelShaderRHI,
		HullShaderRHI,
		DomainShaderRHI,
		GeometryShaderRHI
		);

	if(CachedBoundShaderStateLink)
	{
		// If we've already created a bound shader state with these parameters, reuse it.
		FOpenGLBoundShaderState* BoundShaderState = ResourceCast(CachedBoundShaderStateLink->BoundShaderState);
		FOpenGLLinkedProgram* LinkedProgram = BoundShaderState->LinkedProgram;
		GetOpenGLProgramsCache().Touch(LinkedProgram);

		if (!LinkedProgram->bConfigIsInitalized)
		{
			// touch has unevicted the program, set it up.
			FOpenGLLinkedProgramConfiguration Config = CreateConfig();
			LinkedProgram->SetConfig(Config);
			// We now have the config for this program, we must configure the program for use.
			ConfigureGLProgramStageStates(LinkedProgram);
		}
		return CachedBoundShaderStateLink->BoundShaderState;
	}
	else
	{
		FOpenGLLinkedProgramConfiguration Config = CreateConfig();

		// Check if we already have such a program in released programs cache. Use it, if we do.
		FOpenGLLinkedProgram* LinkedProgram = 0;

		int32 Index = StaticLastReleasedProgramsIndex;
		for( int CacheIndex = 0; CacheIndex < LAST_RELEASED_PROGRAMS_CACHE_COUNT; ++CacheIndex )
		{
			FOpenGLLinkedProgram* Prog = StaticLastReleasedPrograms[Index];
			if( Prog && Prog->Config == Config )
			{
				StaticLastReleasedPrograms[Index] = 0;
				LinkedProgram = Prog;
				GetOpenGLProgramsCache().Touch(LinkedProgram);
				break;
			}
			Index = (Index == LAST_RELEASED_PROGRAMS_CACHE_COUNT-1) ? 0 : Index+1;
		}

		if (!LinkedProgram)
		{
			bool bFindAndCreateEvictedProgram = true;
			// If this is this a request from the PSOFC then do not create an evicted program.
			if (bFromPSOFileCache && GetOpenGLProgramsCache().IsUsingLRU())
			{
				bFindAndCreateEvictedProgram = false;
			}

			FOpenGLLinkedProgram* CachedProgram = GetOpenGLProgramsCache().Find(Config.ProgramKey, bFindAndCreateEvictedProgram);
			if (!CachedProgram)
			{
				// ensure that pending request for this program has been completed before
				if (FOpenGLProgramBinaryCache::CheckSinglePendingGLProgramCreateRequest(Config.ProgramKey))
				{
					CachedProgram = GetOpenGLProgramsCache().Find(Config.ProgramKey, bFindAndCreateEvictedProgram);
				}
			}

			if (CachedProgram)
			{
				LinkedProgram = CachedProgram;
				if (!LinkedProgram->bConfigIsInitalized && bFindAndCreateEvictedProgram)
				{
					LinkedProgram->SetConfig(Config);
					// We now have the config for this program, we must configure the program for use.
					ConfigureGLProgramStageStates(LinkedProgram);
				}
			}
			else
			{
				FOpenGLVertexShader* VertexShader = ResourceCast(VertexShaderRHI);
				FOpenGLPixelShader* PixelShader = ResourceCast(PixelShaderRHI);
				FOpenGLHullShader* HullShader = ResourceCast(HullShaderRHI);
				FOpenGLDomainShader* DomainShader = ResourceCast(DomainShaderRHI);
				FOpenGLGeometryShader* GeometryShader = ResourceCast(GeometryShaderRHI);
		
				// Make sure we have OpenGL context set up, and invalidate the parameters cache and current program (as we'll link a new one soon)
				GetContextStateForCurrentContext().Program = -1;
				MarkShaderParameterCachesDirty(PendingState.ShaderParameters, false);
				PendingState.LinkedProgramAndDirtyFlag = nullptr;

				// Link program, using the data provided in config
				LinkedProgram = LinkProgram(Config, bFromPSOFileCache);

				if (LinkedProgram == NULL)
				{
#if DEBUG_GL_SHADERS
					if (VertexShader)
					{
						UE_LOG(LogRHI, Error, TEXT("Vertex Shader:\n%s"), ANSI_TO_TCHAR(VertexShader->GlslCode.GetData()));
					}
					if (PixelShader)
					{
						UE_LOG(LogRHI, Error, TEXT("Pixel Shader:\n%s"), ANSI_TO_TCHAR(PixelShader->GlslCode.GetData()));
					}
					if (GeometryShader)
					{
						UE_LOG(LogRHI, Error, TEXT("Geometry Shader:\n%s"), ANSI_TO_TCHAR(GeometryShader->GlslCode.GetData()));
					}
					if (FOpenGL::SupportsTessellation())
					{
						if (HullShader)
						{
							UE_LOG(LogRHI, Error, TEXT("Hull Shader:\n%s"), ANSI_TO_TCHAR(HullShader->GlslCode.GetData()));
						}
						if (DomainShader)
						{
							UE_LOG(LogRHI, Error, TEXT("Domain Shader:\n%s"), ANSI_TO_TCHAR(DomainShader->GlslCode.GetData()));
						}
					}
#endif //DEBUG_GL_SHADERS
					FName LinkFailurePanic = bFromPSOFileCache ? FName("FailedProgramLinkDuringPrecompile") : FName("FailedProgramLink");
					RHIGetPanicDelegate().ExecuteIfBound(LinkFailurePanic);
					UE_LOG(LogRHI, Fatal, TEXT("Failed to link program [%s]. Current total programs: %d, precompile: %d"), *Config.ProgramKey.ToString(), GNumPrograms, (uint32)bFromPSOFileCache);
				}

				GetOpenGLProgramsCache().Add(Config.ProgramKey, LinkedProgram);

				// if building the cache file and using the LRU then evict the last shader created. this will reduce the risk of fragmentation of the driver's program memory.
				if (bFindAndCreateEvictedProgram == false && FOpenGLProgramBinaryCache::IsBuildingCache())
				{
					GetOpenGLProgramsCache().EvictMostRecent();
				}
			}
		}

		check(VertexDeclarationRHI);
		
		FOpenGLVertexDeclaration* VertexDeclaration = ResourceCast(VertexDeclarationRHI);
		FOpenGLBoundShaderState* BoundShaderState = new FOpenGLBoundShaderState(
			LinkedProgram,
			VertexDeclarationRHI,
			VertexShaderRHI,
			PixelShaderRHI,
			GeometryShaderRHI,
			HullShaderRHI,
			DomainShaderRHI
			);

		return BoundShaderState;
	}
}

void DestroyShadersAndPrograms()
{
	VERIFY_GL_SCOPE();
	GetOpenGLUniformBlockLocations().Empty();
	GetOpenGLUniformBlockBindings().Empty();
	
	GetOpenGLProgramsCache().Empty();


	StaticLastReleasedProgramsIndex = 0;

	{
		FOpenGLCompiledShaderCache& ShaderCache = GetOpenGLCompiledShaderCache();
		for (FOpenGLCompiledShaderCache::TIterator It(ShaderCache); It; ++It)
		{
			FOpenGL::DeleteShader(It.Value());
		}
		ShaderCache.Empty();
	}
	{
		FOpenGLCompiledLibraryShaderCache& ShaderCache = GetOpenGLCompiledLibraryShaderCache();
		for (FOpenGLCompiledLibraryShaderCache::TIterator It(ShaderCache); It; ++It)
		{
			delete It.Value().Header;
		}
		ShaderCache.Empty();
	}
}

struct FSamplerPair
{
	GLuint Texture;
	GLuint Sampler;

	friend bool operator ==(const FSamplerPair& A,const FSamplerPair& B)
	{
		return A.Texture == B.Texture && A.Sampler == B.Sampler;
	}

	friend uint32 GetTypeHash(const FSamplerPair &Key)
	{
		return Key.Texture ^ (Key.Sampler << 18);
	}
};

static TMap<FSamplerPair, GLuint64> BindlessSamplerMap;

void FOpenGLDynamicRHI::SetupBindlessTextures( FOpenGLContextState& ContextState, const TArray<FOpenGLBindlessSamplerInfo> &Samplers )
{
	if ( OpenGLConsoleVariables::bBindlessTexture == 0 || !FOpenGL::SupportsBindlessTexture())
	{
		return;
	}
	VERIFY_GL_SCOPE();

	// Bind all textures via Bindless
	for (int32 Texture = 0; Texture < Samplers.Num(); Texture++)
	{
		const FOpenGLBindlessSamplerInfo &Sampler = Samplers[Texture];

		GLuint64 BindlessSampler = 0xffffffff;
		FSamplerPair Pair;
		Pair.Texture = PendingState.Textures[Sampler.Slot].Resource;
		Pair.Sampler = (PendingState.SamplerStates[Sampler.Slot] != NULL) ? PendingState.SamplerStates[Sampler.Slot]->Resource : 0;

		if (Pair.Texture)
		{
			// Find Sampler pair
			if ( BindlessSamplerMap.Contains(Pair))
			{
				BindlessSampler = BindlessSamplerMap[Pair];
			}
			else
			{
				// if !found, create

				if (Pair.Sampler)
				{
					BindlessSampler = FOpenGL::GetTextureSamplerHandle( Pair.Texture, Pair.Sampler);
				}
				else
				{
					BindlessSampler = FOpenGL::GetTextureHandle( Pair.Texture);
				}

				FOpenGL::MakeTextureHandleResident( BindlessSampler);

				BindlessSamplerMap.Add( Pair, BindlessSampler);
			}

			FOpenGL::UniformHandleui64( Sampler.Handle, BindlessSampler);
		}
	}
}


void FOpenGLDynamicRHI::BindPendingShaderState( FOpenGLContextState& ContextState )
{
	SCOPE_CYCLE_COUNTER_DETAILED(STAT_OpenGLShaderBindTime);
	VERIFY_GL_SCOPE();

	bool ForceUniformBindingUpdate = false;

	GLuint PendingProgram = PendingState.BoundShaderState->LinkedProgram->Program;
	if (ContextState.Program != PendingProgram)
	{
		FOpenGL::BindProgramPipeline(PendingProgram);
		ContextState.Program = PendingProgram;
		ContextState.bUsingTessellation = PendingState.BoundShaderState->LinkedProgram->bUsingTessellation;
		MarkShaderParameterCachesDirty(PendingState.ShaderParameters, false);
		PendingState.LinkedProgramAndDirtyFlag = nullptr;

		//Disable the forced rebinding to reduce driver overhead - required by SSOs
		ForceUniformBindingUpdate = FOpenGL::SupportsSeparateShaderObjects();
	}

	if (!GUseEmulatedUniformBuffers)
	{
		int32 NextUniformBufferIndex = OGL_FIRST_UNIFORM_BUFFER;

		static_assert(SF_Compute == 5 && SF_NumFrequencies == 10, "Unexpected SF_ ordering");
		static_assert(SF_RayGen > SF_Compute, "SF_Compute must be at the end of the list of frequencies supported in OpenGL");

		int32 NumUniformBuffers[SF_Compute];

		PendingState.BoundShaderState->GetNumUniformBuffers(NumUniformBuffers);
			
		PendingState.BoundShaderState->LinkedProgram->VerifyUniformBlockBindings(CrossCompiler::SHADER_STAGE_VERTEX, NextUniformBufferIndex);

		BindUniformBufferBase(
			ContextState,
			NumUniformBuffers[SF_Vertex],
			PendingState.BoundUniformBuffers[SF_Vertex],
			NextUniformBufferIndex,
			ForceUniformBindingUpdate);
		NextUniformBufferIndex += NumUniformBuffers[SF_Vertex];

		PendingState.BoundShaderState->LinkedProgram->VerifyUniformBlockBindings(CrossCompiler::SHADER_STAGE_PIXEL, NextUniformBufferIndex);
		BindUniformBufferBase(
			ContextState,
			NumUniformBuffers[SF_Pixel],
			PendingState.BoundUniformBuffers[SF_Pixel],
			NextUniformBufferIndex,
			ForceUniformBindingUpdate);
		NextUniformBufferIndex += NumUniformBuffers[SF_Pixel];

		if (NumUniformBuffers[SF_Geometry] >= 0)
		{
			PendingState.BoundShaderState->LinkedProgram->VerifyUniformBlockBindings(CrossCompiler::SHADER_STAGE_GEOMETRY, NextUniformBufferIndex);
			BindUniformBufferBase(
				ContextState,
				NumUniformBuffers[SF_Geometry],
				PendingState.BoundUniformBuffers[SF_Geometry],
				NextUniformBufferIndex,
				ForceUniformBindingUpdate);
			NextUniformBufferIndex += NumUniformBuffers[SF_Geometry];
		}

		if (NumUniformBuffers[SF_Hull] >= 0)
		{
			PendingState.BoundShaderState->LinkedProgram->VerifyUniformBlockBindings(CrossCompiler::SHADER_STAGE_HULL, NextUniformBufferIndex);
			BindUniformBufferBase(ContextState,
				NumUniformBuffers[SF_Hull],
				PendingState.BoundUniformBuffers[SF_Hull],
				NextUniformBufferIndex,
				ForceUniformBindingUpdate);
			NextUniformBufferIndex += NumUniformBuffers[SF_Hull];
		}

		if (NumUniformBuffers[SF_Domain] >= 0)
		{
			PendingState.BoundShaderState->LinkedProgram->VerifyUniformBlockBindings(CrossCompiler::SHADER_STAGE_DOMAIN, NextUniformBufferIndex);
			BindUniformBufferBase(ContextState,
				NumUniformBuffers[SF_Domain],
				PendingState.BoundUniformBuffers[SF_Domain],
				NextUniformBufferIndex,
				ForceUniformBindingUpdate);
			NextUniformBufferIndex += NumUniformBuffers[SF_Domain];
		}
		if (FOpenGL::SupportsBindlessTexture())
		{
			SetupBindlessTextures(ContextState, PendingState.BoundShaderState->LinkedProgram->Samplers);
		}
	}
}

FOpenGLBoundShaderState::FOpenGLBoundShaderState(
	FOpenGLLinkedProgram* InLinkedProgram,
	FRHIVertexDeclaration* InVertexDeclarationRHI,
	FRHIVertexShader* InVertexShaderRHI,
	FRHIPixelShader* InPixelShaderRHI,
	FRHIGeometryShader* InGeometryShaderRHI,
	FRHIHullShader* InHullShaderRHI,
	FRHIDomainShader* InDomainShaderRHI
	)
	:	CacheLink(InVertexDeclarationRHI, InVertexShaderRHI, InPixelShaderRHI,
		InHullShaderRHI, InDomainShaderRHI,	InGeometryShaderRHI, this)
{
	FOpenGLVertexDeclaration* InVertexDeclaration = FOpenGLDynamicRHI::ResourceCast(InVertexDeclarationRHI);
	VertexDeclaration = InVertexDeclaration;
	VertexShaderProxy = static_cast<FOpenGLVertexShaderProxy*>(InVertexShaderRHI);
	PixelShaderProxy = static_cast<FOpenGLPixelShaderProxy*>(InPixelShaderRHI);
	GeometryShaderProxy = static_cast<FOpenGLGeometryShaderProxy*>(InGeometryShaderRHI);
	HullShaderProxy = static_cast<FOpenGLHullShaderProxy*>(InHullShaderRHI);
	DomainShaderProxy = static_cast<FOpenGLDomainShaderProxy*>(InDomainShaderRHI);

	LinkedProgram = InLinkedProgram;

	if (InVertexDeclaration)
	{
		FMemory::Memcpy(StreamStrides, InVertexDeclaration->StreamStrides, sizeof(StreamStrides));
	}
	else
	{
		FMemory::Memzero(StreamStrides, sizeof(StreamStrides));
	}
}

TAutoConsoleVariable<int32> CVarEvictOnBssDestruct(
	TEXT("r.OpenGL.EvictOnBSSDestruct"),
	0,
	TEXT(""),
	ECVF_ReadOnly | ECVF_RenderThreadSafe
);


FOpenGLBoundShaderState::~FOpenGLBoundShaderState()
{
	check(LinkedProgram);
	RunOnGLRenderContextThread([LinkedProgram = LinkedProgram]()
	{
		const bool bIsEvicted = GetOpenGLProgramsCache().IsUsingLRU() && GetOpenGLProgramsCache().IsEvicted(LinkedProgram->Config.ProgramKey);
		if( !bIsEvicted )
		{
			FOpenGLLinkedProgram* Prog = StaticLastReleasedPrograms[StaticLastReleasedProgramsIndex];
			StaticLastReleasedPrograms[StaticLastReleasedProgramsIndex++] = LinkedProgram;
			if (StaticLastReleasedProgramsIndex == LAST_RELEASED_PROGRAMS_CACHE_COUNT)
			{
				StaticLastReleasedProgramsIndex = 0;
			}

			if (CVarEvictOnBssDestruct.GetValueOnAnyThread() && GetOpenGLProgramsCache().IsUsingLRU())
			{
				FDelayedEvictionContainer::Get().Add(LinkedProgram);
			}

			OnProgramDeletion(LinkedProgram->Program);
		}
	});
}

bool FOpenGLBoundShaderState::NeedsTextureStage(int32 TextureStageIndex)
{
	return LinkedProgram->TextureStageNeeds[TextureStageIndex];
}

int32 FOpenGLBoundShaderState::MaxTextureStageUsed()
{
	return LinkedProgram->MaxTextureStage;
}

const TBitArray<>& FOpenGLBoundShaderState::GetTextureNeeds(int32& OutMaxTextureStageUsed)
{
	OutMaxTextureStageUsed = LinkedProgram->MaxTextureStage;
	return LinkedProgram->TextureStageNeeds;
}

const TBitArray<>& FOpenGLBoundShaderState::GetUAVNeeds(int32& OutMaxUAVUnitUsed) const
{
	OutMaxUAVUnitUsed = LinkedProgram->MaxUAVUnitUsed;
	return LinkedProgram->UAVStageNeeds;
}

void FOpenGLBoundShaderState::GetNumUniformBuffers(int32 NumUniformBuffers[SF_Compute])
{
	if (IsRunningRHIInSeparateThread())
	{
		// fast path, no need to check any fences....
		check(IsInRHIThread());
		check(IsValidRef(VertexShaderProxy) && IsValidRef(PixelShaderProxy));


		NumUniformBuffers[SF_Vertex] = VertexShaderProxy->GetGLResourceObject_OnRHIThread()->Bindings.NumUniformBuffers;
		NumUniformBuffers[SF_Pixel] = PixelShaderProxy->GetGLResourceObject_OnRHIThread()->Bindings.NumUniformBuffers;
		NumUniformBuffers[SF_Geometry] = GeometryShaderProxy ? GeometryShaderProxy->GetGLResourceObject_OnRHIThread()->Bindings.NumUniformBuffers : -1;
		NumUniformBuffers[SF_Hull] = HullShaderProxy ? HullShaderProxy->GetGLResourceObject_OnRHIThread()->Bindings.NumUniformBuffers : -1;
		NumUniformBuffers[SF_Domain] = DomainShaderProxy ? DomainShaderProxy->GetGLResourceObject_OnRHIThread()->Bindings.NumUniformBuffers : -1;
	}
	else
	{
		NumUniformBuffers[SF_Vertex] = VertexShaderProxy->GetGLResourceObject()->Bindings.NumUniformBuffers;
		NumUniformBuffers[SF_Pixel] = PixelShaderProxy->GetGLResourceObject()->Bindings.NumUniformBuffers;
		NumUniformBuffers[SF_Geometry] = GeometryShaderProxy ? GeometryShaderProxy->GetGLResourceObject()->Bindings.NumUniformBuffers : -1;
		NumUniformBuffers[SF_Hull] = HullShaderProxy ? HullShaderProxy->GetGLResourceObject()->Bindings.NumUniformBuffers : -1;
		NumUniformBuffers[SF_Domain] = DomainShaderProxy ? DomainShaderProxy->GetGLResourceObject()->Bindings.NumUniformBuffers : -1;
	}
}


bool FOpenGLBoundShaderState::RequiresDriverInstantiation()
{
	check(LinkedProgram);
	bool const bDrawn = LinkedProgram->bDrawn;
	LinkedProgram->bDrawn = true;
	return !bDrawn;
}

bool FOpenGLComputeShader::NeedsTextureStage(int32 TextureStageIndex)
{
	return LinkedProgram->TextureStageNeeds[TextureStageIndex];
}

int32 FOpenGLComputeShader::MaxTextureStageUsed()
{
	return LinkedProgram->MaxTextureStage;
}

const TBitArray<>& FOpenGLComputeShader::GetTextureNeeds(int32& OutMaxTextureStageUsed)
{
	OutMaxTextureStageUsed = LinkedProgram->MaxTextureStage;
	return LinkedProgram->TextureStageNeeds;
}

const TBitArray<>& FOpenGLComputeShader::GetUAVNeeds(int32& OutMaxUAVUnitUsed) const
{
	OutMaxUAVUnitUsed = LinkedProgram->MaxUAVUnitUsed;
	return LinkedProgram->UAVStageNeeds;
}

bool FOpenGLComputeShader::NeedsUAVStage(int32 UAVStageIndex) const
{
	return LinkedProgram->UAVStageNeeds[UAVStageIndex];
}

void FOpenGLDynamicRHI::BindPendingComputeShaderState(FOpenGLContextState& ContextState, FOpenGLComputeShader* ComputeShader)
{
	VERIFY_GL_SCOPE();
	bool ForceUniformBindingUpdate = false;

	GetOpenGLProgramsCache().Touch(ComputeShader->LinkedProgram);

	GLuint PendingProgram = ComputeShader->LinkedProgram->Program;
	if (ContextState.Program != PendingProgram)
	{
		FOpenGL::BindProgramPipeline(PendingProgram);
		ContextState.Program = PendingProgram;
		MarkShaderParameterCachesDirty(PendingState.ShaderParameters, true);
		PendingState.LinkedProgramAndDirtyFlag = nullptr;
		ForceUniformBindingUpdate = true;
	}

	if (!GUseEmulatedUniformBuffers)
	{
		ComputeShader->LinkedProgram->VerifyUniformBlockBindings(CrossCompiler::SHADER_STAGE_COMPUTE, OGL_FIRST_UNIFORM_BUFFER);
		BindUniformBufferBase(
			ContextState,
			ComputeShader->Bindings.NumUniformBuffers,
			PendingState.BoundUniformBuffers[SF_Compute],
			OGL_FIRST_UNIFORM_BUFFER,
			ForceUniformBindingUpdate);
		SetupBindlessTextures( ContextState, ComputeShader->LinkedProgram->Samplers );
	}
}

/** Constructor. */
FOpenGLShaderParameterCache::FOpenGLShaderParameterCache()
	: GlobalUniformArraySize(-1)
{
	for (int32 ArrayIndex = 0; ArrayIndex < CrossCompiler::PACKED_TYPEINDEX_MAX; ++ArrayIndex)
	{
		PackedGlobalUniformDirty[ArrayIndex].StartVector = 0;
		PackedGlobalUniformDirty[ArrayIndex].NumVectors = 0;
	}
}

void FOpenGLShaderParameterCache::InitializeResources(int32 UniformArraySize)
{
	check(GlobalUniformArraySize == -1);

	// Uniform arrays have to be multiples of float4s.
	UniformArraySize = Align(UniformArraySize,SizeOfFloat4);

	PackedGlobalUniforms[0] = (uint8*)FMemory::Malloc(UniformArraySize * CrossCompiler::PACKED_TYPEINDEX_MAX);
	PackedUniformsScratch[0] = (uint8*)FMemory::Malloc(UniformArraySize * CrossCompiler::PACKED_TYPEINDEX_MAX);

	FMemory::Memzero(PackedGlobalUniforms[0], UniformArraySize * CrossCompiler::PACKED_TYPEINDEX_MAX);
	FMemory::Memzero(PackedUniformsScratch[0], UniformArraySize * CrossCompiler::PACKED_TYPEINDEX_MAX);
	for (int32 ArrayIndex = 1; ArrayIndex < CrossCompiler::PACKED_TYPEINDEX_MAX; ++ArrayIndex)
	{
		PackedGlobalUniforms[ArrayIndex] = PackedGlobalUniforms[ArrayIndex - 1] + UniformArraySize;
		PackedUniformsScratch[ArrayIndex] = PackedUniformsScratch[ArrayIndex - 1] + UniformArraySize;
	}
	GlobalUniformArraySize = UniformArraySize;

	for (int32 ArrayIndex = 0; ArrayIndex < CrossCompiler::PACKED_TYPEINDEX_MAX; ++ArrayIndex)
	{
		PackedGlobalUniformDirty[ArrayIndex].StartVector = 0;
		PackedGlobalUniformDirty[ArrayIndex].NumVectors = UniformArraySize / SizeOfFloat4;
	}
}

/** Destructor. */
FOpenGLShaderParameterCache::~FOpenGLShaderParameterCache()
{
	if (GlobalUniformArraySize > 0)
	{
		FMemory::Free(PackedUniformsScratch[0]);
		FMemory::Free(PackedGlobalUniforms[0]);
	}

	FMemory::Memzero(PackedUniformsScratch);
	FMemory::Memzero(PackedGlobalUniforms);

	GlobalUniformArraySize = -1;
}

/**
 * Marks all uniform arrays as dirty.
 */
void FOpenGLShaderParameterCache::MarkAllDirty()
{
	for (int32 ArrayIndex = 0; ArrayIndex < CrossCompiler::PACKED_TYPEINDEX_MAX; ++ArrayIndex)
	{
		PackedGlobalUniformDirty[ArrayIndex].StartVector = 0;
		PackedGlobalUniformDirty[ArrayIndex].NumVectors = GlobalUniformArraySize / SizeOfFloat4;
	}
}

/**
 * Set parameter values.
 */
void FOpenGLShaderParameterCache::Set(uint32 BufferIndexName, uint32 ByteOffset, uint32 NumBytes, const void* NewValues)
{
	uint32 BufferIndex = CrossCompiler::PackedTypeNameToTypeIndex(BufferIndexName);
	check(GlobalUniformArraySize != -1);
	check(BufferIndex < CrossCompiler::PACKED_TYPEINDEX_MAX);
	check(ByteOffset + NumBytes <= (uint32)GlobalUniformArraySize);
	PackedGlobalUniformDirty[BufferIndex].MarkDirtyRange(ByteOffset / SizeOfFloat4, (NumBytes + SizeOfFloat4 - 1) / SizeOfFloat4);
	FMemory::Memcpy(PackedGlobalUniforms[BufferIndex] + ByteOffset, NewValues, NumBytes);
}

/**
 * Commit shader parameters to the currently bound program.
 * @param ParameterTable - Information on the bound uniform arrays for the program.
 */


void FOpenGLShaderParameterCache::CommitPackedGlobals(const FOpenGLLinkedProgram* LinkedProgram, int32 Stage)
{
	SCOPE_CYCLE_COUNTER(STAT_OpenGLUniformCommitTime);
	VERIFY_GL_SCOPE();
	const uint32 BytesPerRegister = 16;

	/**
	 * Note that this always uploads the entire uniform array when it is dirty.
	 * The arrays are marked dirty either when the bound shader state changes or
	 * a value in the array is modified. OpenGL actually caches uniforms per-
	 * program. If we shadowed those per-program uniforms we could avoid calling
	 * glUniform4?v for values that have not changed since the last invocation
	 * of the program.
	 *
	 * It's unclear whether the driver does the same thing and whether there is
	 * a performance benefit. Even if there is, this type of caching makes any
	 * multithreading vastly more difficult, so for now uniforms are not cached
	 * per-program.
	 */
	const TArray<FOpenGLLinkedProgram::FPackedUniformInfo>& PackedUniforms = LinkedProgram->StagePackedUniformInfo[Stage].PackedUniformInfos;
	const TArray<CrossCompiler::FPackedArrayInfo>& PackedArrays = LinkedProgram->Config.Shaders[Stage].Bindings.PackedGlobalArrays;
	for (int32 PackedUniform = 0; PackedUniform < PackedUniforms.Num(); ++PackedUniform)
	{
		const FOpenGLLinkedProgram::FPackedUniformInfo& UniformInfo = PackedUniforms[PackedUniform];
		GLint Location = UniformInfo.Location;
		const uint32 ArrayIndex = UniformInfo.Index;
		if (Location >= 0 && // Probably this uniform array was optimized away in a linked program
			PackedGlobalUniformDirty[ArrayIndex].NumVectors > 0)
		{
			check(ArrayIndex < CrossCompiler::PACKED_TYPEINDEX_MAX);
			const uint32 NumVectors = PackedArrays[PackedUniform].Size / BytesPerRegister;
			const void* UniformData = PackedGlobalUniforms[ArrayIndex];

			const uint32 StartVector = PackedGlobalUniformDirty[ArrayIndex].StartVector;
			int32 NumDirtyVectors = FMath::Min(PackedGlobalUniformDirty[ArrayIndex].NumVectors, NumVectors - StartVector);
			check(NumDirtyVectors);
			UniformData = (uint8*)UniformData + StartVector * sizeof(float) * 4;
			Location += StartVector;
			switch (UniformInfo.Index)
			{
			case CrossCompiler::PACKED_TYPEINDEX_HIGHP:
			case CrossCompiler::PACKED_TYPEINDEX_MEDIUMP:
			case CrossCompiler::PACKED_TYPEINDEX_LOWP:
				FOpenGL::ProgramUniform4fv(LinkedProgram->Config.Shaders[Stage].Resource, Location, NumDirtyVectors, (GLfloat*)UniformData);
				break;

			case CrossCompiler::PACKED_TYPEINDEX_INT:
				FOpenGL::ProgramUniform4iv(LinkedProgram->Config.Shaders[Stage].Resource, Location, NumDirtyVectors, (GLint*)UniformData);
				break;

			case CrossCompiler::PACKED_TYPEINDEX_UINT:
				FOpenGL::ProgramUniform4uiv(LinkedProgram->Config.Shaders[Stage].Resource, Location, NumDirtyVectors, (GLuint*)UniformData);
				break;
			}

			PackedGlobalUniformDirty[ArrayIndex].StartVector = 0;
			PackedGlobalUniformDirty[ArrayIndex].NumVectors = 0;
		}
	}
}

void FOpenGLShaderParameterCache::CommitPackedUniformBuffers(FOpenGLLinkedProgram* LinkedProgram, int32 Stage, FUniformBufferRHIRef* RHIUniformBuffers, const TArray<CrossCompiler::FUniformBufferCopyInfo>& UniformBuffersCopyInfo)
{
	SCOPE_CYCLE_COUNTER(STAT_OpenGLConstantBufferUpdateTime);
	VERIFY_GL_SCOPE();

	// Uniform Buffers are split into precision/type; the list of RHI UBs is traversed and if a new one was set, its
	// contents are copied per precision/type into corresponding scratch buffers which are then uploaded to the program
	const FOpenGLShaderBindings& Bindings = LinkedProgram->Config.Shaders[Stage].Bindings;
	check(Bindings.NumUniformBuffers <= FOpenGLRHIState::MAX_UNIFORM_BUFFERS_PER_SHADER_STAGE);

	if (Bindings.bFlattenUB)
	{
		int32 LastInfoIndex = 0;
		for (int32 BufferIndex = 0; BufferIndex < Bindings.NumUniformBuffers; ++BufferIndex)
		{
			const FOpenGLUniformBuffer* UniformBuffer = (FOpenGLUniformBuffer*)RHIUniformBuffers[BufferIndex].GetReference();
			check(UniformBuffer);
			const uint32* RESTRICT SourceData = UniformBuffer->EmulatedBufferData->Data.GetData();
			for (int32 InfoIndex = LastInfoIndex; InfoIndex < UniformBuffersCopyInfo.Num(); ++InfoIndex)
			{
				const CrossCompiler::FUniformBufferCopyInfo& Info = UniformBuffersCopyInfo[InfoIndex];
				if (Info.SourceUBIndex == BufferIndex)
				{
					check((Info.DestOffsetInFloats + Info.SizeInFloats) * sizeof(float) <= (uint32)GlobalUniformArraySize);
					float* RESTRICT ScratchMem = (float*)PackedGlobalUniforms[Info.DestUBTypeIndex];
					ScratchMem += Info.DestOffsetInFloats;
					FMemory::Memcpy(ScratchMem, SourceData + Info.SourceOffsetInFloats, Info.SizeInFloats * sizeof(float));
					PackedGlobalUniformDirty[Info.DestUBTypeIndex].MarkDirtyRange(Info.DestOffsetInFloats / NumFloatsInFloat4, (Info.SizeInFloats + NumFloatsInFloat4 - 1) / NumFloatsInFloat4);
				}
				else
				{
					LastInfoIndex = InfoIndex;
					break;
				}
			}
		}
	}
	else
	{
		const auto& PackedUniformBufferInfos = LinkedProgram->StagePackedUniformInfo[Stage].PackedUniformBufferInfos;
		int32 LastCopyInfoIndex = 0;
		auto& EmulatedUniformBufferSet = LinkedProgram->StagePackedUniformInfo[Stage].LastEmulatedUniformBufferSet;
		for (int32 BufferIndex = 0; BufferIndex < Bindings.NumUniformBuffers; ++BufferIndex)
		{
			const FOpenGLUniformBuffer* UniformBuffer = (FOpenGLUniformBuffer*)RHIUniformBuffers[BufferIndex].GetReference();
			// Workaround for null UBs (FORT-323429), additional logging here is to give us a chance to investigate the higher level issue causing the null UB.
#if !UE_BUILD_SHIPPING
			UE_CLOG(UniformBuffer == nullptr && EmulatedUniformBufferSet.IsValidIndex(BufferIndex), LogRHI, Fatal, TEXT("CommitPackedUniformBuffers null UB stage %d, idx %d (%d), %s"), Stage, BufferIndex, EmulatedUniformBufferSet.Num(), *LinkedProgram->Config.ProgramKey.ToString());
#endif
			if (UniformBuffer && EmulatedUniformBufferSet.IsValidIndex(BufferIndex) && EmulatedUniformBufferSet[BufferIndex] != UniformBuffer->UniqueID)
			{
				EmulatedUniformBufferSet[BufferIndex] = UniformBuffer->UniqueID;

				// Go through the list of copy commands and perform the appropriate copy into the scratch buffer
				for (int32 InfoIndex = LastCopyInfoIndex; InfoIndex < UniformBuffersCopyInfo.Num(); ++InfoIndex)
				{
					const CrossCompiler::FUniformBufferCopyInfo& Info = UniformBuffersCopyInfo[InfoIndex];
					if (Info.SourceUBIndex == BufferIndex)
					{
						const uint32* RESTRICT SourceData = UniformBuffer->EmulatedBufferData->Data.GetData();
						SourceData += Info.SourceOffsetInFloats;
						float* RESTRICT ScratchMem = (float*)PackedUniformsScratch[Info.DestUBTypeIndex];
						ScratchMem += Info.DestOffsetInFloats;
						FMemory::Memcpy(ScratchMem, SourceData, Info.SizeInFloats * sizeof(float));
					}
					else if (Info.SourceUBIndex > BufferIndex)
					{
						// Done finding current copies
						LastCopyInfoIndex = InfoIndex;
						break;
					}

					// keep going since we could have skipped this loop when skipping cached UBs...
				}

				// Upload the split buffers to the program
				const auto& UniformBufferUploadInfoList = PackedUniformBufferInfos[BufferIndex];
				for (int32 InfoIndex = 0; InfoIndex < UniformBufferUploadInfoList.Num(); ++InfoIndex)
				{
					auto& UBInfo = Bindings.PackedUniformBuffers[BufferIndex];
					const auto& UniformInfo = UniformBufferUploadInfoList[InfoIndex];
					if (UniformInfo.Location < 0)
					{
						// Optimized out
						continue;
					}
					
					const void* RESTRICT UniformData = PackedUniformsScratch[UniformInfo.Index];
					int32 NumVectors = UBInfo[InfoIndex].Size / SizeOfFloat4;
					check(UniformInfo.ArrayType == UBInfo[InfoIndex].TypeName);
					switch (UniformInfo.Index)
					{
					case CrossCompiler::PACKED_TYPEINDEX_HIGHP:
					case CrossCompiler::PACKED_TYPEINDEX_MEDIUMP:
					case CrossCompiler::PACKED_TYPEINDEX_LOWP:
						FOpenGL::ProgramUniform4fv(LinkedProgram->Config.Shaders[Stage].Resource, UniformInfo.Location, NumVectors, (GLfloat*)UniformData);
						break;

					case CrossCompiler::PACKED_TYPEINDEX_INT:
						FOpenGL::ProgramUniform4iv(LinkedProgram->Config.Shaders[Stage].Resource, UniformInfo.Location, NumVectors, (GLint*)UniformData);
						break;

					case CrossCompiler::PACKED_TYPEINDEX_UINT:
						FOpenGL::ProgramUniform4uiv(LinkedProgram->Config.Shaders[Stage].Resource, UniformInfo.Location, NumVectors, (GLuint*)UniformData);
						break;
					}
				}
			}
		}
	}
}


static const uint32 GBinaryProgramFileVersion = 4;

TAutoConsoleVariable<int32> FOpenGLProgramBinaryCache::CVarPBCEnable(
	TEXT("r.ProgramBinaryCache.Enable"),
#if PLATFORM_ANDROID
	1,	// Enabled by default on Android.
#else
	0,
#endif
	TEXT("If true, enables binary program cache. Enabled by default only on Android"),
	ECVF_ReadOnly | ECVF_RenderThreadSafe
	);

TAutoConsoleVariable<int32> FOpenGLProgramBinaryCache::CVarRestartAndroidAfterPrecompile(
	TEXT("r.ProgramBinaryCache.RestartAndroidAfterPrecompile"),
	1,	// Enabled by default on Android.
	TEXT("If true, Android apps will restart after precompiling the binary program cache. Enabled by default only on Android"),
	ECVF_ReadOnly | ECVF_RenderThreadSafe
	);

FOpenGLProgramBinaryCache* FOpenGLProgramBinaryCache::CachePtr = nullptr;

FOpenGLProgramBinaryCache::FOpenGLProgramBinaryCache(const FString& InCachePath)
	: CachePath(InCachePath)
	, BinaryCacheAsyncReadFileHandle(nullptr)
	, BinaryCacheWriteFileHandle(nullptr)
	, BinaryFileState(EBinaryFileState::Uninitialized)
{
	ANSICHAR* GLVersion = (ANSICHAR*)glGetString(GL_VERSION);
	ANSICHAR* GLRenderer = (ANSICHAR*)glGetString(GL_RENDERER);
	FString HashString;
	HashString.Append(GLVersion);
	HashString.Append(GLRenderer);
	FSHAHash VersionHash;
	FSHA1::HashBuffer(TCHAR_TO_ANSI(*HashString), HashString.Len(), VersionHash.Hash);

	CacheFilename = LegacyShaderPlatformToShaderFormat(GMaxRHIShaderPlatform).ToString() + TEXT("_") + VersionHash.ToString();
}

FOpenGLProgramBinaryCache::~FOpenGLProgramBinaryCache()
{
	if(BinaryCacheAsyncReadFileHandle)
	{
		delete BinaryCacheAsyncReadFileHandle;
	}
	if (BinaryCacheWriteFileHandle)
	{
		delete BinaryCacheWriteFileHandle;
	}

	if (OnShaderPipelineCacheOpenedDelegate.IsValid())
	{
		FShaderPipelineCache::GetCacheOpenedDelegate().Remove(OnShaderPipelineCacheOpenedDelegate);
	}

	if (OnShaderPipelineCachePrecompilationCompleteDelegate.IsValid())
	{
		FShaderPipelineCache::GetPrecompilationCompleteDelegate().Remove(OnShaderPipelineCachePrecompilationCompleteDelegate);
	}
};

bool FOpenGLProgramBinaryCache::IsEnabled()
{
	return CachePtr != nullptr;
}

bool FOpenGLProgramBinaryCache::IsBuildingCache()
{
	if(CachePtr != nullptr)
	{
		return CachePtr->IsBuildingCache_internal();
	}
	return false;
}

void FOpenGLProgramBinaryCache::Initialize()
{
	check(CachePtr == nullptr);

	if (CVarPBCEnable.GetValueOnAnyThread() == 0)
	{
		UE_LOG(LogRHI, Log, TEXT("FOpenGLProgramBinaryCache disabled by r.ProgramBinaryCache.Enable=0"));
		return;
	}

	if (FOpenGL::SupportsSeparateShaderObjects())
	{
		UE_LOG(LogRHI, Warning, TEXT("FOpenGLProgramBinaryCache disabled as RHI supports separate shader objects"));
		return;
	}

	if (!FOpenGL::SupportsProgramBinary())
	{
		UE_LOG(LogRHI, Warning, TEXT("FOpenGLProgramBinaryCache disabled as devices does not support program binaries"));
		return;
	}

#if PLATFORM_ANDROID && !PLATFORM_LUMIN && !PLATFORM_LUMINGL4
	if (FOpenGL::HasBinaryProgramRetrievalFailed())
	{
		if (FOpenGL::SupportsProgramBinary())
		{
			UE_LOG(LogRHI, Warning, TEXT("FOpenGLProgramBinaryCache: Device has failed to emit program binary despite SupportsProgramBinary == true. Disabling binary cache."));
			return;
		}
	}
#endif


		FString CacheFolderPath;
#if PLATFORM_ANDROID && USE_ANDROID_FILE
		// @todo Lumin: Use that GetPathForExternalWrite or something?
		extern FString GExternalFilePath;
		CacheFolderPath = GExternalFilePath / TEXT("ProgramBinaryCache");
			
#else
		CacheFolderPath = FPaths::ProjectSavedDir() / TEXT("ProgramBinaryCache");
#endif

	// Remove entire ProgramBinaryCache folder if -ClearOpenGLBinaryProgramCache is specified on command line
	if (FParse::Param(FCommandLine::Get(), TEXT("ClearOpenGLBinaryProgramCache")))
	{
		UE_LOG(LogRHI, Log, TEXT("ClearOpenGLBinaryProgramCache specified, deleting binary program cache folder: %s"), *CacheFolderPath);
		FPlatformFileManager::Get().GetPlatformFile().DeleteDirectoryRecursively(*CacheFolderPath);
	}

	CachePtr = new FOpenGLProgramBinaryCache(CacheFolderPath);
	UE_LOG(LogRHI, Log, TEXT("Enabling program binary cache as %s"), *CachePtr->GetProgramBinaryCacheFilePath());

	// Add delegates for the ShaderPipelineCache precompile.
	UE_LOG(LogRHI, Log, TEXT("FOpenGLProgramBinaryCache will be initialized when ShaderPipelineCache opens its file"));
	CachePtr->OnShaderPipelineCacheOpenedDelegate				  = FShaderPipelineCache::GetCacheOpenedDelegate().AddRaw(CachePtr, &FOpenGLProgramBinaryCache::OnShaderPipelineCacheOpened);
	CachePtr->OnShaderPipelineCachePrecompilationCompleteDelegate = FShaderPipelineCache::GetPrecompilationCompleteDelegate().AddRaw(CachePtr, &FOpenGLProgramBinaryCache::OnShaderPipelineCachePrecompilationComplete);
}

void FOpenGLProgramBinaryCache::OnShaderPipelineCacheOpened(FString const& Name , EShaderPlatform Platform, uint32 Count, const FGuid& VersionGuid, FShaderPipelineCache::FShaderCachePrecompileContext& ShaderCachePrecompileContext)
{
	UE_LOG(LogRHI, Log, TEXT("Scanning Binary program cache, using Shader Pipeline Cache version %s"), *VersionGuid.ToString());
	ScanProgramCacheFile(VersionGuid);
	if(IsBuildingCache_internal())
	{
		ShaderCachePrecompileContext.SetPrecompilationIsSlowTask();
	}
}

void FOpenGLProgramBinaryCache::OnShaderPipelineCachePrecompilationComplete(uint32 Count, double Seconds, const FShaderPipelineCache::FShaderCachePrecompileContext& ShaderCachePrecompileContext)
{
	UE_LOG(LogRHI, Log, TEXT("OnShaderPipelineCachePrecompilationComplete: %d shaders"), Count);
	
	// Want to ignore any subsequent Shader Pipeline Cache opening/closing, eg when loading modules
	FShaderPipelineCache::GetCacheOpenedDelegate().Remove(OnShaderPipelineCacheOpenedDelegate);
	FShaderPipelineCache::GetPrecompilationCompleteDelegate().Remove(OnShaderPipelineCachePrecompilationCompleteDelegate);
	OnShaderPipelineCacheOpenedDelegate.Reset();
	OnShaderPipelineCachePrecompilationCompleteDelegate.Reset();

	check(IsBuildingCache_internal() || BinaryFileState == EBinaryFileState::ValidCacheFile);

	if (IsBuildingCache_internal())
	{
		CloseWriteHandle();

#if PLATFORM_ANDROID
		FAndroidMisc::bNeedsRestartAfterPSOPrecompile = true;
		if (CVarRestartAndroidAfterPrecompile.GetValueOnAnyThread() == 1)
		{
#if USE_ANDROID_JNI
			extern void AndroidThunkCpp_RestartApplication(const FString& IntentString);
			AndroidThunkCpp_RestartApplication(TEXT(""));
#endif
		}
#endif
		OpenAsyncReadHandle();
		BinaryFileState = EBinaryFileState::ValidCacheFile;
	}
}

// contains runtime and file information for a single program entry in the cache file.
struct FGLProgramBinaryFileCacheEntry
{
	struct FGLProgramBinaryFileCacheFileInfo
	{
		// contains location info for a program in the binary cache file
		FOpenGLProgramKey ShaderHasheSet;
		uint32 ProgramOffset;
		uint32 ProgramSize;

		FGLProgramBinaryFileCacheFileInfo() : ProgramOffset(0), ProgramSize(0) { }

		friend bool operator == (const FGLProgramBinaryFileCacheFileInfo& A, const FGLProgramBinaryFileCacheFileInfo& B)
		{
			return A.ShaderHasheSet == B.ShaderHasheSet && A.ProgramOffset == B.ProgramOffset&& A.ProgramSize == B.ProgramSize;
		}
	} FileInfo;

	// program read request.
	TWeakPtr<IAsyncReadRequest, ESPMode::ThreadSafe> ReadRequest;
	// read data
	TArray<uint8> ProgramBinaryData;
	// debugging use only, index of program as encountered during scan, -1 if new.
	int32 ProgramIndex;

	enum class EGLProgramState : uint8
	{
		Unset,
		ProgramStored, // program exists in binary cache but has not yet been loaded
		ProgramLoading,	// program has started async loading.
		ProgramLoaded,	// program has loaded and is ready for GL object creation.
		ProgramAvailable, // program has loaded from binary cache and is available for use with GL.
		ProgramComplete // program has been either added by rhi or handed over to rhi after being made available to it.
	};
	EGLProgramState GLProgramState;

	// if != 0 then prepared runtime GL program name:
	GLuint GLProgramId;

	FGLProgramBinaryFileCacheEntry() : ProgramIndex(-1), GLProgramState(EGLProgramState::Unset), GLProgramId(0) { }

	friend uint32 GetTypeHash(const FGLProgramBinaryFileCacheEntry& Key)
	{
		return FCrc::MemCrc32(&Key, sizeof(Key));
	}
};

static FCriticalSection GPendingGLProgramCreateRequestsCS;

// Scan the binary cache file and build a record of all programs.
void FOpenGLProgramBinaryCache::ScanProgramCacheFile(const FGuid& ShaderPipelineCacheVersionGuid)
{
	UE_LOG(LogRHI, Log, TEXT("OnShaderScanProgramCacheFile"));
	FScopeLock Lock(&GProgramBinaryCacheCS);
	FString ProgramCacheFilename = GetProgramBinaryCacheFilePath();
	FString ProgramCacheFilenameTemp = GetProgramBinaryCacheFilePath() + TEXT(".scan");

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	check(BinaryFileState == EBinaryFileState::Uninitialized);

	bool bBinaryFileIsValid = false;
	bool bBinaryFileIsValidAndGuidMatch = false;

	// Try to move the file to a temporary filename before the scan, so we won't try to read it again if it's corrupted
	PlatformFile.DeleteFile(*ProgramCacheFilenameTemp);
	PlatformFile.MoveFile(*ProgramCacheFilenameTemp, *ProgramCacheFilename);

	TUniquePtr<FArchive> FileReader(IFileManager::Get().CreateFileReader(*ProgramCacheFilenameTemp));
	if (FileReader)
	{
		UE_LOG(LogRHI, Log, TEXT("OnShaderScanProgramCacheFile : Opened %s"), *ProgramCacheFilenameTemp);
		FArchive& Ar = *FileReader;
		uint32 Version = 0;
		Ar << Version;
		if(Version == GBinaryProgramFileVersion)
		{
			FGuid BinaryCacheGuid;
			Ar << BinaryCacheGuid;
			bool bCacheUsesCompressedBinaries;
			Ar << bCacheUsesCompressedBinaries;

			const bool bUseCompressedProgramBinaries = CVarStoreCompressedBinaries.GetValueOnAnyThread() != 0;
			bBinaryFileIsValid = (bUseCompressedProgramBinaries == bCacheUsesCompressedBinaries);
			bBinaryFileIsValidAndGuidMatch = bBinaryFileIsValid && (!ShaderPipelineCacheVersionGuid.IsValid() || ShaderPipelineCacheVersionGuid == BinaryCacheGuid);
			
			if (CVarUseExistingBinaryFileCache.GetValueOnAnyThread() == 0 && bBinaryFileIsValidAndGuidMatch == false)
			{
				// If we dont want to use the existing binary cache and the guids have changed then rebuild the binary file.
				bBinaryFileIsValid = false;
			}
		}
		
		if (bBinaryFileIsValid)
		{
			const uint32 ProgramBinaryStart = Ar.Tell();

			// Search the file for the end record.
			bool bFoundEndRecord = false;
			int32 ProgramIndex = 0;
			while (!Ar.AtEnd())
			{
				check(bFoundEndRecord == false); // There should be no additional data after the eof record.

				FOpenGLProgramKey ProgramKey;
				uint32 ProgramBinarySize = 0;
				Ar << ProgramKey;
				Ar << ProgramBinarySize;
				uint32 ProgramBinaryOffset = Ar.Tell();
				if (ProgramBinarySize == 0)
				{
					if (ProgramKey == FOpenGLProgramKey())
					{
						bFoundEndRecord = true;
					}
					else
					{
						// Note: This should not happen with new code. We can no longer write out records with 0 program size. see AppendProgramBinaryFile.
						UE_LOG(LogRHI, Warning, TEXT("FOpenGLProgramBinaryCache::ScanProgramCacheFile : encountered 0 sized program during binary program cache scan"));
					}
				}
				Ar.Seek(ProgramBinaryOffset + ProgramBinarySize);
			}

			if(bFoundEndRecord)
			{
				Ar.Seek(ProgramBinaryStart);
				while (!Ar.AtEnd())
				{
					FOpenGLProgramKey ProgramKey;
					uint32 ProgramBinarySize = 0;
					Ar << ProgramKey;
					Ar << ProgramBinarySize;

					if (ProgramBinarySize > 0)
					{
						FGLProgramBinaryFileCacheEntry* NewEntry = new FGLProgramBinaryFileCacheEntry();
						NewEntry->FileInfo.ShaderHasheSet = ProgramKey;
						NewEntry->ProgramIndex = ProgramIndex++;

						uint32 ProgramBinaryOffset = Ar.Tell();
						NewEntry->FileInfo.ProgramSize = ProgramBinarySize;
						NewEntry->FileInfo.ProgramOffset = ProgramBinaryOffset;

						if (bBinaryFileIsValidAndGuidMatch)
						{
							ProgramEntryContainer.Emplace(TUniquePtr<FGLProgramBinaryFileCacheEntry>(NewEntry));

							// check to see if any of the shaders are already loaded and so we should serialize the binary
							bool bAllShadersLoaded = true;
							for (int32 i = 0; i < CrossCompiler::NUM_SHADER_STAGES && bAllShadersLoaded; i++)
							{
								bAllShadersLoaded = ProgramKey.ShaderHashes[i] == FSHAHash() || ShaderIsLoaded(ProgramKey.ShaderHashes[i]);
							}
							if (bAllShadersLoaded)
							{
								FPlatformMisc::LowLevelOutputDebugStringf(TEXT("*** All shaders for %s already loaded\n"), *ProgramKey.ToString());
								NewEntry->ProgramBinaryData.AddUninitialized(ProgramBinarySize);
								Ar.Serialize(NewEntry->ProgramBinaryData.GetData(), ProgramBinarySize);
								NewEntry->GLProgramState = FGLProgramBinaryFileCacheEntry::EGLProgramState::ProgramLoaded;
								CompleteLoadedGLProgramRequest_internal(NewEntry);
							}
							else
							{
								NewEntry->GLProgramState = FGLProgramBinaryFileCacheEntry::EGLProgramState::ProgramStored;
							}
							AddProgramFileEntryToMap(NewEntry);
						}
						else
						{
							check(!PreviousBinaryCacheInfo.ProgramToOldBinaryCacheMap.Contains(ProgramKey));
							PreviousBinaryCacheInfo.ProgramToOldBinaryCacheMap.Emplace(ProgramKey, TUniquePtr<FGLProgramBinaryFileCacheEntry>(NewEntry));
						}
						Ar.Seek(ProgramBinaryOffset + ProgramBinarySize);
					}
				}

				if (bBinaryFileIsValidAndGuidMatch)
				{
					UE_LOG(LogRHI, Log, TEXT("Program Binary cache: Found %d cached programs, end record found: %d"), ProgramIndex, (uint32)bFoundEndRecord);
					FileReader->Close();
					// Rename the file back after a successful scan.
					PlatformFile.MoveFile(*ProgramCacheFilename, *ProgramCacheFilenameTemp);
				}
				else
				{
					UE_LOG(LogRHI, Log, TEXT("Program Binary cache: ShaderPipelineCache changed, regenerating for new pipeline cache. Existing cache contains %d programs, using it to populate."), PreviousBinaryCacheInfo.ProgramToOldBinaryCacheMap.Num());
					// Not closing the scan source file, we're using it to move shaders from the old cache.
					PreviousBinaryCacheInfo.OldCacheArchive = MoveTemp(FileReader);
					PreviousBinaryCacheInfo.OldCacheFilename = ProgramCacheFilenameTemp;
				}
			}
			else
			{
				// failed to find sentinel record, the file was not finalized.
				UE_LOG(LogRHI, Warning, TEXT("ScanProgramCacheFile - incomplete binary cache file encountered. Rebuilding binary program cache."));
				FileReader->Close();
				bBinaryFileIsValid = false;
				bBinaryFileIsValidAndGuidMatch = false;
			}
		}
		
		if(!bBinaryFileIsValid)
		{
			UE_LOG(LogRHI, Log, TEXT("OnShaderScanProgramCacheFile : binary file version invalid"));
		}

		if (bBinaryFileIsValidAndGuidMatch)
		{
			OpenAsyncReadHandle();
			BinaryFileState = EBinaryFileState::ValidCacheFile;
		}
	}
	else
	{
		UE_LOG(LogRHI, Log, TEXT("OnShaderScanProgramCacheFile : Failed to open %s"), *ProgramCacheFilename);
	}

	if (!bBinaryFileIsValid)
	{
		// Attempt to remove any existing binary cache or temp files (eg for different driver version)
		UE_LOG(LogRHI, Log, TEXT("Deleting binary program cache folder: %s"), *CachePath);
		PlatformFile.DeleteDirectoryRecursively(*CachePath);

		// Create
		if (!PlatformFile.CreateDirectoryTree(*CachePath))
		{
			UE_LOG(LogRHI, Warning, TEXT("Failed to create directory for a program binary cache. Cache will be disabled: %s"), *CachePath);
			return;
		}
	}

	if(!bBinaryFileIsValid || !bBinaryFileIsValidAndGuidMatch)
	{
		if (OpenWriteHandle())
		{
			BinaryFileState = bBinaryFileIsValid && !bBinaryFileIsValidAndGuidMatch ? EBinaryFileState::BuildingCacheFileWithMove : EBinaryFileState::BuildingCacheFile;

			// save header
			FArchive& Ar = *BinaryCacheWriteFileHandle;
			uint32 Version = GBinaryProgramFileVersion;
			Ar << Version;
			FGuid BinaryCacheGuid = ShaderPipelineCacheVersionGuid;
			Ar << BinaryCacheGuid;
			bool bWritingCompressedBinaries = (CVarStoreCompressedBinaries.GetValueOnAnyThread() != 0);
			Ar << bWritingCompressedBinaries;
		}
		else
		{
			// Binary cache file cannot be used, failed to open output file.
			BinaryFileState = EBinaryFileState::Uninitialized;
			RHIGetPanicDelegate().ExecuteIfBound(FName("FailedBinaryProgramArchiveOpen"));
			UE_LOG(LogRHI, Fatal, TEXT("ScanProgramCacheFile - Failed to open binary cache."));
		}
	}
}

// add the GLProgramBinaryFileCacheEntry into the runtime lookup containers.
void FOpenGLProgramBinaryCache::AddProgramFileEntryToMap(FGLProgramBinaryFileCacheEntry* NewEntry)
{
	const FOpenGLProgramKey& ProgramKey = NewEntry->FileInfo.ShaderHasheSet;
	check(!ProgramToBinaryMap.Contains(ProgramKey));
	ProgramToBinaryMap.Add(ProgramKey, NewEntry);

	UE_LOG(LogRHI, Verbose, TEXT("AddProgramFileEntryToMap : Adding program: %s"), *ProgramKey.ToString());

	for (int i = 0; i < CrossCompiler::NUM_NON_COMPUTE_SHADER_STAGES; ++i)
	{
		const FSHAHash& ShaderHash = ProgramKey.ShaderHashes[i];

		if (ShaderHash != FSHAHash())
		{
			if (ShaderToProgramsMap.Contains(ShaderHash))
			{
				ShaderToProgramsMap[ShaderHash].Add(NewEntry);
			}
			else
			{
				ShaderToProgramsMap.Add(ShaderHash, NewEntry);
			}
		}
	}
}

bool FOpenGLProgramBinaryCache::OpenWriteHandle()
{
	check(BinaryCacheWriteFileHandle == nullptr);
	check(BinaryCacheAsyncReadFileHandle == nullptr);

	// perform file writing to a file temporary filename so we don't attempt to use the file later if the write session is interrupted
	FString ProgramCacheFilename = GetProgramBinaryCacheFilePath();
	FString ProgramCacheFilenameWrite = ProgramCacheFilename + TEXT(".write");

	BinaryCacheWriteFileHandle = IFileManager::Get().CreateFileWriter(*ProgramCacheFilenameWrite, EFileWrite::FILEWRITE_None);

	UE_CLOG(BinaryCacheWriteFileHandle == nullptr, LogRHI, Warning, TEXT("Failed to open OGL binary cache output file."));

	return BinaryCacheWriteFileHandle != nullptr;
}

void FOpenGLProgramBinaryCache::CloseWriteHandle()
{
	if(BinaryFileState == EBinaryFileState::BuildingCacheFileWithMove)
	{
		UE_LOG(LogRHI, Log, TEXT("FOpenGLProgramBinaryCache: Deleting previous binary program cache (%s), reused %d programs from a total of %d."), *PreviousBinaryCacheInfo.OldCacheFilename, PreviousBinaryCacheInfo.NumberOfOldEntriesReused, ProgramToBinaryMap.Num());

		// clean up references to old cache.
		PreviousBinaryCacheInfo.OldCacheArchive->Close();
		PreviousBinaryCacheInfo.OldCacheArchive = nullptr;
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.DeleteFile(*PreviousBinaryCacheInfo.OldCacheFilename);
		PreviousBinaryCacheInfo.OldCacheFilename.Empty();
		PreviousBinaryCacheInfo.ProgramToOldBinaryCacheMap.Empty();
	}

	check(BinaryCacheWriteFileHandle != nullptr);

	AppendProgramBinaryFileEofEntry(*BinaryCacheWriteFileHandle);
	bool bArchiveFailed = BinaryCacheWriteFileHandle->IsError() || BinaryCacheWriteFileHandle->IsCriticalError();

	BinaryCacheWriteFileHandle->Close();
	delete BinaryCacheWriteFileHandle;
	BinaryCacheWriteFileHandle = nullptr;

	if (bArchiveFailed)
	{
		RHIGetPanicDelegate().ExecuteIfBound(FName("FailedBinaryProgramArchiveWrite"));
		UE_LOG(LogRHI, Fatal, TEXT("CloseWriteHandle - FArchive error bit set, failed to write binary cache."));
	}

	// rename the temp filename back to the final filename
	FString ProgramCacheFilename = GetProgramBinaryCacheFilePath();
	FString ProgramCacheFilenameWrite = ProgramCacheFilename + TEXT(".write");
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.DeleteFile(*ProgramCacheFilename); // file should never exist, but for safety
	PlatformFile.MoveFile(*ProgramCacheFilename, *ProgramCacheFilenameWrite);
}

void FOpenGLProgramBinaryCache::OpenAsyncReadHandle()
{
	check(BinaryCacheAsyncReadFileHandle == nullptr);

	FString ProgramCacheFilename = GetProgramBinaryCacheFilePath();
	BinaryCacheAsyncReadFileHandle = FPlatformFileManager::Get().GetPlatformFile().OpenAsyncRead(*ProgramCacheFilename);
	checkf(BinaryCacheAsyncReadFileHandle, TEXT("Could not opan an async file")); // this generally cannot fail because it is async
}

/* dead code, needs removal
void FOpenGLProgramBinaryCache::CloseAsyncReadHandle()
{
	// wait for any pending reads.
	{
		FScopeLock Lock(&GPendingGLProgramCreateRequestsCS);
		for (FGLProgramBinaryFileCacheEntry* CreateRequest : PendingGLProgramCreateRequests)
		{
			TSharedPtr<IAsyncReadRequest, ESPMode::ThreadSafe> ReadRequest = CreateRequest->ReadRequest.Pin();
			if (ReadRequest.IsValid())
			{
				ReadRequest->WaitCompletion();
				CreateRequest->ReadRequest = nullptr;
			}
		}
	}

	delete BinaryCacheAsyncReadFileHandle;
	BinaryCacheAsyncReadFileHandle = nullptr;
}*/

// Called when a new program has been created by OGL RHI, creates the binary cache if it's invalid and then appends the new program details to the file and runtime containers.
void FOpenGLProgramBinaryCache::AppendGLProgramToBinaryCache(const FOpenGLProgramKey& ProgramKey, GLuint Program, TArray<uint8>& CachedProgramBinaryOUT)
{
	if (IsBuildingCache_internal() == false)
	{
		return;
	}

	FScopeLock Lock(&GProgramBinaryCacheCS);

	AddUniqueGLProgramToBinaryCache(BinaryCacheWriteFileHandle, ProgramKey, Program, CachedProgramBinaryOUT);
}

// Add the program to the binary cache if it does not already exist.
void FOpenGLProgramBinaryCache::AddUniqueGLProgramToBinaryCache(FArchive* FileWriter, const FOpenGLProgramKey& ProgramKey, GLuint Program, TArray<uint8>& CachedProgramBinaryOUT)
{
	// Add to runtime and disk.
	const FOpenGLProgramKey& ProgramHash = ProgramKey;

	// Check we dont already have this: Something could be in the cache but still reach this point if OnSharedShaderCodeRequest(s) have not occurred.
	if (!ProgramToBinaryMap.Contains(ProgramHash))
	{
		uint32 ProgramBinaryOffset = 0, ProgramBinarySize = 0;

		FOpenGLProgramKey SerializedProgramKey = ProgramKey;
		if (ensure(GetProgramBinaryFromGLProgram(Program, CachedProgramBinaryOUT)))
		{
			AddProgramBinaryDataToBinaryCache(*FileWriter, CachedProgramBinaryOUT, ProgramKey);
		}
		else
		{
			// we've encountered a problem with this program and there's nothing to write.
			// This likely means the device will never be able to use this program.
			// Panic!
			RHIGetPanicDelegate().ExecuteIfBound(FName("FailedBinaryProgramWrite"));
			UE_LOG(LogRHI, Fatal, TEXT("AppendProgramBinaryFile Binary program returned 0 bytes!"));
			// Panic!
		}
	}
}

// Serialize out the program binary data and add to runtime structures.
void FOpenGLProgramBinaryCache::AddProgramBinaryDataToBinaryCache(FArchive& Ar, TArray<uint8>& BinaryProgramData, const FOpenGLProgramKey& ProgramKey)
{
	// Serialize to output file:
	FOpenGLProgramKey SerializedProgramKey = ProgramKey;
	uint32 ProgramBinarySize = (uint32)BinaryProgramData.Num();
	Ar << SerializedProgramKey;
	uint32 ProgramBinaryOffset = Ar.Tell();
	Ar << ProgramBinarySize;
	Ar.Serialize(BinaryProgramData.GetData(), ProgramBinarySize);
	if(CVarStoreCompressedBinaries.GetValueOnAnyThread())
	{
		static uint32 TotalUncompressed = 0;
		static uint32 TotalCompressed = 0;

		FCompressedProgramBinaryHeader* Header = (FCompressedProgramBinaryHeader*)BinaryProgramData.GetData();
		TotalUncompressed += Header->UncompressedSize;
		TotalCompressed += BinaryProgramData.Num();

		UE_LOG(LogRHI, Verbose, TEXT("AppendProgramBinaryFile: total Uncompressed: %d, total Compressed %d, Total saved so far: %d"), TotalUncompressed, TotalCompressed, TotalUncompressed - TotalCompressed);
	}

	FGLProgramBinaryFileCacheEntry* NewIndexEntry = new FGLProgramBinaryFileCacheEntry();
	ProgramEntryContainer.Emplace(TUniquePtr<FGLProgramBinaryFileCacheEntry>(NewIndexEntry));

	// Store the program file descriptor in the runtime program/shader container:
	NewIndexEntry->GLProgramState = FGLProgramBinaryFileCacheEntry::EGLProgramState::ProgramStored;
	NewIndexEntry->FileInfo.ProgramOffset = ProgramBinaryOffset;
	NewIndexEntry->FileInfo.ProgramSize = ProgramBinarySize;
	NewIndexEntry->ProgramIndex = ProgramToBinaryMap.Num();
	NewIndexEntry->FileInfo.ShaderHasheSet = ProgramKey;
	AddProgramFileEntryToMap(NewIndexEntry);
}

void FOpenGLProgramBinaryCache::AppendProgramBinaryFileEofEntry(FArchive& Ar)
{
	// write out an all zero record that signifies eof.
	FOpenGLProgramKey SerializedProgramKey;
	Ar << SerializedProgramKey;
	uint32 ProgramBinarySize = 0;
	Ar << ProgramBinarySize;
}

void FOpenGLProgramBinaryCache::Shutdown()
{
	if (CachePtr)
	{
		delete CachePtr;
		CachePtr = nullptr;
	}
}

bool FOpenGLProgramBinaryCache::DeferShaderCompilation(GLuint Shader, const TArray<ANSICHAR>& GlslCode)
{
	if (CachePtr)
	{
		FPendingShaderCode PendingShaderCode;
		CompressShader(GlslCode, PendingShaderCode);
		CachePtr->ShadersPendingCompilation.Add(Shader, MoveTemp(PendingShaderCode));
		return true;
	}
	return false;
}

void FOpenGLProgramBinaryCache::CacheProgram(GLuint Program, const FOpenGLProgramKey& ProgramKey, TArray<uint8>& CachedProgramBinaryOUT)
{
	if (CachePtr)
	{
		CachePtr->AppendGLProgramToBinaryCache(ProgramKey, Program, CachedProgramBinaryOUT);
	}
}

bool FOpenGLProgramBinaryCache::UseCachedProgram(GLuint& ProgramOUT, const FOpenGLProgramKey& ProgramKey, TArray<uint8>& CachedProgramBinaryOUT)
{
	if (CachePtr)
	{
		return CachePtr->UseCachedProgram_internal(ProgramOUT, ProgramKey, CachedProgramBinaryOUT);
	}
	return false;
}

bool FOpenGLProgramBinaryCache::UseCachedProgram_internal(GLuint& ProgramOUT, const FOpenGLProgramKey& ProgramKey, TArray<uint8>& CachedProgramBinaryOUT)
{
	SCOPE_CYCLE_COUNTER(STAT_OpenGLUseCachedProgramTime);
	
	FGLProgramBinaryFileCacheEntry** ProgramBinRefPtr = nullptr;

	FScopeLock Lock(&GProgramBinaryCacheCS);

	ProgramBinRefPtr = ProgramToBinaryMap.Find(ProgramKey);

	if (ProgramBinRefPtr)
	{
		FGLProgramBinaryFileCacheEntry* FoundProgram = *ProgramBinRefPtr;
		check(FoundProgram->FileInfo.ShaderHasheSet == ProgramKey);

		TSharedPtr<IAsyncReadRequest, ESPMode::ThreadSafe> LocalReadRequest = FoundProgram->ReadRequest.Pin();
		bool bHasReadRequest = LocalReadRequest.IsValid();
		check(!bHasReadRequest);

		// by this point the program must be either available or no attempt to load from shader library has occurred.
		checkf(FoundProgram->GLProgramState == FGLProgramBinaryFileCacheEntry::EGLProgramState::ProgramStored
			|| FoundProgram->GLProgramState == FGLProgramBinaryFileCacheEntry::EGLProgramState::ProgramAvailable,
			TEXT("Unexpected program state:  (%s) == %d"), *ProgramKey.ToString(), (int32)FoundProgram->GLProgramState);

		if (FoundProgram->GLProgramState == FGLProgramBinaryFileCacheEntry::EGLProgramState::ProgramAvailable)
		{
			UE_LOG(LogRHI, Log, TEXT("UseCachedProgram : Program (%s) GLid = %x is ready!"), *ProgramKey.ToString(), FoundProgram->GLProgramId);
			ProgramOUT = FoundProgram->GLProgramId;

			// GLProgram has been handed over.
			FoundProgram->GLProgramId = 0;
			FoundProgram->GLProgramState = FGLProgramBinaryFileCacheEntry::EGLProgramState::ProgramComplete;
			return true;
		}
		else
		{
			UE_LOG(LogRHI, Log, TEXT("UseCachedProgram : %s was not ready when needed!! (state %d)"), *ProgramKey.ToString(), (uint32)FoundProgram->GLProgramState);
		}
	}
	else if (BinaryFileState == EBinaryFileState::BuildingCacheFileWithMove)
	{
		// We're building the new cache using the original cache to warm:
		TUniquePtr<FGLProgramBinaryFileCacheEntry>* FoundExistingBinary = PreviousBinaryCacheInfo.ProgramToOldBinaryCacheMap.Find(ProgramKey);
		if (FoundExistingBinary)
		{
			TUniquePtr<FGLProgramBinaryFileCacheEntry>& ExistingBinary = *FoundExistingBinary;
			// read old binary:
			CachedProgramBinaryOUT.SetNumUninitialized(ExistingBinary->FileInfo.ProgramSize);
			PreviousBinaryCacheInfo.OldCacheArchive->Seek(ExistingBinary->FileInfo.ProgramOffset);
			PreviousBinaryCacheInfo.OldCacheArchive->Serialize(CachedProgramBinaryOUT.GetData(), ExistingBinary->FileInfo.ProgramSize);
			bool bSuccess = CreateGLProgramFromBinary(ProgramOUT, CachedProgramBinaryOUT);
			if (!bSuccess)
			{
				UE_LOG(LogRHI, Log, TEXT("[%s, %d, %d]"), *ProgramKey.ToString(), ProgramOUT, CachedProgramBinaryOUT.Num());
				RHIGetPanicDelegate().ExecuteIfBound(FName("FailedBinaryProgramCreateFromOldCache"));
				UE_LOG(LogRHI, Fatal, TEXT("UseCachedProgram : Failed to create GL program from binary data while BuildingCacheFileWithMove! [%s]"), *ProgramKey.ToString());
			}
			SetNewProgramStats(ProgramOUT);
			// Now write to new cache, we're returning true here so no attempt will be made to add it back to the cache later.
			AddProgramBinaryDataToBinaryCache(*BinaryCacheWriteFileHandle, CachedProgramBinaryOUT, ProgramKey);

			PreviousBinaryCacheInfo.NumberOfOldEntriesReused++;
			return true;
		}
	}
	return false;
}

void FOpenGLProgramBinaryCache::CompilePendingShaders(const FOpenGLLinkedProgramConfiguration& Config)
{
	if (CachePtr)
	{
		for (int32 StageIdx = 0; StageIdx < UE_ARRAY_COUNT(Config.Shaders); ++StageIdx)
		{
			GLuint ShaderResource = Config.Shaders[StageIdx].Resource;
			FPendingShaderCode* PendingShaderCodePtr = CachePtr->ShadersPendingCompilation.Find(ShaderResource);
			if (PendingShaderCodePtr)
			{
				TArray<ANSICHAR> GlslCode;
				UncompressShader(*PendingShaderCodePtr, GlslCode);
				CompileCurrentShader(ShaderResource, GlslCode);
				CachePtr->ShadersPendingCompilation.Remove(ShaderResource);
			}
		}
	}
}

FString FOpenGLProgramBinaryCache::GetProgramBinaryCacheFilePath() const
{
	FString ProgramFilename = CachePath + TEXT("/") + CacheFilename;
	return ProgramFilename;
}

void FOpenGLProgramBinaryCache::CompressShader(const TArray<ANSICHAR>& InGlslCode, FPendingShaderCode& OutCompressedShader)
{
	check(InGlslCode.GetTypeSize() == sizeof(uint8));
	check(OutCompressedShader.GlslCode.GetTypeSize() == sizeof(uint8));
	
	int32 UncompressedSize = InGlslCode.Num();
	int32 CompressedSize = UncompressedSize * 4.f / 3.f;
	OutCompressedShader.GlslCode.Empty(CompressedSize);
	OutCompressedShader.GlslCode.SetNum(CompressedSize);

	OutCompressedShader.bCompressed = FCompression::CompressMemory(
		NAME_Zlib,
		(void*)OutCompressedShader.GlslCode.GetData(),
		CompressedSize,
		(void*)InGlslCode.GetData(),
		UncompressedSize,
		COMPRESS_BiasMemory);

	if (OutCompressedShader.bCompressed)
	{
		// shrink buffer
		OutCompressedShader.GlslCode.SetNum(CompressedSize, true);
	}
	else
	{
		OutCompressedShader.GlslCode = InGlslCode;
	}
	
	OutCompressedShader.UncompressedSize = UncompressedSize;
	
}

void FOpenGLProgramBinaryCache::UncompressShader(const FPendingShaderCode& InCompressedShader, TArray<ANSICHAR>& OutGlslCode)
{
	check(OutGlslCode.GetTypeSize() == sizeof(uint8));
	check(InCompressedShader.GlslCode.GetTypeSize() == sizeof(uint8));

	if (InCompressedShader.bCompressed)
	{
		int32 UncompressedSize = InCompressedShader.UncompressedSize;
		OutGlslCode.Empty(UncompressedSize);
		OutGlslCode.SetNum(UncompressedSize);

		bool bResult = FCompression::UncompressMemory(
			NAME_Zlib,
			(void*)OutGlslCode.GetData(),
			UncompressedSize,
			(void*)InCompressedShader.GlslCode.GetData(),
			InCompressedShader.GlslCode.Num());

		check(bResult);
	}
	else
	{
		OutGlslCode = InCompressedShader.GlslCode;
	}
}

void FOpenGLProgramBinaryCache::CheckPendingGLProgramCreateRequests()
{
	FDelayedEvictionContainer::Get().Tick();
	if (CachePtr)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_OpenGLShaderCreateShaderLibRequests);
		CachePtr->CheckPendingGLProgramCreateRequests_internal();
	}
}

void FOpenGLProgramBinaryCache::CheckPendingGLProgramCreateRequests_internal()
{
	check(IsInRenderingThread() || IsInRHIThread());
	FScopeLock Lock(&GPendingGLProgramCreateRequestsCS);
	//UE_LOG(LogRHI, Log, TEXT("CheckPendingGLProgramCreateRequests : PendingGLProgramCreateRequests = %d"), PendingGLProgramCreateRequests.Num());

	float TimeRemainingS = (float)GMaxShaderLibProcessingTimeMS / 1000.0f;
	double StartTime = FPlatformTime::Seconds();
	int32 Count = 0;
	while(PendingGLProgramCreateRequests.Num() && TimeRemainingS > 0.0f)
	{
		CompleteLoadedGLProgramRequest_internal(PendingGLProgramCreateRequests.Pop());
		TimeRemainingS -= (float)(FPlatformTime::Seconds() - StartTime);
		Count++;
	}
	UE_CLOG(PendingGLProgramCreateRequests.Num()>0, LogRHI, Log, TEXT("CheckPendingGLProgramCreateRequests : iter count = %d, time taken = %d ms (remaining %d)"), Count, GMaxShaderLibProcessingTimeMS - (int32)(TimeRemainingS*1000.0f), PendingGLProgramCreateRequests.Num());
}

void FOpenGLProgramBinaryCache::CompleteLoadedGLProgramRequest_internal(FGLProgramBinaryFileCacheEntry* PendingGLCreate)
{
	VERIFY_GL_SCOPE();

	check(PendingGLCreate->GLProgramState == FGLProgramBinaryFileCacheEntry::EGLProgramState::ProgramLoaded);

	PendingGLCreate->ReadRequest = nullptr;

	FOpenGLProgramKey& ProgramKey = PendingGLCreate->FileInfo.ShaderHasheSet;
	const bool bProgramExists = GetOpenGLProgramsCache().Find(ProgramKey, false) != nullptr;

	if (GetOpenGLProgramsCache().IsUsingLRU())
	{
		if (!bProgramExists)
		{
			// Always add programs as evicted, 1st use will create them as programs.
			// This will reduce pressure on driver by ensuring only used programs
			// are created.
			// In this case do not create the GL program.
			GetOpenGLProgramsCache().AddAsEvicted(ProgramKey, MoveTemp(PendingGLCreate->ProgramBinaryData));
		}
		else
		{
			// The program is already in use, discard the binary data.
			PendingGLCreate->ProgramBinaryData.Empty();
		}

		// Ownership transfered to OpenGLProgramsCache.
		PendingGLCreate->GLProgramState = FGLProgramBinaryFileCacheEntry::EGLProgramState::ProgramComplete;
	}
	else
	{
		if(!bProgramExists)
		{
			bool bSuccess = CreateGLProgramFromBinary(PendingGLCreate->GLProgramId, PendingGLCreate->ProgramBinaryData);
			if (!bSuccess)
			{
				UE_LOG(LogRHI, Log, TEXT("[%s, %d, %d]"), *ProgramKey.ToString(), PendingGLCreate->GLProgramId, PendingGLCreate->ProgramBinaryData.Num());
				RHIGetPanicDelegate().ExecuteIfBound(FName("FailedBinaryProgramCreateLoadRequest"));
				UE_LOG(LogRHI, Fatal, TEXT("CompleteLoadedGLProgramRequest_internal : Failed to create GL program from binary data! [%s]"), *ProgramKey.ToString());
			}
			FOpenGLLinkedProgram* NewLinkedProgram = new FOpenGLLinkedProgram(ProgramKey, PendingGLCreate->GLProgramId);
			GetOpenGLProgramsCache().Add(ProgramKey, NewLinkedProgram);
			SetNewProgramStats(PendingGLCreate->GLProgramId);
		}

		// Finished with binary data.
		PendingGLCreate->GLProgramState = FGLProgramBinaryFileCacheEntry::EGLProgramState::ProgramAvailable;
		PendingGLCreate->ProgramBinaryData.Empty();
	}
}

bool FOpenGLProgramBinaryCache::CheckSinglePendingGLProgramCreateRequest(const FOpenGLProgramKey& ProgramKey)
{
	if (CachePtr)
	{
		return CachePtr->CheckSinglePendingGLProgramCreateRequest_internal(ProgramKey);
	}
	return false;
}

// Any pending program must complete in this case.
bool FOpenGLProgramBinaryCache::CheckSinglePendingGLProgramCreateRequest_internal(const FOpenGLProgramKey& ProgramKey)
{
	FGLProgramBinaryFileCacheEntry** ProgramBinRefPtr = nullptr;
	FScopeLock ProgramBinaryCacheLock(&GProgramBinaryCacheCS);
	ProgramBinRefPtr = CachePtr->ProgramToBinaryMap.Find(ProgramKey);
	if( ProgramBinRefPtr )
	{
		FGLProgramBinaryFileCacheEntry* ProgramEntry = *ProgramBinRefPtr;
		TSharedPtr<IAsyncReadRequest, ESPMode::ThreadSafe> LocalReadRequest = ProgramEntry->ReadRequest.Pin();
		if (LocalReadRequest.IsValid())
		{
			ensure(ProgramEntry->GLProgramState == FGLProgramBinaryFileCacheEntry::EGLProgramState::ProgramLoading);
			LocalReadRequest->WaitCompletion();
			ProgramEntry->ReadRequest = nullptr;
			ProgramEntry->GLProgramState = FGLProgramBinaryFileCacheEntry::EGLProgramState::ProgramLoaded;
			CompleteLoadedGLProgramRequest_internal(ProgramEntry);
		}
		else
		{
			FScopeLock Lock(&GPendingGLProgramCreateRequestsCS);
			if (ProgramEntry->GLProgramState == FGLProgramBinaryFileCacheEntry::EGLProgramState::ProgramLoaded)
			{
				int32 PendingRequestIndex = -1;
				if (ensure(PendingGLProgramCreateRequests.Find(ProgramEntry, PendingRequestIndex)))
				{
					CompleteLoadedGLProgramRequest_internal(ProgramEntry);
					PendingGLProgramCreateRequests.RemoveAtSwap(PendingRequestIndex);
				}
			}
		}
		return true;
	}
	return false;
}

bool OnExternalReadCallback(const TSharedPtr<IAsyncReadRequest, ESPMode::ThreadSafe>& AsyncReadRequest, FGLProgramBinaryFileCacheEntry* ProgramBinEntry, TArray<FGLProgramBinaryFileCacheEntry *>& PendingGLProgramCreateRequests, double RemainingTime)
{
	if (!AsyncReadRequest->WaitCompletion(RemainingTime))
	{
		return false;
	}

	FScopeLock ProgramBinaryCacheLock(&GProgramBinaryCacheCS);

	if (ProgramBinEntry->GLProgramState == FGLProgramBinaryFileCacheEntry::EGLProgramState::ProgramLoading)
	{
		// Async load complete.
		ProgramBinEntry->GLProgramState = FGLProgramBinaryFileCacheEntry::EGLProgramState::ProgramLoaded;
		FOpenGLProgramKey& ProgramKey = ProgramBinEntry->FileInfo.ShaderHasheSet;

		{
			// Add this program to the create queue.
			FScopeLock Lock(&GPendingGLProgramCreateRequestsCS);
			PendingGLProgramCreateRequests.Add(ProgramBinEntry);
		}
	}

	return true;
}

void FOpenGLProgramBinaryCache::BeginProgramReadRequest(FGLProgramBinaryFileCacheEntry* ProgramBinEntry, FArchive* Ar)
{
	check(ProgramBinEntry);

	TSharedPtr<IAsyncReadRequest, ESPMode::ThreadSafe> LocalReadRequest = ProgramBinEntry->ReadRequest.Pin();
	bool bHasReadRequest = LocalReadRequest.IsValid();

	if (ensure(!bHasReadRequest))
	{
		check(ProgramBinEntry->ProgramBinaryData.Num() == 0);
		check(ProgramBinEntry->GLProgramState == FGLProgramBinaryFileCacheEntry::EGLProgramState::ProgramStored);

		int64 ReadSize = ProgramBinEntry->FileInfo.ProgramSize;
		int64 ReadOffset = ProgramBinEntry->FileInfo.ProgramOffset;

		if(ensure(ReadSize>0))
		{
			ProgramBinEntry->ProgramBinaryData.SetNumUninitialized(ReadSize);
			ProgramBinEntry->GLProgramState = FGLProgramBinaryFileCacheEntry::EGLProgramState::ProgramLoading;
			LocalReadRequest = MakeShareable(BinaryCacheAsyncReadFileHandle->ReadRequest(ReadOffset, ReadSize, AIOP_Normal, nullptr, ProgramBinEntry->ProgramBinaryData.GetData()));
			ProgramBinEntry->ReadRequest = LocalReadRequest;
			bHasReadRequest = true;

			FExternalReadCallback ExternalReadCallback = [ProgramBinEntry, LocalReadRequest, this](double ReaminingTime)
			{
				return OnExternalReadCallback(LocalReadRequest, ProgramBinEntry, PendingGLProgramCreateRequests, ReaminingTime);
			};

			if (!Ar || !Ar->AttachExternalReadDependency(ExternalReadCallback))
			{
				// Archive does not support async loading
				// do a blocking load
				ExternalReadCallback(0.0);
			}
		}
	}
}

void FOpenGLProgramBinaryCache::OnShaderLibraryRequestShaderCode(const FSHAHash& Hash, FArchive* Ar)
{
	if (CachePtr)
	{
		CachePtr->OnShaderLibraryRequestShaderCode_internal(Hash, Ar);
	}
}

void FOpenGLProgramBinaryCache::OnShaderLibraryRequestShaderCode_internal(const FSHAHash& Hash, FArchive* Ar)
{
	FScopeLock Lock(&GProgramBinaryCacheCS);
	FGLShaderToPrograms& FoundShaderToBinary = ShaderToProgramsMap.FindOrAdd(Hash);
	if (!FoundShaderToBinary.bLoaded)
	{
		FoundShaderToBinary.bLoaded = true;

		// if the binary cache is valid, look to see if we now have any complete programs to stream in.
		// otherwise, we'll do this check bLoaded shaders when the binary cache loads.
		if (BinaryFileState == EBinaryFileState::ValidCacheFile)
		{
			for (struct FGLProgramBinaryFileCacheEntry* ProgramBinEntry : FoundShaderToBinary.AssociatedPrograms)
			{
				const FOpenGLProgramKey& ProgramKey = ProgramBinEntry->FileInfo.ShaderHasheSet;
				if (ProgramBinEntry->GLProgramState == FGLProgramBinaryFileCacheEntry::EGLProgramState::ProgramStored)
				{
					bool bAllShadersLoaded = true;
					for (int32 i = 0; i < CrossCompiler::NUM_NON_COMPUTE_SHADER_STAGES && bAllShadersLoaded; i++)
					{
						bAllShadersLoaded = ProgramKey.ShaderHashes[i] == FSHAHash() || ShaderIsLoaded(ProgramKey.ShaderHashes[i]);
					}

					if (bAllShadersLoaded)
					{
						FOpenGLProgramBinaryCache::BeginProgramReadRequest(ProgramBinEntry, Ar);
					}
				}
			}
		}
	}
}

static FDelegateHandle OnSharedShaderCodeRequest;
//static FDelegateHandle OnSharedShaderCodeRelease;

void OnShaderLibraryRequestShaderCode(const FSHAHash& Hash, FArchive* Ar)
{
	FOpenGLProgramBinaryCache::OnShaderLibraryRequestShaderCode(Hash, Ar);
}

//void OnShaderLibraryReleaseShaderCode(const FSHAHash& Hash)
//{
//}


void FOpenGLDynamicRHI::RegisterSharedShaderCodeDelegates()
{
	OnSharedShaderCodeRequest = FShaderCodeLibrary::RegisterSharedShaderCodeRequestDelegate_Handle(FSharedShaderCodeRequest::FDelegate::CreateStatic(&OnShaderLibraryRequestShaderCode));
	//OnSharedShaderCodeRelease = FShaderCodeLibrary::RegisterSharedShaderCodeReleaseDelegate_Handle(FSharedShaderCodeRelease::FDelegate::CreateStatic(&OnShaderLibraryReleaseShaderCode));
}

void FOpenGLDynamicRHI::UnregisterSharedShaderCodeDelegates()
{
	FShaderCodeLibrary::UnregisterSharedShaderCodeRequestDelegate_Handle(OnSharedShaderCodeRequest);
	//FShaderCodeLibrary::UnregisterSharedShaderCodeReleaseDelegate_Handle(OnSharedShaderCodeRelease);
}
