// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VertexFactory.h: Vertex factory definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Containers/List.h"
#include "Misc/SecureHash.h"
#include "RHI.h"
#include "RenderResource.h"
#include "ShaderCore.h"
#include "Shader.h"
#include "Misc/EnumClassFlags.h"

class FMaterial;
class FMeshDrawSingleShaderBindings;
class FPrimitiveSceneProxy;
struct FVertexFactoryShaderPermutationParameters;

#if PLATFORM_SUPPORTS_PRAGMA_PACK
	#pragma pack (push,4)
#endif

struct FVertexInputStream
{
	uint32 StreamIndex : 4;
	uint32 Offset : 28;
	FRHIVertexBuffer* VertexBuffer;

	FVertexInputStream() :
		StreamIndex(0),
		Offset(0),
		VertexBuffer(nullptr)
	{}

	FVertexInputStream(uint32 InStreamIndex, uint32 InOffset, FRHIVertexBuffer* InVertexBuffer)
		: StreamIndex(InStreamIndex), Offset(InOffset), VertexBuffer(InVertexBuffer)
	{
		// Verify no overflow
		checkSlow(InStreamIndex == StreamIndex && InOffset == Offset);
	}

	inline bool operator==(const FVertexInputStream& rhs) const
	{
		if (StreamIndex != rhs.StreamIndex ||
			Offset != rhs.Offset || 
			VertexBuffer != rhs.VertexBuffer) 
		{
			return false;
		}

		return true;
	}

	inline bool operator!=(const FVertexInputStream& rhs) const
	{
		return !(*this == rhs);
	}
};

#if PLATFORM_SUPPORTS_PRAGMA_PACK
	#pragma pack (pop)
#endif

/** 
 * Number of vertex input bindings to allocate inline within a FMeshDrawCommand.
 * This is tweaked so that the bindings for FLocalVertexFactory fit into the inline storage.
 * Overflow of the inline storage will cause a heap allocation per draw (and corresponding cache miss on traversal)
 */
typedef TArray<FVertexInputStream, TInlineAllocator<4>> FVertexInputStreamArray;

enum class EVertexStreamUsage : uint8
{
	Default			= 0 << 0,
	Instancing		= 1 << 0,
	Overridden		= 1 << 1,
	ManualFetch		= 1 << 2
};


enum class EVertexInputStreamType : uint8
{
	Default = 0,
	PositionOnly,
	PositionAndNormalOnly
};

ENUM_CLASS_FLAGS(EVertexStreamUsage);
/**
 * A typed data source for a vertex factory which streams data from a vertex buffer.
 */
struct FVertexStreamComponent
{
	/** The vertex buffer to stream data from.  If null, no data can be read from this stream. */
	const FVertexBuffer* VertexBuffer = nullptr;

	/** The offset to the start of the vertex buffer fetch. */
	uint32 StreamOffset = 0;

	/** The offset of the data, relative to the beginning of each element in the vertex buffer. */
	uint8 Offset = 0;

	/** The stride of the data. */
	uint8 Stride = 0;

	/** The type of the data read from this stream. */
	TEnumAsByte<EVertexElementType> Type = VET_None;

	EVertexStreamUsage VertexStreamUsage = EVertexStreamUsage::Default;

	/**
	 * Initializes the data stream to null.
	 */
	FVertexStreamComponent()
	{}

	/**
	 * Minimal initialization constructor.
	 */
	FVertexStreamComponent(const FVertexBuffer* InVertexBuffer, uint32 InOffset, uint32 InStride, EVertexElementType InType, EVertexStreamUsage Usage = EVertexStreamUsage::Default) :
		VertexBuffer(InVertexBuffer),
		StreamOffset(0),
		Offset(InOffset),
		Stride(InStride),
		Type(InType),
		VertexStreamUsage(Usage)
	{
		check(InStride <= 0xFF);
		check(InOffset <= 0xFF);
	}

	FVertexStreamComponent(const FVertexBuffer* InVertexBuffer, uint32 InStreamOffset, uint32 InOffset, uint32 InStride, EVertexElementType InType, EVertexStreamUsage Usage = EVertexStreamUsage::Default) :
		VertexBuffer(InVertexBuffer),
		StreamOffset(InStreamOffset),
		Offset(InOffset),
		Stride(InStride),
		Type(InType),
		VertexStreamUsage(Usage)
	{
		check(InStride <= 0xFF);
		check(InOffset <= 0xFF);
	}
};

/**
 * A macro which initializes a FVertexStreamComponent to read a member from a struct.
 */
#define STRUCTMEMBER_VERTEXSTREAMCOMPONENT(VertexBuffer,VertexType,Member,MemberType) \
	FVertexStreamComponent(VertexBuffer,STRUCT_OFFSET(VertexType,Member),sizeof(VertexType),MemberType)

/**
 * An interface to the parameter bindings for the vertex factory used by a shader.
 */
class RENDERCORE_API FVertexFactoryShaderParameters
{
	DECLARE_TYPE_LAYOUT(FVertexFactoryShaderParameters, NonVirtual);
public:
	void Bind(const class FShaderParameterMap& ParameterMap) {}

	/** 
	 * Gets the vertex factory's shader bindings and vertex streams.
	 * View can be null when caching mesh draw commands (only for supported vertex factories)
	 */
	void GetElementShaderBindings(
		const class FSceneInterface* Scene,
		const class FSceneView* View,
		const class FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const class FVertexFactory* VertexFactory,
		const struct FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const {}

private:
	// Should remove, but first need to fix some logic surrounding empty base classes
	LAYOUT_FIELD_INITIALIZED(uint32, Size_DEPRECATED, 0u);
};

template<EShaderFrequency ShaderFrequency, typename VertexFactoryType>
struct TVertexFactoryParameterTraits
{
	static const FTypeLayoutDesc* GetLayout() { return nullptr; }
	static FVertexFactoryShaderParameters* Create(const class FShaderParameterMap& ParameterMap) { return nullptr; }
	
	static void GetElementShaderBindings(
		const FVertexFactoryShaderParameters* Parameters,
		const class FSceneInterface* Scene,
		const class FSceneView* View,
		const class FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const class FVertexFactory* VertexFactory,
		const struct FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) {}
};

#define IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FactoryClass, ShaderFrequency, ParameterClass) \
	template<> struct TVertexFactoryParameterTraits<ShaderFrequency, FactoryClass> \
	{ \
		static const FTypeLayoutDesc* GetLayout() { return &StaticGetTypeLayoutDesc<ParameterClass>(); } \
		static FVertexFactoryShaderParameters* Create(const class FShaderParameterMap& ParameterMap) { ParameterClass* Result = new ParameterClass(); Result->Bind(ParameterMap); return Result; } \
		static void GetElementShaderBindings( \
			const FVertexFactoryShaderParameters* Parameters, \
			const class FSceneInterface* Scene, \
			const class FSceneView* View, \
			const class FMeshMaterialShader* Shader, \
			const EVertexInputStreamType InputStreamType, \
			ERHIFeatureLevel::Type FeatureLevel, \
			const class FVertexFactory* VertexFactory, \
			const struct FMeshBatchElement& BatchElement, \
			class FMeshDrawSingleShaderBindings& ShaderBindings, \
			FVertexInputStreamArray& VertexStreams) \
		{ \
			static_cast<const ParameterClass*>(Parameters)->GetElementShaderBindings(Scene, View, Shader, InputStreamType, FeatureLevel, VertexFactory, BatchElement, ShaderBindings, VertexStreams); \
		} \
	}

template<typename VertexFactoryType>
static const FTypeLayoutDesc* GetVertexFactoryParametersLayout(EShaderFrequency ShaderFrequency)
{
	switch (ShaderFrequency)
	{
	case SF_Vertex: return TVertexFactoryParameterTraits<SF_Vertex, VertexFactoryType>::GetLayout();
	case SF_Hull: return TVertexFactoryParameterTraits<SF_Hull, VertexFactoryType>::GetLayout();
	case SF_Domain: return TVertexFactoryParameterTraits<SF_Domain, VertexFactoryType>::GetLayout();
	case SF_Pixel: return TVertexFactoryParameterTraits<SF_Pixel, VertexFactoryType>::GetLayout();
	case SF_Geometry: return TVertexFactoryParameterTraits<SF_Geometry, VertexFactoryType>::GetLayout();
	case SF_Compute: return TVertexFactoryParameterTraits<SF_Compute, VertexFactoryType>::GetLayout();
	case SF_RayGen: return TVertexFactoryParameterTraits<SF_RayGen, VertexFactoryType>::GetLayout();
	case SF_RayMiss: return TVertexFactoryParameterTraits<SF_RayMiss, VertexFactoryType>::GetLayout();
	case SF_RayHitGroup: return TVertexFactoryParameterTraits<SF_RayHitGroup, VertexFactoryType>::GetLayout();
	case SF_RayCallable: return TVertexFactoryParameterTraits<SF_RayCallable, VertexFactoryType>::GetLayout();
	default: checkNoEntry(); return nullptr;
	}
}

template<typename VertexFactoryType>
static FVertexFactoryShaderParameters* ConstructVertexFactoryParameters(EShaderFrequency ShaderFrequency, const class FShaderParameterMap& ParameterMap)
{
	switch (ShaderFrequency)
	{
	case SF_Vertex: return TVertexFactoryParameterTraits<SF_Vertex, VertexFactoryType>::Create(ParameterMap);
	case SF_Hull: return TVertexFactoryParameterTraits<SF_Hull, VertexFactoryType>::Create(ParameterMap);
	case SF_Domain: return TVertexFactoryParameterTraits<SF_Domain, VertexFactoryType>::Create(ParameterMap);
	case SF_Pixel: return TVertexFactoryParameterTraits<SF_Pixel, VertexFactoryType>::Create(ParameterMap);
	case SF_Geometry: return TVertexFactoryParameterTraits<SF_Geometry, VertexFactoryType>::Create(ParameterMap);
	case SF_Compute: return TVertexFactoryParameterTraits<SF_Compute, VertexFactoryType>::Create(ParameterMap);
	case SF_RayGen: return TVertexFactoryParameterTraits<SF_RayGen, VertexFactoryType>::Create(ParameterMap);
	case SF_RayMiss: return TVertexFactoryParameterTraits<SF_RayMiss, VertexFactoryType>::Create(ParameterMap);
	case SF_RayHitGroup: return TVertexFactoryParameterTraits<SF_RayHitGroup, VertexFactoryType>::Create(ParameterMap);
	case SF_RayCallable: return TVertexFactoryParameterTraits<SF_RayCallable, VertexFactoryType>::Create(ParameterMap);
	default: checkNoEntry(); return nullptr;
	}
}


template<typename VertexFactoryType>
static void GetVertexFactoryParametersElementShaderBindings(EShaderFrequency ShaderFrequency, const FVertexFactoryShaderParameters* Parameters,
	const class FSceneInterface* Scene,
	const class FSceneView* View,
	const class FMeshMaterialShader* Shader,
	const EVertexInputStreamType InputStreamType,
	ERHIFeatureLevel::Type FeatureLevel,
	const class FVertexFactory* VertexFactory,
	const struct FMeshBatchElement& BatchElement,
	class FMeshDrawSingleShaderBindings& ShaderBindings,
	FVertexInputStreamArray& VertexStreams)
{
	switch (ShaderFrequency)
	{
	case SF_Vertex: TVertexFactoryParameterTraits<SF_Vertex, VertexFactoryType>::GetElementShaderBindings(Parameters, Scene, View, Shader, InputStreamType, FeatureLevel, VertexFactory, BatchElement, ShaderBindings, VertexStreams); break;
	case SF_Hull: TVertexFactoryParameterTraits<SF_Hull, VertexFactoryType>::GetElementShaderBindings(Parameters, Scene, View, Shader, InputStreamType, FeatureLevel, VertexFactory, BatchElement, ShaderBindings, VertexStreams); break;
	case SF_Domain: TVertexFactoryParameterTraits<SF_Domain, VertexFactoryType>::GetElementShaderBindings(Parameters, Scene, View, Shader, InputStreamType, FeatureLevel, VertexFactory, BatchElement, ShaderBindings, VertexStreams); break;
	case SF_Pixel: TVertexFactoryParameterTraits<SF_Pixel, VertexFactoryType>::GetElementShaderBindings(Parameters, Scene, View, Shader, InputStreamType, FeatureLevel, VertexFactory, BatchElement, ShaderBindings, VertexStreams); break;
	case SF_Geometry: TVertexFactoryParameterTraits<SF_Geometry, VertexFactoryType>::GetElementShaderBindings(Parameters, Scene, View, Shader, InputStreamType, FeatureLevel, VertexFactory, BatchElement, ShaderBindings, VertexStreams); break;
	case SF_Compute: TVertexFactoryParameterTraits<SF_Compute, VertexFactoryType>::GetElementShaderBindings(Parameters, Scene, View, Shader, InputStreamType, FeatureLevel, VertexFactory, BatchElement, ShaderBindings, VertexStreams); break;
	case SF_RayGen: TVertexFactoryParameterTraits<SF_RayGen, VertexFactoryType>::GetElementShaderBindings(Parameters, Scene, View, Shader, InputStreamType, FeatureLevel, VertexFactory, BatchElement, ShaderBindings, VertexStreams); break;
	case SF_RayMiss: TVertexFactoryParameterTraits<SF_RayMiss, VertexFactoryType>::GetElementShaderBindings(Parameters, Scene, View, Shader, InputStreamType, FeatureLevel, VertexFactory, BatchElement, ShaderBindings, VertexStreams); break;
	case SF_RayHitGroup: TVertexFactoryParameterTraits<SF_RayHitGroup, VertexFactoryType>::GetElementShaderBindings(Parameters, Scene, View, Shader, InputStreamType, FeatureLevel, VertexFactory, BatchElement, ShaderBindings, VertexStreams); break;
	case SF_RayCallable: TVertexFactoryParameterTraits<SF_RayCallable, VertexFactoryType>::GetElementShaderBindings(Parameters, Scene, View, Shader, InputStreamType, FeatureLevel, VertexFactory, BatchElement, ShaderBindings, VertexStreams); break;
	default: checkNoEntry(); break;
	}
}

/**
 * An object used to represent the type of a vertex factory.
 */
class FVertexFactoryType
{
public:

	typedef FVertexFactoryShaderParameters* (*ConstructParametersType)(EShaderFrequency ShaderFrequency, const class FShaderParameterMap& ParameterMap);
	typedef const FTypeLayoutDesc* (*GetParameterTypeLayoutType)(EShaderFrequency ShaderFrequency);
	typedef void (*GetParameterTypeElementShaderBindingsType)(EShaderFrequency ShaderFrequency,
		const FVertexFactoryShaderParameters* Parameters,
		const class FSceneInterface* Scene,
		const class FSceneView* View,
		const class FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const class FVertexFactory* VertexFactory,
		const struct FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams);

	typedef bool (*ShouldCacheType)(const FVertexFactoryShaderPermutationParameters&);
	typedef void (*ModifyCompilationEnvironmentType)(const FVertexFactoryShaderPermutationParameters&, FShaderCompilerEnvironment&);
	typedef void (*ValidateCompiledResultType)(const FVertexFactoryType*, EShaderPlatform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors);
	typedef bool (*SupportsTessellationShadersType)();

	static int32 GetNumVertexFactoryTypes() { return NumVertexFactories; }

	/**
	 * @return The global shader factory list.
	 */
	static RENDERCORE_API TLinkedList<FVertexFactoryType*>*& GetTypeList();

	static RENDERCORE_API const TArray<FVertexFactoryType*>& GetSortedMaterialTypes();

	/**
	 * Finds a FVertexFactoryType by name.
	 */
	static RENDERCORE_API FVertexFactoryType* GetVFByName(const FHashedName& VFName);

	/** Initialize FVertexFactoryType static members, this must be called before any VF types are created. */
	static void Initialize(const TMap<FString, TArray<const TCHAR*> >& ShaderFileToUniformBufferVariables);

	/** Uninitializes FVertexFactoryType cached data. */
	static void Uninitialize();

	/**
	 * Minimal initialization constructor.
	 * @param bInUsedWithMaterials - True if the vertex factory will be rendered in combination with a material.
	 * @param bInSupportsStaticLighting - True if the vertex factory will be rendered with static lighting.
	 */
	RENDERCORE_API FVertexFactoryType(
		const TCHAR* InName,
		const TCHAR* InShaderFilename,
		bool bInUsedWithMaterials,
		bool bInSupportsStaticLighting,
		bool bInSupportsDynamicLighting,
		bool bInSupportsPrecisePrevWorldPos,
		bool bInSupportsPositionOnly,
		bool bInSupportsCachingMeshDrawCommands,
		bool bInSupportsPrimitiveIdStream,
		ConstructParametersType InConstructParameters,
		GetParameterTypeLayoutType InGetParameterTypeLayout,
		GetParameterTypeElementShaderBindingsType InGetParameterTypeElementShaderBindings,
		ShouldCacheType InShouldCache,
		ModifyCompilationEnvironmentType InModifyCompilationEnvironment,
		ValidateCompiledResultType InValidateCompiledResult,
		SupportsTessellationShadersType InSupportsTessellationShaders
		);

	RENDERCORE_API virtual ~FVertexFactoryType();

	// Accessors.
	const TCHAR* GetName() const { return Name; }
	FName GetFName() const { return TypeName; }
	const FHashedName& GetHashedName() const { return HashedName; }
	const TCHAR* GetShaderFilename() const { return ShaderFilename; }

	FVertexFactoryShaderParameters* CreateShaderParameters(EShaderFrequency ShaderFrequency, const class FShaderParameterMap& ParameterMap) const { return (*ConstructParameters)(ShaderFrequency, ParameterMap); }
	const FTypeLayoutDesc* GetShaderParameterLayout(EShaderFrequency ShaderFrequency) const { return (*GetParameterTypeLayout)(ShaderFrequency); }
	void GetShaderParameterElementShaderBindings(EShaderFrequency ShaderFrequency,
		const FVertexFactoryShaderParameters* Parameters,
		const class FSceneInterface* Scene,
		const class FSceneView* View,
		const class FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const class FVertexFactory* VertexFactory,
		const struct FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const { (*GetParameterTypeElementShaderBindings)(ShaderFrequency, Parameters, Scene, View, Shader, InputStreamType, FeatureLevel, VertexFactory, BatchElement, ShaderBindings, VertexStreams); }

	bool IsUsedWithMaterials() const { return bUsedWithMaterials; }
	bool SupportsStaticLighting() const { return bSupportsStaticLighting; }
	bool SupportsDynamicLighting() const { return bSupportsDynamicLighting; }
	bool SupportsPrecisePrevWorldPos() const { return bSupportsPrecisePrevWorldPos; }
	bool SupportsPositionOnly() const { return bSupportsPositionOnly; }
	bool SupportsCachingMeshDrawCommands() const { return bSupportsCachingMeshDrawCommands; }
	bool SupportsPrimitiveIdStream() const { return bSupportsPrimitiveIdStream; }

	// Hash function.
	friend uint32 GetTypeHash(const FVertexFactoryType* Type)
	{ 
		return Type ? GetTypeHash(Type->HashedName) : 0u;
	}

	/** Calculates a Hash based on this vertex factory type's source code and includes */
	RENDERCORE_API const FSHAHash& GetSourceHash(EShaderPlatform ShaderPlatform) const;

	/**
	 * Should we cache the material's shadertype on this platform with this vertex factory? 
	 */
	bool ShouldCache(const FVertexFactoryShaderPermutationParameters& Parameters) const
	{
		return (*ShouldCacheRef)(Parameters);
	}

	/**
	* Calls the function ptr for the shader type on the given environment
	* @param Environment - shader compile environment to modify
	*/
	void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment) const
	{
		// Set up the mapping from VertexFactory.usf to the vertex factory type's source code.
		FString VertexFactoryIncludeString = FString::Printf( TEXT("#include \"%s\""), GetShaderFilename() );
		OutEnvironment.IncludeVirtualPathToContentsMap.Add(TEXT("/Engine/Generated/VertexFactory.ush"), VertexFactoryIncludeString);

		OutEnvironment.SetDefine(TEXT("HAS_PRIMITIVE_UNIFORM_BUFFER"), 1);

		(*ModifyCompilationEnvironmentRef)(Parameters, OutEnvironment);
	}

	void ValidateCompiledResult(EShaderPlatform Platform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors) const
	{
		(*ValidateCompiledResultRef)(this, Platform, ParameterMap, OutErrors);
	}

	/**
	 * Does this vertex factory support tessellation shaders?
	 */
	bool SupportsTessellationShaders() const
	{
		return (*SupportsTessellationShadersRef)(); 
	}

	/** Adds include statements for uniform buffers that this shader type references, and builds a prefix for the shader file with the include statements. */
	RENDERCORE_API void AddReferencedUniformBufferIncludes(FShaderCompilerEnvironment& OutEnvironment, FString& OutSourceFilePrefix, EShaderPlatform Platform) const;

	RENDERCORE_API void FlushShaderFileCache(const TMap<FString, TArray<const TCHAR*> >& ShaderFileToUniformBufferVariables);
	const TMap<const TCHAR*, FCachedUniformBufferDeclaration>& GetReferencedUniformBufferStructsCache() const
	{
		return ReferencedUniformBufferStructsCache;
	}

private:
	static RENDERCORE_API uint32 NumVertexFactories;

	/** Tracks whether serialization history for all shader types has been initialized. */
	static bool bInitializedSerializationHistory;

	const TCHAR* Name;
	const TCHAR* ShaderFilename;
	FName TypeName;
	FHashedName HashedName;
	uint32 bUsedWithMaterials : 1;
	uint32 bSupportsStaticLighting : 1;
	uint32 bSupportsDynamicLighting : 1;
	uint32 bSupportsPrecisePrevWorldPos : 1;
	uint32 bSupportsPositionOnly : 1;
	uint32 bSupportsCachingMeshDrawCommands : 1;
	uint32 bSupportsPrimitiveIdStream : 1;
	ConstructParametersType ConstructParameters;
	GetParameterTypeLayoutType GetParameterTypeLayout;
	GetParameterTypeElementShaderBindingsType GetParameterTypeElementShaderBindings;
	ShouldCacheType ShouldCacheRef;
	ModifyCompilationEnvironmentType ModifyCompilationEnvironmentRef;
	ValidateCompiledResultType ValidateCompiledResultRef;
	SupportsTessellationShadersType SupportsTessellationShadersRef;

	TLinkedList<FVertexFactoryType*> GlobalListLink;

	/** 
	 * Cache of referenced uniform buffer includes.  
	 * These are derived from source files so they need to be flushed when editing and recompiling shaders on the fly. 
	 * FVertexFactoryType::Initialize will add an entry for each referenced uniform buffer, but the declarations are added on demand as shaders are compiled.
	 */
	mutable TMap<const TCHAR*, FCachedUniformBufferDeclaration> ReferencedUniformBufferStructsCache;

	/** Tracks what platforms ReferencedUniformBufferStructsCache has had declarations cached for. */
	mutable FThreadSafeBool bCachedUniformBufferStructDeclarations;
};

/**
 * Serializes a reference to a vertex factory type.
 */
extern RENDERCORE_API FArchive& operator<<(FArchive& Ar,FVertexFactoryType*& TypeRef);

/**
 * Find the vertex factory type with the given name.
 * @return NULL if no vertex factory type matched, otherwise a vertex factory type with a matching name.
 */
extern RENDERCORE_API FVertexFactoryType* FindVertexFactoryType(const FHashedName& TypeName);

/**
 * A macro for declaring a new vertex factory type, for use in the vertex factory class's definition body.
 */
#define DECLARE_VERTEX_FACTORY_TYPE(FactoryClass) \
	public: \
	static FVertexFactoryType StaticType; \
	virtual FVertexFactoryType* GetType() const override;

#define IMPLEMENT_VERTEX_FACTORY_VTABLE(FactoryClass) \
	&ConstructVertexFactoryParameters<FactoryClass>, \
	&GetVertexFactoryParametersLayout<FactoryClass>, \
	&GetVertexFactoryParametersElementShaderBindings<FactoryClass>, \
	FactoryClass::ShouldCompilePermutation, \
	FactoryClass::ModifyCompilationEnvironment, \
	FactoryClass::ValidateCompiledResult, \
	FactoryClass::SupportsTessellationShaders

/**
 * A macro for implementing the static vertex factory type object, and specifying parameters used by the type.
 * @param bUsedWithMaterials - True if the vertex factory will be rendered in combination with a material.
 * @param bSupportsStaticLighting - True if the vertex factory will be rendered with static lighting.
 */
#define IMPLEMENT_VERTEX_FACTORY_TYPE(FactoryClass,ShaderFilename,bUsedWithMaterials,bSupportsStaticLighting,bSupportsDynamicLighting,bPrecisePrevWorldPos,bSupportsPositionOnly) \
	FVertexFactoryType FactoryClass::StaticType( \
		TEXT(#FactoryClass), \
		TEXT(ShaderFilename), \
		bUsedWithMaterials, \
		bSupportsStaticLighting, \
		bSupportsDynamicLighting, \
		bPrecisePrevWorldPos, \
		bSupportsPositionOnly, \
		false, \
		false, \
		IMPLEMENT_VERTEX_FACTORY_VTABLE(FactoryClass) \
		); \
		FVertexFactoryType* FactoryClass::GetType() const { return &StaticType; }

#define IMPLEMENT_TEMPLATE_VERTEX_FACTORY_TYPE_EX(TemplatePrefix, FactoryClass,ShaderFilename,bUsedWithMaterials,bSupportsStaticLighting,bSupportsDynamicLighting,bPrecisePrevWorldPos,bSupportsPositionOnly,bSupportsCachingMeshDrawCommands,bSupportsPrimitiveIdStream) \
	PREPROCESSOR_REMOVE_OPTIONAL_PARENS(TemplatePrefix) FVertexFactoryType FactoryClass::StaticType( \
		TEXT(#FactoryClass), \
		TEXT(ShaderFilename), \
		bUsedWithMaterials, \
		bSupportsStaticLighting, \
		bSupportsDynamicLighting, \
		bPrecisePrevWorldPos, \
		bSupportsPositionOnly, \
		bSupportsCachingMeshDrawCommands, \
		bSupportsPrimitiveIdStream, \
		IMPLEMENT_VERTEX_FACTORY_VTABLE(FactoryClass) \
		); \
		PREPROCESSOR_REMOVE_OPTIONAL_PARENS(TemplatePrefix) FVertexFactoryType* FactoryClass::GetType() const { return &StaticType; }

// @todo - need more extensible type properties - shouldn't have to change all IMPLEMENT_VERTEX_FACTORY_TYPE's when you add one new parameter
#define IMPLEMENT_VERTEX_FACTORY_TYPE_EX(FactoryClass,ShaderFilename,bUsedWithMaterials,bSupportsStaticLighting,bSupportsDynamicLighting,bPrecisePrevWorldPos,bSupportsPositionOnly,bSupportsCachingMeshDrawCommands,bSupportsPrimitiveIdStream) \
	IMPLEMENT_TEMPLATE_VERTEX_FACTORY_TYPE_EX(,FactoryClass,ShaderFilename,bUsedWithMaterials,bSupportsStaticLighting,bSupportsDynamicLighting,bPrecisePrevWorldPos,bSupportsPositionOnly,bSupportsCachingMeshDrawCommands,bSupportsPrimitiveIdStream)

/** Encapsulates a dependency on a vertex factory type and saved state from that vertex factory type. */
class FVertexFactoryTypeDependency
{
public:
	FVertexFactoryTypeDependency() {}

	FHashedName VertexFactoryTypeName;

	/** Used to detect changes to the vertex factory source files. */
	FSHAHash VFSourceHash;

	friend FArchive& operator<<(FArchive& Ar,class FVertexFactoryTypeDependency& Ref)
	{
		Ar << Ref.VertexFactoryTypeName << Ref.VFSourceHash;
		return Ar;
	}

	bool operator==(const FVertexFactoryTypeDependency& Reference) const
	{
		return VertexFactoryTypeName == Reference.VertexFactoryTypeName && VFSourceHash == Reference.VFSourceHash;
	}

	bool operator!=(const FVertexFactoryTypeDependency& Reference) const
	{
		return !(*this == Reference);
	}
};

/** Used to compare two Vertex Factory types by name. */
class FCompareVertexFactoryTypes											
{																				
public:		
	FORCEINLINE bool operator()(const FVertexFactoryType& A, const FVertexFactoryType& B ) const
	{
		int32 AL = FCString::Strlen(A.GetName());
		int32 BL = FCString::Strlen(B.GetName());
		if ( AL == BL )
		{
			return FCString::Strncmp(A.GetName(), B.GetName(), AL) > 0;
		}
		return AL > BL;
	}
};

/**
 * Encapsulates a vertex data source which can be linked into a vertex shader.
 */
class RENDERCORE_API FVertexFactory : public FRenderResource
{
public:
	FVertexFactory(ERHIFeatureLevel::Type InFeatureLevel) 
		: FRenderResource(InFeatureLevel)
	{
	}

	virtual FVertexFactoryType* GetType() const { return NULL; }

	void GetStreams(ERHIFeatureLevel::Type InFeatureLevel, EVertexInputStreamType VertexStreamType, FVertexInputStreamArray& OutVertexStreams) const;

	void OffsetInstanceStreams(uint32 InstanceOffset, EVertexInputStreamType VertexStreamType, FVertexInputStreamArray& VertexStreams) const;

	/**
	* Can be overridden by FVertexFactory subclasses to modify their compile environment just before compilation occurs.
	*/
	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment ) {}

	/**
	* Can be overridden by FVertexFactory subclasses to fail a compile based on compilation output.
	*/
	static void ValidateCompiledResult(const FVertexFactoryType* Type, EShaderPlatform Platform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors) {}

	/**
	* Can be overridden by FVertexFactory subclasses to enable HS/DS in D3D11
	*/
	static bool SupportsTessellationShaders() { return false; }

	// FRenderResource interface.
	virtual void ReleaseRHI();

	// Accessors.
	FVertexDeclarationRHIRef& GetDeclaration() { return Declaration; }
	void SetDeclaration(FVertexDeclarationRHIRef& NewDeclaration) { Declaration = NewDeclaration; }

	const FVertexDeclarationRHIRef& GetDeclaration(EVertexInputStreamType InputStreamType/* = FVertexInputStreamType::Default*/) const 
	{
		switch (InputStreamType)
		{
		case EVertexInputStreamType::Default:				return Declaration;
		case EVertexInputStreamType::PositionOnly:			return PositionDeclaration;
		case EVertexInputStreamType::PositionAndNormalOnly:	return PositionAndNormalDeclaration;
		}
		return Declaration;
	}

	virtual bool IsGPUSkinned() const { return false; }

	/** Indicates whether the vertex factory supports a position-only stream. */
	virtual bool SupportsPositionOnlyStream() const { return !!PositionStream.Num(); }

	/** Indicates whether the vertex factory supports a position-and-normal-only stream. */
	virtual bool SupportsPositionAndNormalOnlyStream() const { return !!PositionAndNormalStream.Num(); }

	/** Indicates whether the vertex factory supports a null pixel shader. */
	virtual bool SupportsNullPixelShader() const { return true; }

	virtual bool RendersPrimitivesAsCameraFacingSprites() const { return false; }

	bool NeedsDeclaration() const { return bNeedsDeclaration; }
	inline bool SupportsManualVertexFetch(const FStaticFeatureLevel InFeatureLevel) const
	{ 
		check(InFeatureLevel != ERHIFeatureLevel::Num);
		return bSupportsManualVertexFetch && (InFeatureLevel > ERHIFeatureLevel::ES3_1) && RHISupportsManualVertexFetch(GMaxRHIShaderPlatform);
	}

	inline int32 GetPrimitiveIdStreamIndex(EVertexInputStreamType InputStreamType) const
	{
		return PrimitiveIdStreamIndex[static_cast<uint8>(InputStreamType)];
	}


protected:

	inline void SetPrimitiveIdStreamIndex(EVertexInputStreamType InputStreamType, int32 StreamIndex)
	{
		PrimitiveIdStreamIndex[static_cast<uint8>(InputStreamType)] = StreamIndex;
	}

	/**
	 * Creates a vertex element for a vertex stream components.  Adds a unique stream index for the vertex buffer used by the component.
	 * @param Component - The vertex stream component.
	 * @param AttributeIndex - The attribute index to which the stream component is bound.
	 * @return The vertex element which corresponds to Component.
	 */
	FVertexElement AccessStreamComponent(const FVertexStreamComponent& Component,uint8 AttributeIndex);

	/**
	 * Creates a vertex element for a vertex stream component.  Adds a unique position stream index for the vertex buffer used by the component.
	 * @param Component - The vertex stream component.
	 * @param Usage - The vertex element usage semantics.
	 * @param AttributeIndex - The attribute index to which the stream component is bound.
	 * @return The vertex element which corresponds to Component.
	 */
	FVertexElement AccessStreamComponent(const FVertexStreamComponent& Component, uint8 AttributeIndex, EVertexInputStreamType InputStreamType);

	/**
	 * Initializes the vertex declaration.
	 * @param Elements - The elements of the vertex declaration.
	 */
	void InitDeclaration(const FVertexDeclarationElementList& Elements, EVertexInputStreamType StreamType = EVertexInputStreamType::Default);

	/**
	 * Information needed to set a vertex stream.
	 */
	struct FVertexStream
	{
		const FVertexBuffer* VertexBuffer = nullptr;
		uint32 Offset = 0;
		uint16 Stride = 0;
		EVertexStreamUsage VertexStreamUsage = EVertexStreamUsage::Default;
		uint8 Padding = 0;

		friend bool operator==(const FVertexStream& A,const FVertexStream& B)
		{
			return A.VertexBuffer == B.VertexBuffer && A.Stride == B.Stride && A.Offset == B.Offset && A.VertexStreamUsage == B.VertexStreamUsage;
		}

		FVertexStream()
		{
		}
	};

	/** The vertex streams used to render the factory. */
	TArray<FVertexStream,TInlineAllocator<8> > Streams;

	/* VF can explicitly set this to false to avoid errors without decls; this is for VFs that fetch from buffers directly (e.g. Niagara) */
	bool bNeedsDeclaration = true;
	
	bool bSupportsManualVertexFetch = false;

	int8 PrimitiveIdStreamIndex[3] = { -1, -1, -1 }; // Need to match entry count of EVertexInputStreamType

private:

	/** The position only vertex stream used to render the factory during depth only passes. */
	TArray<FVertexStream,TInlineAllocator<2> > PositionStream;
	TArray<FVertexStream, TInlineAllocator<3> > PositionAndNormalStream;

	/** The RHI vertex declaration used to render the factory normally. */
	FVertexDeclarationRHIRef Declaration;

	/** The RHI vertex declaration used to render the factory during depth only passes. */
	FVertexDeclarationRHIRef PositionDeclaration;
	FVertexDeclarationRHIRef PositionAndNormalDeclaration;
};

/**
* Default PrimitiveId vertex buffer.  Contains a single index of 0.
* This is used when the VF is used for rendering outside normal mesh passes, where there is no valid scene.
*/
class FPrimitiveIdDummyBuffer : public FVertexBuffer
{
public:

	virtual void InitRHI() override;

	virtual void ReleaseRHI() override
	{
		VertexBufferSRV.SafeRelease();
		FVertexBuffer::ReleaseRHI();
	}

	FShaderResourceViewRHIRef VertexBufferSRV;
};

extern RENDERCORE_API TGlobalResource<FPrimitiveIdDummyBuffer> GPrimitiveIdDummy;