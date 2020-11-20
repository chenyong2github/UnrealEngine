// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShaderFormatD3D.h"
#include "ShaderPreprocessor.h"
#include "ShaderCompilerCommon.h"
#include "D3D11ShaderResources.h"
#include "D3D12RHI.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Serialization/MemoryWriter.h"
#include "RayTracingDefinitions.h"

DEFINE_LOG_CATEGORY_STATIC(LogD3D11ShaderCompiler, Log, All);

#define DEBUG_SHADERS 0

// D3D headers.
#define D3D_OVERLOADS 1

// D3D doesn't define a mask for this, so we do so here
#define SHADER_OPTIMIZATION_LEVEL_MASK (D3D10_SHADER_OPTIMIZATION_LEVEL0 | D3D10_SHADER_OPTIMIZATION_LEVEL1 | D3D10_SHADER_OPTIMIZATION_LEVEL2 | D3D10_SHADER_OPTIMIZATION_LEVEL3)

// Disable macro redefinition warning for compatibility with Windows SDK 8+
#pragma warning(push)
#pragma warning(disable : 4005)	// macro redefinition

#include "Windows/AllowWindowsPlatformTypes.h"
	#include <D3D11.h>
	#include <D3Dcompiler.h>
	#include <d3d11Shader.h>
#include "Windows/HideWindowsPlatformTypes.h"
#undef DrawText

#include "D3DShaderCompiler.inl"

#pragma warning(pop)

static const uint32 GD3DMaximumNumUAVs = 8; // Limit for feature level 11.0

int32 GD3DAllowRemoveUnused = 0;
static int32 GD3DCheckForDoubles = 1;
static int32 GD3DDumpAMDCodeXLFile = 0;

/**
 * TranslateCompilerFlag - translates the platform-independent compiler flags into D3DX defines
 * @param CompilerFlag - the platform-independent compiler flag to translate
 * @return uint32 - the value of the appropriate D3DX enum
 */
static uint32 TranslateCompilerFlagD3D11(ECompilerFlags CompilerFlag)
{
	// @TODO - currently d3d11 uses d3d10 shader compiler flags... update when this changes in DXSDK
	switch(CompilerFlag)
	{
	case CFLAG_PreferFlowControl: return D3D10_SHADER_PREFER_FLOW_CONTROL;
	case CFLAG_AvoidFlowControl: return D3D10_SHADER_AVOID_FLOW_CONTROL;
	default: return 0;
	};
}

/**
 * Filters out unwanted shader compile warnings
 */
static void D3D11FilterShaderCompileWarnings(const FString& CompileWarnings, TArray<FString>& FilteredWarnings)
{
	TArray<FString> WarningArray;
	FString OutWarningString = TEXT("");
	CompileWarnings.ParseIntoArray(WarningArray, TEXT("\n"), true);
	
	//go through each warning line
	for (int32 WarningIndex = 0; WarningIndex < WarningArray.Num(); WarningIndex++)
	{
		//suppress "warning X3557: Loop only executes for 1 iteration(s), forcing loop to unroll"
		if (!WarningArray[WarningIndex].Contains(TEXT("X3557"))
			// "warning X3205: conversion from larger type to smaller, possible loss of data"
			// Gets spammed when converting from float to half
			&& !WarningArray[WarningIndex].Contains(TEXT("X3205")))
		{
			FilteredWarnings.AddUnique(WarningArray[WarningIndex]);
		}
	}
}

// @return 0 if not recognized
static const TCHAR* GetShaderProfileName(FShaderTarget Target, bool bForceSM6)
{
	if(Target.Platform == SP_PCD3D_SM5)
	{
		//set defines and profiles for the appropriate shader paths
		switch(Target.Frequency)
		{
		default:
			checkfSlow(false, TEXT("Unexpected shader frequency"));
			return nullptr;
		case SF_Pixel:
			return bForceSM6 ? TEXT("ps_6_0") : TEXT("ps_5_0");
		case SF_Vertex:
			return bForceSM6 ? TEXT("vs_6_0") : TEXT("vs_5_0");
		case SF_Hull:
			return bForceSM6 ? TEXT("hs_6_0") : TEXT("hs_5_0");
		case SF_Domain:
			return bForceSM6 ? TEXT("ds_6_0") : TEXT("ds_5_0");
		case SF_Geometry:
			return bForceSM6 ? TEXT("gs_6_0") : TEXT("gs_5_0");
		case SF_Compute:
			return bForceSM6 ? TEXT("cs_6_0") : TEXT("cs_5_0");
		case SF_RayGen:
		case SF_RayMiss:
		case SF_RayHitGroup:
		case SF_RayCallable:
			return TEXT("lib_6_3");
		}
	}
	else if (Target.Platform == SP_PCD3D_ES3_1)
	{
		checkSlow(Target.Frequency == SF_Vertex ||
			Target.Frequency == SF_Pixel ||
			Target.Frequency == SF_Geometry || 
			Target.Frequency == SF_Compute);

		//set defines and profiles for the appropriate shader paths
		switch(Target.Frequency)
		{
		case SF_Pixel:
			return TEXT("ps_5_0");
		case SF_Vertex:
			return TEXT("vs_5_0");
		case SF_Geometry:
			return TEXT("gs_5_0");
		case SF_Compute:
			return TEXT("cs_5_0");
		}
	}

	return NULL;
}

/**
 * D3D11CreateShaderCompileCommandLine - takes shader parameters used to compile with the DX11
 * compiler and returns an fxc command to compile from the command line
 */
static FString D3D11CreateShaderCompileCommandLine(
	const FString& ShaderPath, 
	const TCHAR* EntryFunction, 
	const TCHAR* ShaderProfile, 
	uint32 CompileFlags,
	FShaderCompilerOutput& Output
	)
{
	// fxc is our command line compiler
	FString FXCCommandline = FString(TEXT("%FXC% ")) + ShaderPath;

	// add the entry point reference
	FXCCommandline += FString(TEXT(" /E ")) + EntryFunction;

	// @TODO - currently d3d11 uses d3d10 shader compiler flags... update when this changes in DXSDK
	// go through and add other switches
	if(CompileFlags & D3D10_SHADER_PREFER_FLOW_CONTROL)
	{
		CompileFlags &= ~D3D10_SHADER_PREFER_FLOW_CONTROL;
		FXCCommandline += FString(TEXT(" /Gfp"));
	}

	if(CompileFlags & D3D10_SHADER_DEBUG)
	{
		CompileFlags &= ~D3D10_SHADER_DEBUG;
		FXCCommandline += FString(TEXT(" /Zi"));
	}

	if(CompileFlags & D3D10_SHADER_SKIP_OPTIMIZATION)
	{
		CompileFlags &= ~D3D10_SHADER_SKIP_OPTIMIZATION;
		FXCCommandline += FString(TEXT(" /Od"));
	}

	if (CompileFlags & D3D10_SHADER_SKIP_VALIDATION)
	{
		CompileFlags &= ~D3D10_SHADER_SKIP_VALIDATION;
		FXCCommandline += FString(TEXT(" /Vd"));
	}

	if(CompileFlags & D3D10_SHADER_AVOID_FLOW_CONTROL)
	{
		CompileFlags &= ~D3D10_SHADER_AVOID_FLOW_CONTROL;
		FXCCommandline += FString(TEXT(" /Gfa"));
	}

	if(CompileFlags & D3D10_SHADER_PACK_MATRIX_ROW_MAJOR)
	{
		CompileFlags &= ~D3D10_SHADER_PACK_MATRIX_ROW_MAJOR;
		FXCCommandline += FString(TEXT(" /Zpr"));
	}

	if(CompileFlags & D3D10_SHADER_ENABLE_BACKWARDS_COMPATIBILITY)
	{
		CompileFlags &= ~D3D10_SHADER_ENABLE_BACKWARDS_COMPATIBILITY;
		FXCCommandline += FString(TEXT(" /Gec"));
	}

	switch (CompileFlags & SHADER_OPTIMIZATION_LEVEL_MASK)
	{
		case D3D10_SHADER_OPTIMIZATION_LEVEL2:
		CompileFlags &= ~D3D10_SHADER_OPTIMIZATION_LEVEL2;
		FXCCommandline += FString(TEXT(" /O2"));
			break;

		case D3D10_SHADER_OPTIMIZATION_LEVEL3:
		CompileFlags &= ~D3D10_SHADER_OPTIMIZATION_LEVEL3;
		FXCCommandline += FString(TEXT(" /O3"));
			break;

		case D3D10_SHADER_OPTIMIZATION_LEVEL1:
		CompileFlags &= ~D3D10_SHADER_OPTIMIZATION_LEVEL1;
		FXCCommandline += FString(TEXT(" /O1"));
			break;

		case D3D10_SHADER_OPTIMIZATION_LEVEL0:
			CompileFlags &= ~D3D10_SHADER_OPTIMIZATION_LEVEL0;
			break;

		default:
			Output.Errors.Emplace(TEXT("Unknown D3D10 optimization level"));
			break;
	}

	checkf(CompileFlags == 0, TEXT("Unhandled d3d11 shader compiler flag!"));

	// add the target instruction set
	FXCCommandline += FString(TEXT(" /T ")) + ShaderProfile;

	// Assembly instruction numbering
	FXCCommandline += TEXT(" /Ni");

	// Output to ShaderPath.d3dasm
	if (FPaths::GetExtension(ShaderPath) == TEXT("usf"))
	{
		FXCCommandline += FString::Printf(TEXT(" /Fc%sd3dasm"), *ShaderPath.LeftChop(3));
	}

	// add a pause on a newline
	FXCCommandline += FString(TEXT(" \r\n pause"));

	// Batch file header:
	/*
	@ECHO OFF
		SET FXC="C:\Program Files (x86)\Windows Kits\10\bin\x64\fxc.exe"
		IF EXIST %FXC% (
			REM
			) ELSE (
				ECHO Couldn't find Windows 10 SDK, falling back to DXSDK...
				SET FXC="%DXSDK_DIR%\Utilities\bin\x86\fxc.exe"
				IF EXIST %FXC% (
					REM
					) ELSE (
						ECHO Couldn't find DXSDK! Exiting...
						GOTO END
					)
			)
	*/
	FString BatchFileHeader = TEXT("@ECHO OFF\nSET FXC=\"C:\\Program Files (x86)\\Windows Kits\\10\\bin\\x64\\fxc.exe\"\n"\
		"IF EXIST %FXC% (\nREM\n) ELSE (\nECHO Couldn't find Windows 10 SDK, falling back to DXSDK...\n"\
		"SET FXC=\"%DXSDK_DIR%\\Utilities\\bin\\x86\\fxc.exe\"\nIF EXIST %FXC% (\nREM\n) ELSE (\nECHO Couldn't find DXSDK! Exiting...\n"\
		"GOTO END\n)\n)\n");
	return BatchFileHeader + FXCCommandline + TEXT("\n:END\nREM\n");
}


// Validate that we are not going over to maximum amount of resource bindings support by the default root signature on DX12
// Currently limited for hard-coded root signature setup (see: FD3D12RootSignatureDesc::GetStaticGraphicsRootSignatureDesc)
// In theory this limitation is only required for DX12, but we don't want a shader to compile on DX11 while not working on DX12.
// (DX11 has an API limit on 128 SRVs, 16 Samplers, 8 UAVs and 14 CBs but if you go over these values then the shader won't compile)
bool ValidateResourceCounts(uint32 NumSRVs, uint32 NumSamplers, uint32 NumUAVs, uint32 NumCBs, TArray<FString>& OutFilteredErrors)
{
	if (NumSRVs > MAX_SRVS || NumSamplers > MAX_SAMPLERS || NumUAVs > MAX_UAVS || NumCBs > MAX_CBS)
	{
		if (NumSRVs > MAX_SRVS)
		{
			OutFilteredErrors.Add(FString::Printf(TEXT("Shader is using too many SRVs: %d (only %d supported)"), NumSRVs, MAX_SRVS));
		}

		if (NumSamplers > MAX_SAMPLERS)
		{
			OutFilteredErrors.Add(FString::Printf(TEXT("Shader is using too many Samplers: %d (only %d supported)"), NumSamplers, MAX_SAMPLERS));
		}

		if (NumUAVs > MAX_UAVS)
		{
			OutFilteredErrors.Add(FString::Printf(TEXT("Shader is using too many UAVs: %d (only %d supported)"), NumUAVs, MAX_UAVS));
		}

		if (NumCBs > MAX_CBS)
		{
			OutFilteredErrors.Add(FString::Printf(TEXT("Shader is using too many Constant Buffers: %d (only %d supported)"), NumCBs, MAX_CBS));
		}

		return false;
	}

	return true;
}

/** Creates a batch file string to call the AMD shader analyzer. */
static FString CreateAMDCodeXLCommandLine(
	const FString& ShaderPath, 
	const TCHAR* EntryFunction, 
	const TCHAR* ShaderProfile,
	uint32 DXFlags
	)
{
	// Hardcoded to the default install path since there's no Env variable or addition to PATH
	FString Commandline = FString(TEXT("\"C:\\Program Files (x86)\\AMD\\CodeXL\\CodeXLAnalyzer.exe\" -c Pitcairn")) 
		+ TEXT(" -f ") + EntryFunction
		+ TEXT(" -s HLSL")
		+ TEXT(" -p ") + ShaderProfile
		+ TEXT(" -a AnalyzerStats.csv")
		+ TEXT(" --isa ISA.txt")
		+ *FString::Printf(TEXT(" --DXFlags %u "), DXFlags)
		+ ShaderPath;

	// add a pause on a newline
	Commandline += FString(TEXT(" \r\n pause"));
	return Commandline;
}

// D3Dcompiler.h has function pointer typedefs for some functions, but not all
typedef HRESULT(WINAPI *pD3DReflect)
	(__in_bcount(SrcDataSize) LPCVOID pSrcData,
	 __in SIZE_T  SrcDataSize,
	 __in  REFIID pInterface,
	 __out void** ppReflector);

typedef HRESULT(WINAPI *pD3DStripShader)
	(__in_bcount(BytecodeLength) LPCVOID pShaderBytecode,
	 __in SIZE_T     BytecodeLength,
	 __in UINT       uStripFlags,
	__out ID3DBlob** ppStrippedBlob);

#define DEFINE_GUID_FOR_CURRENT_COMPILER(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
	static const GUID name = { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }

// ShaderReflection IIDs may change between SDK versions if the reflection API changes.
// Define a GUID below that matches the desired IID for the DLL in CompilerPath. For example,
// look for IID_ID3D11ShaderReflection in d3d11shader.h for the SDK matching the compiler DLL.
DEFINE_GUID_FOR_CURRENT_COMPILER(IID_ID3D11ShaderReflectionForCurrentCompiler, 0x8d536ca1, 0x0cca, 0x4956, 0xa8, 0x37, 0x78, 0x69, 0x63, 0x75, 0x55, 0x84);

/**
 * GetD3DCompilerFuncs - gets function pointers from the dll at NewCompilerPath
 * @param OutD3DCompile - function pointer for D3DCompile (0 if not found)
 * @param OutD3DReflect - function pointer for D3DReflect (0 if not found)
 * @param OutD3DDisassemble - function pointer for D3DDisassemble (0 if not found)
 * @param OutD3DStripShader - function pointer for D3DStripShader (0 if not found)
 * @return bool - true if functions were retrieved from NewCompilerPath
 */
static bool GetD3DCompilerFuncs(const FString& NewCompilerPath, pD3DCompile* OutD3DCompile,
	pD3DReflect* OutD3DReflect, pD3DDisassemble* OutD3DDisassemble, pD3DStripShader* OutD3DStripShader)
{
	static FString CurrentCompiler;
	static HMODULE CompilerDLL = 0;

	if(CurrentCompiler != *NewCompilerPath)
	{
		CurrentCompiler = *NewCompilerPath;

		if(CompilerDLL)
		{
			FreeLibrary(CompilerDLL);
			CompilerDLL = 0;
		}

		if(CurrentCompiler.Len())
		{
			CompilerDLL = LoadLibrary(*CurrentCompiler);
		}

		if(!CompilerDLL && NewCompilerPath.Len())
		{
			// Couldn't find HLSL compiler in specified path. We fail the first compile.
			*OutD3DCompile = 0;
			*OutD3DReflect = 0;
			*OutD3DDisassemble = 0;
			*OutD3DStripShader = 0;
			return false;
		}
	}

	if(CompilerDLL)
	{
		// from custom folder e.g. "C:/DXWin8/D3DCompiler_44.dll"
		*OutD3DCompile = (pD3DCompile)(void*)GetProcAddress(CompilerDLL, "D3DCompile");
		*OutD3DReflect = (pD3DReflect)(void*)GetProcAddress(CompilerDLL, "D3DReflect");
		*OutD3DDisassemble = (pD3DDisassemble)(void*)GetProcAddress(CompilerDLL, "D3DDisassemble");
		*OutD3DStripShader = (pD3DStripShader)(void*)GetProcAddress(CompilerDLL, "D3DStripShader");
		return true;
	}

	// D3D SDK we compiled with (usually D3DCompiler_43.dll from windows folder)
	*OutD3DCompile = &D3DCompile;
	*OutD3DReflect = &D3DReflect;
	*OutD3DDisassemble = &D3DDisassemble;
	*OutD3DStripShader = &D3DStripShader;
	return false;
}

static HRESULT D3DCompileWrapper(
	pD3DCompile				D3DCompileFunc,
	bool&					bException,
	LPCVOID					pSrcData,
	SIZE_T					SrcDataSize,
	LPCSTR					pFileName,
	CONST D3D_SHADER_MACRO*	pDefines,
	ID3DInclude*			pInclude,
	LPCSTR					pEntrypoint,
	LPCSTR					pTarget,
	uint32					Flags1,
	uint32					Flags2,
	ID3DBlob**				ppCode,
	ID3DBlob**				ppErrorMsgs
	)
{
#if !PLATFORM_SEH_EXCEPTIONS_DISABLED
	__try
#endif
	{
		return D3DCompileFunc(
			pSrcData,
			SrcDataSize,
			pFileName,
			pDefines,
			pInclude,
			pEntrypoint,
			pTarget,
			Flags1,
			Flags2,
			ppCode,
			ppErrorMsgs
		);
	}
#if !PLATFORM_SEH_EXCEPTIONS_DISABLED
	__except( EXCEPTION_EXECUTE_HANDLER )
	{
		GSCWErrorCode = ESCWErrorCode::CrashInsidePlatformCompiler;
		bException = true;
		return E_FAIL;
	}
#endif
}

// Utility variable so we can place a breakpoint while debugging
static int32 GBreakpoint = 0;

inline bool IsCompatibleBinding(const D3D11_SHADER_INPUT_BIND_DESC& BindDesc, uint32 BindingSpace)
{
	return true;
}

bool DumpDebugShaderUSF(FString& PreprocessedShaderSource, const FShaderCompilerInput& Input)
{
	bool bDumpDebugInfo = false;

	// Write out the preprocessed file and a batch file to compile it if requested (DumpDebugInfoPath is valid)
	if (Input.DumpDebugInfoPath.Len() > 0 && IFileManager::Get().DirectoryExists(*Input.DumpDebugInfoPath))
	{
		bDumpDebugInfo = true;
		FString Filename = Input.GetSourceFilename();
		FArchive* FileWriter = IFileManager::Get().CreateFileWriter(*(Input.DumpDebugInfoPath / Filename));
		if (FileWriter)
		{
			auto AnsiSourceFile = StringCast<ANSICHAR>(*PreprocessedShaderSource);
			FileWriter->Serialize((ANSICHAR*)AnsiSourceFile.Get(), AnsiSourceFile.Length());
			{
				FString Line = CrossCompiler::CreateResourceTableFromEnvironment(Input.Environment);

				Line += TEXT("#if 0 /*DIRECT COMPILE*/\n");
				Line += CreateShaderCompilerWorkerDirectCommandLine(Input);
				Line += TEXT("\n#endif /*DIRECT COMPILE*/\n");
				Line += TEXT("//");
				Line += Input.DebugDescription;
				Line += TEXT("\n");
				FileWriter->Serialize(TCHAR_TO_ANSI(*Line), Line.Len());
			}
			FileWriter->Close();
			delete FileWriter;
		}
	}

	return bDumpDebugInfo;
}


// Generate the dumped usf file; call the D3D compiler, gather reflection information and generate the output data
bool CompileAndProcessD3DShaderFXC(FString& PreprocessedShaderSource, const FString& CompilerPath,
	uint32 CompileFlags, const FShaderCompilerInput& Input, FString& EntryPointName,
	const TCHAR* ShaderProfile, bool bProcessingSecondTime,
	TArray<FString>& FilteredErrors, FShaderCompilerOutput& Output)
{
	auto AnsiSourceFile = StringCast<ANSICHAR>(*PreprocessedShaderSource);

	// Write out the preprocessed file and a batch file to compile it if requested (DumpDebugInfoPath is valid)
	bool bDumpDebugInfo = DumpDebugShaderUSF(PreprocessedShaderSource, Input);
	FString DisasmFilename;
	if (bDumpDebugInfo)
	{
		FString BatchFileContents;
		FString Filename = Input.GetSourceFilename();
		BatchFileContents = D3D11CreateShaderCompileCommandLine(Filename, *EntryPointName, ShaderProfile, CompileFlags, Output);

		if (GD3DDumpAMDCodeXLFile)
		{
			const FString BatchFileContents2 = CreateAMDCodeXLCommandLine(Filename, *EntryPointName, ShaderProfile, CompileFlags);
			FFileHelper::SaveStringToFile(BatchFileContents2, *(Input.DumpDebugInfoPath / TEXT("CompileAMD.bat")));
		}

		FFileHelper::SaveStringToFile(BatchFileContents, *(Input.DumpDebugInfoPath / TEXT("CompileFXC.bat")));

		if (Input.bGenerateDirectCompileFile)
		{
			FFileHelper::SaveStringToFile(CreateShaderCompilerWorkerDirectCommandLine(Input), *(Input.DumpDebugInfoPath / TEXT("DirectCompile.txt")));
			FFileHelper::SaveStringToFile(Input.DebugDescription, *(Input.DumpDebugInfoPath / TEXT("permutation_info.txt")));
		}

		DisasmFilename = *(Input.DumpDebugInfoPath / TEXT("Output.d3dasm"));
	}

	TRefCountPtr<ID3DBlob> Shader;

	HRESULT Result = S_OK;
	pD3DCompile D3DCompileFunc = nullptr;
	pD3DReflect D3DReflectFunc = nullptr;
	pD3DDisassemble D3DDisassembleFunc = nullptr;
	pD3DStripShader D3DStripShaderFunc = nullptr;
	bool bCompilerPathFunctionsUsed = GetD3DCompilerFuncs(CompilerPath, &D3DCompileFunc, &D3DReflectFunc, &D3DDisassembleFunc, &D3DStripShaderFunc);
	TRefCountPtr<ID3DBlob> Errors;

	if (D3DCompileFunc)
	{
		bool bException = false;

		Result = D3DCompileWrapper(
			D3DCompileFunc,
			bException,
			AnsiSourceFile.Get(),
			AnsiSourceFile.Length(),
			TCHAR_TO_ANSI(*Input.VirtualSourceFilePath),
			/*pDefines=*/ NULL,
			/*pInclude=*/ NULL,
			TCHAR_TO_ANSI(*EntryPointName),
			TCHAR_TO_ANSI(ShaderProfile),
			CompileFlags,
			0,
			Shader.GetInitReference(),
			Errors.GetInitReference()
		);

		if (bException)
		{
			FilteredErrors.Add(TEXT("D3DCompile exception"));
		}
	}
	else
	{
		FilteredErrors.Add(FString::Printf(TEXT("Couldn't find shader compiler: %s"), *CompilerPath));
		Result = E_FAIL;
	}

	// Filter any errors.
	void* ErrorBuffer = Errors ? Errors->GetBufferPointer() : NULL;
	if (ErrorBuffer)
	{
		D3D11FilterShaderCompileWarnings(ANSI_TO_TCHAR(ErrorBuffer), FilteredErrors);
	}

	// Fail the compilation if certain extended features are being used, since those are not supported on all D3D11 cards.
	if (SUCCEEDED(Result) && D3DDisassembleFunc)
	{
		const bool bCheckForTypedUAVs = !Input.Environment.CompilerFlags.Contains(CFLAG_AllowTypedUAVLoads);
		if (GD3DCheckForDoubles || bCheckForTypedUAVs || bDumpDebugInfo)
		{
			TRefCountPtr<ID3DBlob> Disassembly;
			if (SUCCEEDED(D3DDisassembleFunc(Shader->GetBufferPointer(), Shader->GetBufferSize(), 0, "", Disassembly.GetInitReference())))
			{
				ANSICHAR* DisassemblyString = new ANSICHAR[Disassembly->GetBufferSize() + 1];
				FMemory::Memcpy(DisassemblyString, Disassembly->GetBufferPointer(), Disassembly->GetBufferSize());
				DisassemblyString[Disassembly->GetBufferSize()] = 0;
				FString DisassemblyStringW(DisassemblyString);
				delete[] DisassemblyString;

				if (bDumpDebugInfo)
				{
					FFileHelper::SaveStringToFile(DisassemblyStringW, *(Input.DumpDebugInfoPath / TEXT("Output.d3dasm")));
				}

				if (GD3DCheckForDoubles)
				{
					// dcl_globalFlags will contain enableDoublePrecisionFloatOps when the shader uses doubles, even though the docs on dcl_globalFlags don't say anything about this
					if (DisassemblyStringW.Contains(TEXT("enableDoublePrecisionFloatOps")))
					{
						FilteredErrors.Add(TEXT("Shader uses double precision floats, which are not supported on all D3D11 hardware!"));
						return false;
					}
				}
					
				if (bCheckForTypedUAVs)
				{
					// Disassembly will contain this text with typed loads from UAVs are used where the format and dimension are not fully supported
					// across all versions of Windows (like Windows 7/8.1).
					// https://microsoft.github.io/DirectX-Specs/d3d/UAVTypedLoad.html
					// https://docs.microsoft.com/en-us/windows/win32/direct3d12/typed-unordered-access-view-loads
					// https://docs.microsoft.com/en-us/windows/win32/direct3ddxgi/format-support-for-direct3d-11-0-feature-level-hardware
					if (DisassemblyStringW.Contains(TEXT("Typed UAV Load Additional Formats")))
					{
						FilteredErrors.Add(TEXT("Shader uses UAV loads from additional typed formats, which are not supported on all D3D11 hardware! Set r.D3D.CheckedForTypedUAVs=0 if you want to allow typed UAV loads for your project, or individual shaders can opt-in by specifying CFLAG_AllowTypedUAVLoads."));
						return false;
					}
				}
			}
		}
	}

	// Gather reflection information
	int32 NumInterpolants = 0;
	TIndirectArray<FString> InterpolantNames;
	TArray<FString> ShaderInputs;
	TArray<FShaderCodeVendorExtension> VendorExtensions;

	if (SUCCEEDED(Result))
	{
		bool bGlobalUniformBufferUsed = false;
		uint32 NumInstructions = 0;
		uint32 NumSamplers = 0;
		uint32 NumSRVs = 0;
		uint32 NumCBs = 0;
		uint32 NumUAVs = 0;
		uint32 OutputMask = 0;

		TArray<FString> UniformBufferNames;
		TArray<FString> ShaderOutputs;

		TBitArray<> UsedUniformBufferSlots;
		UsedUniformBufferSlots.Init(false, 32);

		if (D3DReflectFunc)
		{
			Output.bSucceeded = true;
			ID3D11ShaderReflection* Reflector = NULL;

			// IID_ID3D11ShaderReflectionForCurrentCompiler is defined in this file and needs to match the IID from the dll in CompilerPath
			// if the function pointers from that dll are being used
			const IID ShaderReflectionInterfaceID = bCompilerPathFunctionsUsed ? IID_ID3D11ShaderReflectionForCurrentCompiler : IID_ID3D11ShaderReflection;
			Result = D3DReflectFunc(Shader->GetBufferPointer(), Shader->GetBufferSize(), ShaderReflectionInterfaceID, (void**)&Reflector);
			if (FAILED(Result))
			{
				UE_LOG(LogD3D11ShaderCompiler, Fatal, TEXT("D3DReflect failed: Result=%08x"), Result);
			}

			// Read the constant table description.
			D3D11_SHADER_DESC ShaderDesc;
			Reflector->GetDesc(&ShaderDesc);

			if (Input.Target.Frequency == SF_Vertex)
			{
				for (uint32 Index = 0; Index < ShaderDesc.OutputParameters; ++Index)
				{
					// VC++ horrible hack: Runtime ESP checks get confused and fail for some reason calling Reflector->GetOutputParameterDesc() (because it comes from another DLL?)
					// so "guard it" using the middle of an array; it's been confirmed NO corruption is really happening.
					D3D11_SIGNATURE_PARAMETER_DESC ParamDescs[3];
					D3D11_SIGNATURE_PARAMETER_DESC& ParamDesc = ParamDescs[1];
					Reflector->GetOutputParameterDesc(Index, &ParamDesc);
					if (ParamDesc.SystemValueType == D3D_NAME_UNDEFINED && ParamDesc.Mask != 0)
					{
						++NumInterpolants;
						InterpolantNames.Add(new FString(FString::Printf(TEXT("%s%d"), ANSI_TO_TCHAR(ParamDesc.SemanticName), ParamDesc.SemanticIndex)));
						ShaderOutputs.Add(*InterpolantNames.Last());
					}
				}
			}
			else if (Input.Target.Frequency == SF_Pixel)
			{
				if (GD3DAllowRemoveUnused != 0 && Input.bCompilingForShaderPipeline)
				{
					// Handy place for a breakpoint for debugging...
					++GBreakpoint;
				}
				for(uint32 Index = 0; Index < ShaderDesc.OutputParameters; ++Index)
				{
					// VC++ horrible hack: Runtime ESP checks get confused and fail for some reason calling Reflector->GetInputParameterDesc() (because it comes from another DLL?)
					// so "guard it" using the middle of an array; it's been confirmed NO corruption is really happening.
					D3D11_SIGNATURE_PARAMETER_DESC ParamDescs[3];
					D3D11_SIGNATURE_PARAMETER_DESC& ParamDesc = ParamDescs[1];
					Reflector->GetOutputParameterDesc(Index, &ParamDesc);
					OutputMask |= (1 << ParamDesc.Register);
				}

				bool bFoundUnused = false;
				for (uint32 Index = 0; Index < ShaderDesc.InputParameters; ++Index)
				{
					// VC++ horrible hack: Runtime ESP checks get confused and fail for some reason calling Reflector->GetInputParameterDesc() (because it comes from another DLL?)
					// so "guard it" using the middle of an array; it's been confirmed NO corruption is really happening.
					D3D11_SIGNATURE_PARAMETER_DESC ParamDescs[3];
					D3D11_SIGNATURE_PARAMETER_DESC& ParamDesc = ParamDescs[1];
					Reflector->GetInputParameterDesc(Index, &ParamDesc);
					if (ParamDesc.SystemValueType == D3D_NAME_UNDEFINED)
					{
						if (ParamDesc.ReadWriteMask != 0)
						{
							FString SemanticName = ANSI_TO_TCHAR(ParamDesc.SemanticName);

							ShaderInputs.AddUnique(SemanticName);

							// Add the number (for the case of TEXCOORD)
							FString SemanticIndexName = FString::Printf(TEXT("%s%d"), *SemanticName, ParamDesc.SemanticIndex);
							ShaderInputs.AddUnique(SemanticIndexName);

							// Add _centroid
							ShaderInputs.AddUnique(SemanticName + TEXT("_centroid"));
							ShaderInputs.AddUnique(SemanticIndexName + TEXT("_centroid"));
						}
						else
						{
							bFoundUnused = true;
						}
					}
					else
					{
						//if (ParamDesc.ReadWriteMask != 0)
						{
							// Keep system values
							ShaderInputs.AddUnique(FString(ANSI_TO_TCHAR(ParamDesc.SemanticName)));
						}
					}
				}

				if (GD3DAllowRemoveUnused && Input.bCompilingForShaderPipeline && bFoundUnused && !bProcessingSecondTime)
				{
					// Rewrite the source removing the unused inputs so the bindings will match
					TArray<FString> RemoveErrors;
					if (RemoveUnusedInputs(PreprocessedShaderSource, ShaderInputs, EntryPointName, RemoveErrors))
					{
						return CompileAndProcessD3DShaderFXC(PreprocessedShaderSource, CompilerPath, CompileFlags, Input, EntryPointName, ShaderProfile, true, FilteredErrors, Output);
					}
					else
					{
						UE_LOG(LogD3D11ShaderCompiler, Warning, TEXT("Failed to Remove unused inputs [%s]!"), *Input.DumpDebugInfoPath);
						for (int32 Index = 0; Index < RemoveErrors.Num(); ++Index)
						{
							FShaderCompilerError NewError;
							NewError.StrippedErrorMessage = RemoveErrors[Index];
							Output.Errors.Add(NewError);
						}
						Output.bFailedRemovingUnused = true;
					}
				}
			}

			const uint32 BindingSpace = 0; // Default binding space for D3D11 shaders
			ExtractParameterMapFromD3DShader<
				ID3D11ShaderReflection, D3D11_SHADER_DESC, D3D11_SHADER_INPUT_BIND_DESC,
				ID3D11ShaderReflectionConstantBuffer, D3D11_SHADER_BUFFER_DESC,
				ID3D11ShaderReflectionVariable, D3D11_SHADER_VARIABLE_DESC>(Input.Target.Platform, BindingSpace, Input.VirtualSourceFilePath, Reflector, ShaderDesc,
					bGlobalUniformBufferUsed, NumSamplers, NumSRVs, NumCBs, NumUAVs,
					Output, UniformBufferNames, UsedUniformBufferSlots, VendorExtensions);

			NumInstructions = ShaderDesc.InstructionCount;

			// Reflector is a com interface, so it needs to be released.
			Reflector->Release();
		}
		else
		{
			FilteredErrors.Add(FString::Printf(TEXT("Couldn't find shader reflection function in %s"), *CompilerPath));
			Result = E_FAIL;
			Output.bSucceeded = false;
		}
		
		if (!ValidateResourceCounts(NumSRVs, NumSamplers, NumUAVs, NumCBs, FilteredErrors))
		{
			Result = E_FAIL;
			Output.bSucceeded = false;
		}

		// Check for resource limits for feature level 11.0
		if (NumUAVs > GD3DMaximumNumUAVs)
		{
			Output.bSucceeded = false;
			UE_LOG(LogD3D11ShaderCompiler, Fatal, TEXT("Number of UAVs in \"%s\" exceeded limit: %d slots used, but limit is %d due to maximum feature level 11.0"), *Input.VirtualSourceFilePath, NumUAVs, GD3DMaximumNumUAVs);
		}

		// Save results if compilation and reflection succeeded
		if (Output.bSucceeded)
		{
			TRefCountPtr<ID3DBlob> CompressedData;

			if (Input.Environment.CompilerFlags.Contains(CFLAG_KeepDebugInfo))
			{
				CompressedData = Shader;
			}
			else if (D3DStripShaderFunc)
			{
				// Strip shader reflection and debug info
				D3D_SHADER_DATA ShaderData;
				ShaderData.pBytecode = Shader->GetBufferPointer();
				ShaderData.BytecodeLength = Shader->GetBufferSize();
				Result = D3DStripShaderFunc(Shader->GetBufferPointer(),
					Shader->GetBufferSize(),
					D3DCOMPILER_STRIP_REFLECTION_DATA | D3DCOMPILER_STRIP_DEBUG_INFO | D3DCOMPILER_STRIP_TEST_BLOBS,
					CompressedData.GetInitReference());

				if (FAILED(Result))
				{
					UE_LOG(LogD3D11ShaderCompiler, Fatal, TEXT("D3DStripShader failed: Result=%08x"), Result);
				}
			}
			else
			{
				// D3DStripShader is not guaranteed to exist
				// e.g. the open-source DXIL shader compiler does not currently implement it
				CompressedData = Shader;
			}

			FShaderCodePackedResourceCounts PackedResourceCounts = { bGlobalUniformBufferUsed, static_cast<uint8>(NumSamplers), static_cast<uint8>(NumSRVs), static_cast<uint8>(NumCBs), static_cast<uint8>(NumUAVs), static_cast<uint16>(OutputMask)};
			GenerateFinalOutput(CompressedData,
				Input, VendorExtensions,
				UsedUniformBufferSlots, UniformBufferNames,
				bProcessingSecondTime, ShaderInputs,
				PackedResourceCounts, NumInstructions,
				Output,
				[](FMemoryWriter&){},
				[](FShaderCode&){});
		}
	}

	if (FAILED(Result))
	{
		++GBreakpoint;
	}

	return SUCCEEDED(Result);
}

void CompileD3DShader(const FShaderCompilerInput& Input, FShaderCompilerOutput& Output, FShaderCompilerDefinitions& AdditionalDefines, const FString& WorkingDirectory, ELanguage Language)
{
	FString PreprocessedShaderSource;
	const bool bIsRayTracingShader = Input.IsRayTracingShader();
	const bool bUseDXC = bIsRayTracingShader
		|| Input.Environment.CompilerFlags.Contains(CFLAG_WaveOperations)
		|| Input.Environment.CompilerFlags.Contains(CFLAG_ForceDXC);
	const TCHAR* ShaderProfile = GetShaderProfileName(Input.Target, bUseDXC);

	if(!ShaderProfile)
	{
		Output.Errors.Add(FShaderCompilerError(TEXT("Unrecognized shader frequency")));
		return;
	}

	// Set additional defines.
	AdditionalDefines.SetDefine(TEXT("COMPILER_HLSL"), 1);

	if (bUseDXC)
	{
		AdditionalDefines.SetDefine(TEXT("PLATFORM_SUPPORTS_SM6_0_WAVE_OPERATIONS"), 1);
		AdditionalDefines.SetDefine(TEXT("PLATFORM_SUPPORTS_STATIC_SAMPLERS"), 1);
	}

	if (Input.bSkipPreprocessedCache)
	{
		if (!FFileHelper::LoadFileToString(PreprocessedShaderSource, *Input.VirtualSourceFilePath))
		{
			return;
		}

		// Remove const as we are on debug-only mode
		CrossCompiler::CreateEnvironmentFromResourceTable(PreprocessedShaderSource, (FShaderCompilerEnvironment&)Input.Environment);
	}
	else
	{
		if (!PreprocessShader(PreprocessedShaderSource, Output, Input, AdditionalDefines))
		{
			// The preprocessing stage will add any relevant errors.
			return;
		}
	}

	GD3DAllowRemoveUnused = Input.Environment.CompilerFlags.Contains(CFLAG_ForceRemoveUnusedInterpolators) ? 1 : 0;

	FString EntryPointName = Input.EntryPointName;

	Output.bFailedRemovingUnused = false;
	if (GD3DAllowRemoveUnused == 1 && Input.Target.Frequency == SF_Vertex && Input.bCompilingForShaderPipeline)
	{
		// Always add SV_Position
		TArray<FString> UsedOutputs = Input.UsedOutputs;
		UsedOutputs.AddUnique(TEXT("SV_POSITION"));

		// We can't remove any of the output-only system semantics
		//@todo - there are a bunch of tessellation ones as well
		TArray<FString> Exceptions;
		Exceptions.AddUnique(TEXT("SV_ClipDistance"));
		Exceptions.AddUnique(TEXT("SV_ClipDistance0"));
		Exceptions.AddUnique(TEXT("SV_ClipDistance1"));
		Exceptions.AddUnique(TEXT("SV_ClipDistance2"));
		Exceptions.AddUnique(TEXT("SV_ClipDistance3"));
		Exceptions.AddUnique(TEXT("SV_ClipDistance4"));
		Exceptions.AddUnique(TEXT("SV_ClipDistance5"));
		Exceptions.AddUnique(TEXT("SV_ClipDistance6"));
		Exceptions.AddUnique(TEXT("SV_ClipDistance7"));

		Exceptions.AddUnique(TEXT("SV_CullDistance"));
		Exceptions.AddUnique(TEXT("SV_CullDistance0"));
		Exceptions.AddUnique(TEXT("SV_CullDistance1"));
		Exceptions.AddUnique(TEXT("SV_CullDistance2"));
		Exceptions.AddUnique(TEXT("SV_CullDistance3"));
		Exceptions.AddUnique(TEXT("SV_CullDistance4"));
		Exceptions.AddUnique(TEXT("SV_CullDistance5"));
		Exceptions.AddUnique(TEXT("SV_CullDistance6"));
		Exceptions.AddUnique(TEXT("SV_CullDistance7"));
		
		// Write the preprocessed file out in case so we can debug issues on HlslParser
		DumpDebugShaderUSF(PreprocessedShaderSource, Input);

		TArray<FString> Errors;
		if (!RemoveUnusedOutputs(PreprocessedShaderSource, UsedOutputs, Exceptions, EntryPointName, Errors))
		{
			DumpDebugShaderUSF(PreprocessedShaderSource, Input);
			UE_LOG(LogD3D11ShaderCompiler, Warning, TEXT("Failed to Remove unused outputs [%s]!"), *Input.DumpDebugInfoPath);
			for (int32 Index = 0; Index < Errors.Num(); ++Index)
			{
				FShaderCompilerError NewError;
				NewError.StrippedErrorMessage = Errors[Index];
				Output.Errors.Add(NewError);
			}
			Output.bFailedRemovingUnused = true;
		}
	}

	FShaderParameterParser ShaderParameterParser;
	if (!ShaderParameterParser.ParseAndMoveShaderParametersToRootConstantBuffer(
		Input, Output, PreprocessedShaderSource,
		Input.IsRayTracingShader() ? TEXT("cbuffer") : nullptr))
	{
		// The FShaderParameterParser will add any relevant errors.
		return;
	}

	RemoveUniformBuffersFromSource(Input.Environment, PreprocessedShaderSource);

	// @TODO - currently d3d11 uses d3d10 shader compiler flags... update when this changes in DXSDK
	// @TODO - implement different material path to allow us to remove backwards compat flag on sm5 shaders
	uint32 CompileFlags = D3D10_SHADER_ENABLE_BACKWARDS_COMPATIBILITY
		// Unpack uniform matrices as row-major to match the CPU layout.
		| D3D10_SHADER_PACK_MATRIX_ROW_MAJOR;

	if (Input.Environment.CompilerFlags.Contains(CFLAG_Debug)) 
	{
		//add the debug flags
		CompileFlags |= D3D10_SHADER_DEBUG | D3D10_SHADER_SKIP_OPTIMIZATION;
	}
	else
	{
		if (Input.Environment.CompilerFlags.Contains(CFLAG_StandardOptimization))
		{
			CompileFlags |= D3D10_SHADER_OPTIMIZATION_LEVEL1;
		}
		else
		{
			CompileFlags |= D3D10_SHADER_OPTIMIZATION_LEVEL3;
		}
	}

	for (int32 FlagIndex = 0; FlagIndex < Input.Environment.CompilerFlags.Num(); FlagIndex++)
	{
		//accumulate flags set by the shader
		CompileFlags |= TranslateCompilerFlagD3D11((ECompilerFlags)Input.Environment.CompilerFlags[FlagIndex]);
	}

	TArray<FString> FilteredErrors;
	if (bUseDXC)
	{
		if (!CompileAndProcessD3DShaderDXC(PreprocessedShaderSource, CompileFlags, Input, EntryPointName, ShaderProfile, Language, false, FilteredErrors, Output))
		{
			if (!FilteredErrors.Num())
			{
				FilteredErrors.Add(TEXT("Compile Failed without errors!"));
			}
		}
		CrossCompiler::FShaderConductorContext::ConvertCompileErrors(MoveTemp(FilteredErrors), Output.Errors);
	}
	else
	{
		// Override default compiler path to newer dll
		FString CompilerPath = FPaths::EngineDir();
		CompilerPath.Append(TEXT("Binaries/ThirdParty/Windows/DirectX/x64/d3dcompiler_47.dll"));

		if (!CompileAndProcessD3DShaderFXC(PreprocessedShaderSource, CompilerPath, CompileFlags, Input, EntryPointName, ShaderProfile, false, FilteredErrors, Output))
		{
			if (!FilteredErrors.Num())
			{
				FilteredErrors.Add(TEXT("Compile Failed without errors!"));
			}
		}

		// Process errors
		for (int32 ErrorIndex = 0; ErrorIndex < FilteredErrors.Num(); ErrorIndex++)
		{
			const FString& CurrentError = FilteredErrors[ErrorIndex];
			FShaderCompilerError NewError;

			// Extract filename and line number from FXC output with format:
			// "d:\UE4\Binaries\BasePassPixelShader(30,7): error X3000: invalid target or usage string"
			int32 FirstParenIndex = CurrentError.Find(TEXT("("));
			int32 LastParenIndex = CurrentError.Find(TEXT("):"));
			if (FirstParenIndex != INDEX_NONE &&
				LastParenIndex != INDEX_NONE &&
				LastParenIndex > FirstParenIndex)
			{
				// Extract and store error message with source filename
				NewError.ErrorVirtualFilePath = CurrentError.Left(FirstParenIndex);
				NewError.ErrorLineString = CurrentError.Mid(FirstParenIndex + 1, LastParenIndex - FirstParenIndex - FCString::Strlen(TEXT("(")));
				NewError.StrippedErrorMessage = CurrentError.Right(CurrentError.Len() - LastParenIndex - FCString::Strlen(TEXT("):")));
			}
			else
			{
				NewError.StrippedErrorMessage = CurrentError;
			}
			Output.Errors.Add(NewError);
		}
	}

	const bool bDirectCompile = FParse::Param(FCommandLine::Get(), TEXT("directcompile"));
	if (bDirectCompile)
	{
		for (const auto& Error : Output.Errors)
		{
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("%s\n"), *Error.GetErrorStringWithLineMarker());
		}
	}

	ShaderParameterParser.ValidateShaderParameterTypes(Input, Output);

	if (Input.ExtraSettings.bExtractShaderSource)
	{
		Output.OptionalFinalShaderSource = PreprocessedShaderSource;
	}
}

void CompileShader_Windows(const FShaderCompilerInput& Input,FShaderCompilerOutput& Output,const FString& WorkingDirectory, ELanguage Language)
{
	FShaderCompilerDefinitions AdditionalDefines;
	if (Language == ELanguage::SM5)
	{
		check(Input.Target.Platform == SP_PCD3D_SM5);
		AdditionalDefines.SetDefine(TEXT("SM5_PROFILE"), 1);
	}
	else if (Language == ELanguage::ES3_1)
	{
		check(Input.Target.Platform == SP_PCD3D_ES3_1);
		AdditionalDefines.SetDefine(TEXT("ES3_1_PROFILE"), 1);
	}
	else
	{
		checkf(0, TEXT("Unknown ELanguage %d"), (int32)Language);
	}

	CompileD3DShader(Input, Output, AdditionalDefines, WorkingDirectory, Language);
}
