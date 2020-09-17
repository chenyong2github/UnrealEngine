// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShaderFormatD3D.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IShaderFormat.h"
#include "Interfaces/IShaderFormatModule.h"
#include "DxcWrapper.h"

static FName NAME_PCD3D_SM5(TEXT("PCD3D_SM5"));
static FName NAME_PCD3D_ES3_1(TEXT("PCD3D_ES31"));

class FShaderFormatD3D : public IShaderFormat
{
	enum
	{
		/** Version for shader format, this becomes part of the DDC key. */
		UE_SHADER_PCD3D_SM5_VER = 8,
		UE_SHADER_PCD3D_ES3_1_VER = 8,
	};

	void CheckFormat(FName Format) const
	{
		check(Format == NAME_PCD3D_SM5 || Format == NAME_PCD3D_ES3_1);
	}

public:
	virtual uint32 GetVersion(FName Format) const override
	{
		CheckFormat(Format);
		if (Format == NAME_PCD3D_SM5) 
		{
			return UE_SHADER_PCD3D_SM5_VER;
		}
		else if (Format == NAME_PCD3D_ES3_1) 
		{
			return UE_SHADER_PCD3D_ES3_1_VER;
		}
		checkf(0, TEXT("Unknown Format %s"), *Format.ToString());
		return 0;
	}
	virtual void GetSupportedFormats(TArray<FName>& OutFormats) const
	{
		OutFormats.Add(NAME_PCD3D_SM5);
		OutFormats.Add(NAME_PCD3D_ES3_1);
	}

	virtual void CompileShader(FName Format, const struct FShaderCompilerInput& Input, struct FShaderCompilerOutput& Output,const FString& WorkingDirectory) const
	{
		CheckFormat(Format);
		if (Format == NAME_PCD3D_SM5)
		{
			CompileShader_Windows(Input, Output, WorkingDirectory, ELanguage::SM5);
		}
		else if (Format == NAME_PCD3D_ES3_1)
		{
			CompileShader_Windows(Input, Output, WorkingDirectory, ELanguage::ES3_1);
		}
		else
		{
			checkf(0, TEXT("Unknown format %s"), *Format.ToString());
		}
	}
	virtual const TCHAR* GetPlatformIncludeDirectory() const
	{
		return TEXT("D3D");
	}
};


/**
 * Module for D3D shaders
 */

static IShaderFormat* Singleton = nullptr;

class FShaderFormatD3DModule : public IShaderFormatModule, public FDxcModuleWrapper
{
public:
	virtual ~FShaderFormatD3DModule()
	{
		delete Singleton;
		Singleton = nullptr;
	}

	virtual IShaderFormat* GetShaderFormat()
	{
		if (!Singleton)
		{
			Singleton = new FShaderFormatD3D();
		}
		return Singleton;
	}
};

IMPLEMENT_MODULE( FShaderFormatD3DModule, ShaderFormatD3D);
