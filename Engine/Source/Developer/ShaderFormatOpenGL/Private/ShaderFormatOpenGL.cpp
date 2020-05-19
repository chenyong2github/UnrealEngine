// Copyright Epic Games, Inc. All Rights Reserved.
//

#include "ShaderFormatOpenGL.h"
#include "HAL/FileManager.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IShaderFormat.h"
#include "Interfaces/IShaderFormatModule.h"
#include "ShaderCore.h"

static FName NAME_GLSL_430(TEXT("GLSL_430"));
static FName NAME_GLSL_150_ES3_1(TEXT("GLSL_150_ES31"));
static FName NAME_GLSL_310_ES_EXT(TEXT("GLSL_310_ES_EXT"));
static FName NAME_GLSL_ES3_1_ANDROID(TEXT("GLSL_ES3_1_ANDROID"));
 
class FShaderFormatGLSL : public IShaderFormat
{
	enum
	{
		/** Version for shader format, this becomes part of the DDC key. */
		UE_SHADER_GLSL_VER = 93,
	};

	void CheckFormat(FName Format) const
	{
		check(	Format == NAME_GLSL_430 || 
				Format == NAME_GLSL_150_ES3_1 ||
				Format == NAME_GLSL_310_ES_EXT ||
				Format == NAME_GLSL_ES3_1_ANDROID
			);
	}

public:
	virtual uint32 GetVersion(FName Format) const override
	{
		CheckFormat(Format);
		uint32 GLSLVersion = 0;
		if (Format == NAME_GLSL_150_ES3_1
			|| Format == NAME_GLSL_310_ES_EXT
			|| Format == NAME_GLSL_ES3_1_ANDROID)
		{
			GLSLVersion = UE_SHADER_GLSL_VER;
		}
		else if (Format == NAME_GLSL_430)
		{
			GLSLVersion = UE_SHADER_GLSL_VER + 1;
		}
		const uint16 Version = ((HLSLCC_VersionMinor & 0xff) << 8) | (GLSLVersion & 0xff);
		return Version;
	}

	virtual void GetSupportedFormats(TArray<FName>& OutFormats) const override
	{
		OutFormats.Add(NAME_GLSL_430);
		OutFormats.Add(NAME_GLSL_150_ES3_1);
		OutFormats.Add(NAME_GLSL_310_ES_EXT);
		OutFormats.Add(NAME_GLSL_ES3_1_ANDROID);
	}

	static GLSLVersion TranslateFormatNameToEnum(FName Format)
	{
		if (Format == NAME_GLSL_430)
		{
			return GLSL_430;
		}
		else if (Format == NAME_GLSL_150_ES3_1)
		{
			return GLSL_150_ES3_1;
		}
		else if (Format == NAME_GLSL_310_ES_EXT)
		{
			return GLSL_310_ES_EXT;
		}
		else if (Format == NAME_GLSL_ES3_1_ANDROID)
		{
			return GLSL_ES3_1_ANDROID;
		}
		else
		{
			check(0);
			return GLSL_MAX;
		}
	}

	virtual void CompileShader(FName Format, const struct FShaderCompilerInput& Input, struct FShaderCompilerOutput& Output,const FString& WorkingDirectory) const override
	{
		CheckFormat(Format);

		GLSLVersion Version = TranslateFormatNameToEnum(Format);

		FOpenGLFrontend Frontend;
		// the frontend will run the cross compiler
		Frontend.CompileShader(Input, Output, WorkingDirectory, Version);
	}

	virtual const TCHAR* GetPlatformIncludeDirectory() const
	{
		return TEXT("");
	}
};

/**
 * Module for OpenGL shaders
 */

static IShaderFormat* Singleton = NULL;

class FShaderFormatOpenGLModule : public IShaderFormatModule
{
public:
	virtual ~FShaderFormatOpenGLModule()
	{
		delete Singleton;
		Singleton = NULL;
	}
	virtual IShaderFormat* GetShaderFormat()
	{
		if (!Singleton)
		{
			Singleton = new FShaderFormatGLSL();
		}
		return Singleton;
	}
};

IMPLEMENT_MODULE( FShaderFormatOpenGLModule, ShaderFormatOpenGL);
