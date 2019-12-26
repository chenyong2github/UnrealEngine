// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MeshMaterialShader.h: Mesh material shader definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Shader.h"
#include "MaterialShaderType.h"

class FMaterial;
class FShaderCommonCompileJob;
class FShaderCompileJob;
class FUniformExpressionSet;
class FVertexFactoryType;

struct FMeshMaterialShaderPermutationParameters
{
	// Shader platform to compile to.
	const EShaderPlatform Platform;

	// Material to compile.
	const FMaterial* Material;

	// Type of vertex factory to compile.
	const FVertexFactoryType* VertexFactoryType;

	// Unique permutation identifier of the mesh material shader type.
	const int32 PermutationId;

	FMeshMaterialShaderPermutationParameters(EShaderPlatform InPlatform, const FMaterial* InMaterial, const FVertexFactoryType* InVertexFactoryType, const int32 InPermutationId)
		: Platform(InPlatform)
		, Material(InMaterial)
		, VertexFactoryType(InVertexFactoryType)
		, PermutationId(InPermutationId)
	{
	}
};

/**
 * A shader meta type for material-linked shaders which use a vertex factory.
 */
class FMeshMaterialShaderType : public FShaderType
{
public:
	struct CompiledShaderInitializerType : FMaterialShaderType::CompiledShaderInitializerType
	{
		FVertexFactoryType* VertexFactoryType;
		CompiledShaderInitializerType(
			FShaderType* InType,
			int32 PermutationId,
			const FShaderCompilerOutput& CompilerOutput,
			FShaderResource* InResource,
			const FUniformExpressionSet& InUniformExpressionSet,
			const FSHAHash& InMaterialShaderMapHash,
			const FString& InDebugDescription,
			const FShaderPipelineType* InShaderPipeline,
			FVertexFactoryType* InVertexFactoryType
			):
			FMaterialShaderType::CompiledShaderInitializerType(InType,PermutationId,CompilerOutput,InResource,InUniformExpressionSet,InMaterialShaderMapHash,InShaderPipeline,InVertexFactoryType,InDebugDescription),
			VertexFactoryType(InVertexFactoryType)
		{}
	};
	typedef FShader* (*ConstructCompiledType)(const CompiledShaderInitializerType&);
	typedef bool (*ShouldCompilePermutationType)(const FMeshMaterialShaderPermutationParameters&);
	typedef bool(*ValidateCompiledResultType)(EShaderPlatform, const TArray<FMaterial*>&, const FVertexFactoryType*, const FShaderParameterMap&, TArray<FString>&);
	typedef void (*ModifyCompilationEnvironmentType)(const FMaterialShaderPermutationParameters&, FShaderCompilerEnvironment&);

	FMeshMaterialShaderType(
		const TCHAR* InName,
		const TCHAR* InSourceFilename,
		const TCHAR* InFunctionName,
		uint32 InFrequency,
		int32 InTotalPermutationCount,
		ConstructSerializedType InConstructSerializedRef,
		ConstructCompiledType InConstructCompiledRef,
		ModifyCompilationEnvironmentType InModifyCompilationEnvironmentRef,
		ShouldCompilePermutationType InShouldCompilePermutationRef,
		ValidateCompiledResultType InValidateCompiledResultRef
		):
		FShaderType(EShaderTypeForDynamicCast::MeshMaterial, InName,InSourceFilename,InFunctionName,InFrequency,InTotalPermutationCount,InConstructSerializedRef, nullptr),
		ConstructCompiledRef(InConstructCompiledRef),
		ShouldCompilePermutationRef(InShouldCompilePermutationRef),
		ValidateCompiledResultRef(InValidateCompiledResultRef),
		ModifyCompilationEnvironmentRef(InModifyCompilationEnvironmentRef)
	{
		checkf(FPaths::GetExtension(InSourceFilename) == TEXT("usf"),
			TEXT("Incorrect virtual shader path extension for mesh material shader '%s': Only .usf files should be compiled."),
			InSourceFilename);
	}

	/**
	 * Enqueues a compilation for a new shader of this type.
	 * @param Platform - The platform to compile for.
	 * @param Material - The material to link the shader with.
	 * @param VertexFactoryType - The vertex factory to compile with.
	 */
	class FShaderCompileJob* BeginCompileShader(
		uint32 ShaderMapId,
		int32 PermutationId,
		EShaderPlatform Platform,
		const FMaterial* Material,
		FShaderCompilerEnvironment* MaterialEnvironment,
		FVertexFactoryType* VertexFactoryType,
		const FShaderPipelineType* ShaderPipeline,
		TArray<FShaderCommonCompileJob*>& NewJobs,
		FString DebugDescription,
		FString DebugExtension
		);

	static void BeginCompileShaderPipeline(
		uint32 ShaderMapId,
		int32 PermutationId,
		EShaderPlatform Platform,
		const FMaterial* Material,
		FShaderCompilerEnvironment* MaterialEnvironment,
		FVertexFactoryType* VertexFactoryType,
		const FShaderPipelineType* ShaderPipeline,
		const TArray<FMeshMaterialShaderType*>& ShaderStages,
		TArray<FShaderCommonCompileJob*>& NewJobs,
		FString DebugDescription,
		FString DebugExtension
		);

	/**
	 * Either creates a new instance of this type or returns an equivalent existing shader.
	 * @param Material - The material to link the shader with.
	 * @param CurrentJob - Compile job that was enqueued by BeginCompileShader.
	 */
	FShader* FinishCompileShader(
		const FUniformExpressionSet& UniformExpressionSet, 
		const FSHAHash& MaterialShaderMapHash,
		const FShaderCompileJob& CurrentJob,
		const FShaderPipelineType* ShaderPipeline,
		const FString& InDebugDescription
		);

	/**
	 * Checks if the shader type should be cached for a particular platform, material, and vertex factory type.
	 * @param Platform - The platform to check.
	 * @param Material - The material to check.
	 * @param VertexFactoryType - The vertex factory type to check.
	 * @return True if this shader type should be cached.
	 */
	bool ShouldCompilePermutation(EShaderPlatform Platform,const FMaterial* Material,const FVertexFactoryType* VertexFactoryType, int32 PermutationId) const
	{
		return (*ShouldCompilePermutationRef)(FMeshMaterialShaderPermutationParameters(Platform,Material,VertexFactoryType,PermutationId));
	}

	/**
	* Checks if the shader type should pass compilation for a particular set of parameters.
	* @param Platform - Platform to validate.
	* @param Materials - Materials to validate.
	* @param VertexFactoryType - Vertex factory to validate.
	* @param ParameterMap - Shader parameters to validate.
	* @param OutError - List for appending validation errors.
	*/
	bool ValidateCompiledResult(EShaderPlatform Platform, const TArray<FMaterial*>& Materials, const FVertexFactoryType* VertexFactoryType, const FShaderParameterMap& ParameterMap, TArray<FString>& OutError) const
	{
		return (*ValidateCompiledResultRef)(Platform, Materials, VertexFactoryType, ParameterMap, OutError);
	}

protected:

	/**
	 * Sets up the environment used to compile an instance of this shader type.
	 * @param Platform - Platform to compile for.
	 * @param Environment - The shader compile environment that the function modifies.
	 */
	void SetupCompileEnvironment(EShaderPlatform Platform, const FMaterial* Material, int32 PermutationId, FShaderCompilerEnvironment& Environment)
	{
		// Allow the shader type to modify its compile environment.
		(*ModifyCompilationEnvironmentRef)(FMaterialShaderPermutationParameters(Platform, Material, PermutationId), Environment);
	}

private:
	ConstructCompiledType ConstructCompiledRef;
	ShouldCompilePermutationType ShouldCompilePermutationRef;
	ValidateCompiledResultType ValidateCompiledResultRef;
	ModifyCompilationEnvironmentType ModifyCompilationEnvironmentRef;
};

/** DECLARE_MESH_MATERIAL_SHADER and IMPLEMENT_MESH_MATERIAL_SHADER setup a mesh material shader class's boiler plate. They are meant to be used like so:
 *
 * class FMyMeshMaterialShaderPS : public FMeshMaterialShader
 * {
 *		// Setup the shader's boiler plate.
 *		DECLARE_MESH_MATERIAL_SHADER(FMyMeshMaterialShaderPS);
 *
 *		// Setup the shader's permutation domain. If no dimensions, can do FPermutationDomain = FShaderPermutationNone.
 *		using FPermutationDomain = TShaderPermutationDomain<DIMENSIONS...>;
 *
 *		// ...
 * };
 *
 * // Instantiates shader's global variable that will take care of compilation process of the shader. This needs imperatively to be
 * done in a .cpp file regardless of whether FMyMeshMaterialShaderPS is in a header or not.
 * IMPLEMENT_MESH_MATERIAL_SHADER(FMyMaterialShaderPS, "/Engine/Private/MyShaderFile.usf", "MainPS", SF_Pixel);
 *
 * When the shader class is a public header, let say in Engine module public header, the shader class then should have the ENGINE_API
 * like this:
 *
 * class ENGINE_API FMyMeshMaterialShaderPS : public FMeshMaterialShader
 * {
 *		// Setup the shader's boiler plate.
 *		DECLARE_MESH_MATERIAL_SHADER(FMyMeshMaterialShaderPS);
 *
 *		// ...
 * };
 */
#define DECLARE_MESH_MATERIAL_SHADER(ShaderClass) \
	public: \
	using ShaderMetaType = FMeshMaterialShaderType; \
	\
	static ShaderMetaType StaticType; \
	\
	static FShader* ConstructSerializedInstance() { return new ShaderClass(); } \
	static FShader* ConstructCompiledInstance(const ShaderMetaType::CompiledShaderInitializerType& Initializer) \
	{ return new ShaderClass(Initializer); } \
	\
	virtual uint32 GetTypeSize() const override { return sizeof(*this); } \
	\
	static void ModifyCompilationEnvironmentImpl( \
		const FMaterialShaderPermutationParameters& Parameters, \
		FShaderCompilerEnvironment& OutEnvironment) \
	{ \
		FPermutationDomain PermutationVector(Parameters.PermutationId); \
		PermutationVector.ModifyCompilationEnvironment(OutEnvironment); \
		ShaderClass::ModifyCompilationEnvironment(Parameters, OutEnvironment); \
	}

#define IMPLEMENT_MESH_MATERIAL_SHADER(ShaderClass,SourceFilename,FunctionName,Frequency) \
	ShaderClass::ShaderMetaType ShaderClass::StaticType( \
		TEXT(#ShaderClass), \
		TEXT(SourceFilename), \
		TEXT(FunctionName), \
		Frequency, \
		ShaderClass::FPermutationDomain::PermutationCount, \
		ShaderClass::ConstructSerializedInstance, \
		ShaderClass::ConstructCompiledInstance, \
		ShaderClass::ModifyCompilationEnvironmentImpl, \
		ShaderClass::ShouldCompilePermutation, \
		ShaderClass::ValidateCompiledResult \
		)
