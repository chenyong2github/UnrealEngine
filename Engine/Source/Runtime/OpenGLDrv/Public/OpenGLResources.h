// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	OpenGLResources.h: OpenGL resource RHI definitions.
=============================================================================*/

#pragma once

#include "CoreTypes.h"
#include "Misc/AssertionMacros.h"
#include "HAL/UnrealMemory.h"
#include "HAL/LowLevelMemTracker.h"
#include "Containers/ContainerAllocationPolicies.h"
#include "Containers/Array.h"
#include "Math/UnrealMathUtility.h"
#include "Logging/LogMacros.h"
#include "Containers/BitArray.h"
#include "Math/IntPoint.h"
#include "Misc/CommandLine.h"
#include "Templates/RefCounting.h"
#include "Stats/Stats.h"
#include "RHI.h"
#include "BoundShaderStateCache.h"
#include "RenderResource.h"
#include "OpenGLShaderResources.h"
#include "PsoLruCache.h"

class FOpenGLDynamicRHI;
class FOpenGLLinkedProgram;
typedef TArray<ANSICHAR> FAnsiCharArray;


extern void OnVertexBufferDeletion( GLuint VertexBufferResource );
extern void OnIndexBufferDeletion( GLuint IndexBufferResource );
extern void OnPixelBufferDeletion( GLuint PixelBufferResource );
extern void OnUniformBufferDeletion( GLuint UniformBufferResource, uint32 AllocatedSize, bool bStreamDraw, uint32 Offset, uint8* Pointer );
extern void OnProgramDeletion( GLint ProgramResource );

extern void CachedBindArrayBuffer( GLuint Buffer );
extern void CachedBindElementArrayBuffer( GLuint Buffer );
extern void CachedBindPixelUnpackBuffer( GLuint Buffer );
extern void CachedBindUniformBuffer( GLuint Buffer );
extern bool IsUniformBufferBound( GLuint Buffer );

namespace OpenGLConsoleVariables
{
	extern int32 bUseMapBuffer;
	extern int32 MaxSubDataSize;
	extern int32 bUseStagingBuffer;
	extern int32 bBindlessTexture;
	extern int32 bUseBufferDiscard;
};

#if PLATFORM_WINDOWS || PLATFORM_LUMINGL4
#define RESTRICT_SUBDATA_SIZE 1
#else
#define RESTRICT_SUBDATA_SIZE 0
#endif

void IncrementBufferMemory(GLenum Type, bool bStructuredBuffer, uint32 NumBytes);
void DecrementBufferMemory(GLenum Type, bool bStructuredBuffer, uint32 NumBytes);

// Extra stats for finer-grained timing
// They shouldn't always be on, as they may impact overall performance
#define OPENGLRHI_DETAILED_STATS 0


#if OPENGLRHI_DETAILED_STATS
	DECLARE_CYCLE_STAT_EXTERN(TEXT("MapBuffer time"),STAT_OpenGLMapBufferTime,STATGROUP_OpenGLRHI, );
	DECLARE_CYCLE_STAT_EXTERN(TEXT("UnmapBuffer time"),STAT_OpenGLUnmapBufferTime,STATGROUP_OpenGLRHI, );
	#define SCOPE_CYCLE_COUNTER_DETAILED(Stat)	SCOPE_CYCLE_COUNTER(Stat)
	#define DETAILED_QUICK_SCOPE_CYCLE_COUNTER(x) QUICK_SCOPE_CYCLE_COUNTER(x)
#else
	#define SCOPE_CYCLE_COUNTER_DETAILED(Stat)
	#define DETAILED_QUICK_SCOPE_CYCLE_COUNTER(x)
#endif

#if UE_BUILD_TEST
#define USE_REAL_RHI_FENCES (0)
#define USE_CHEAP_ASSERTONLY_RHI_FENCES (1)
#define GLAF_CHECK(x) \
if (!(x)) \
{  \
	UE_LOG(LogRHI, Fatal, TEXT("AssertFence Fail on line %s."), TEXT(PREPROCESSOR_TO_STRING(__LINE__))); \
	FPlatformMisc::LocalPrint(TEXT("Failed a check on line:\n")); FPlatformMisc::LocalPrint(TEXT(PREPROCESSOR_TO_STRING(__LINE__))); FPlatformMisc::LocalPrint(TEXT("\n")); *((int*)3) = 13; \
}

#elif DO_CHECK
#define USE_REAL_RHI_FENCES (1)
#define USE_CHEAP_ASSERTONLY_RHI_FENCES (1)
#define GLAF_CHECK(x)  check(x)

//#define GLAF_CHECK(x) if (!(x)) { FPlatformMisc::LocalPrint(TEXT("Failed a check on line:\n")); FPlatformMisc::LocalPrint(TEXT( PREPROCESSOR_TO_STRING(__LINE__))); FPlatformMisc::LocalPrint(TEXT("\n")); *((int*)3) = 13; }

#else
#define USE_REAL_RHI_FENCES (0)
#define USE_CHEAP_ASSERTONLY_RHI_FENCES (0)

#define GLAF_CHECK(x) 

#endif

#define GLDEBUG_LABELS_ENABLED (!UE_BUILD_SHIPPING)

class FOpenGLRHIThreadResourceFence
{
	FGraphEventRef RealRHIFence;

public:

	FORCEINLINE_DEBUGGABLE void Reset()
	{
		if (IsRunningRHIInSeparateThread())
		{
			GLAF_CHECK(IsInRenderingThread());
			GLAF_CHECK(!RealRHIFence.GetReference() || RealRHIFence->IsComplete());
			RealRHIFence = nullptr;
		}
	}
	FORCEINLINE_DEBUGGABLE void SetRHIThreadFence()
	{
		if (IsRunningRHIInSeparateThread())
		{
			GLAF_CHECK(IsInRenderingThread());

			GLAF_CHECK(!RealRHIFence.GetReference() || RealRHIFence->IsComplete());
			if (IsRunningRHIInSeparateThread())
			{
				RealRHIFence = FRHICommandListExecutor::GetImmediateCommandList().RHIThreadFence(false);
			}
		}
	}
	FORCEINLINE_DEBUGGABLE void WriteAssertFence()
	{
		if (IsRunningRHIInSeparateThread())
		{
			GLAF_CHECK((IsInRenderingThread() && !IsRunningRHIInSeparateThread()) || (IsInRHIThread() && IsRunningRHIInSeparateThread()));
		}
	}
	FORCEINLINE_DEBUGGABLE void WaitFence()
	{
		if (IsRunningRHIInSeparateThread())
		{
			GLAF_CHECK(IsInRenderingThread());
			if (!IsRunningRHIInSeparateThread() && !FRHICommandListExecutor::GetImmediateCommandList().Bypass() && !GRHINeedsExtraDeletionLatency) // if we don't have an RHI thread, but we are doing parallel rendering, then we need to flush now because we are not deferring resource destruction
			{
				QUICK_SCOPE_CYCLE_COUNTER(STAT_FOpenGLRHIThreadResourceFence_Flush);
				FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::FlushRHIThread);
			}
			if (RealRHIFence.GetReference() && RealRHIFence->IsComplete())
			{
				RealRHIFence = nullptr;
			}
			else if (RealRHIFence.GetReference())
			{
				UE_LOG(LogRHI, Warning, TEXT("FOpenGLRHIThreadResourceFence waited.")); 
				QUICK_SCOPE_CYCLE_COUNTER(STAT_FOpenGLRHIThreadResourceFence_Wait);
				FRHICommandListExecutor::WaitOnRHIThreadFence(RealRHIFence);
				RealRHIFence = nullptr;
			}
		}
	}
	FORCEINLINE_DEBUGGABLE void WaitFenceRenderThreadOnly()
	{
		if (IsRunningRHIInSeparateThread())
		{
			// Do not check if running on RHI thread.
			// all rhi thread operations will be in order, check for RHIT isnt required.
			if (IsInRenderingThread())
			{
				WaitFence();
			}
		}
	}
};

class FOpenGLAssertRHIThreadFence
{
#if USE_REAL_RHI_FENCES
	FGraphEventRef RealRHIFence;
#endif
#if USE_CHEAP_ASSERTONLY_RHI_FENCES
	FThreadSafeCounter AssertFence;
#endif

public:

	FORCEINLINE_DEBUGGABLE void Reset()
	{
		if (IsRunningRHIInSeparateThread())
		{
			check(IsInRenderingThread() || IsInRHIThread());
#if USE_REAL_RHI_FENCES

			GLAF_CHECK(!RealRHIFence.GetReference() || RealRHIFence->IsComplete());
			RealRHIFence = nullptr;
#endif
#if USE_CHEAP_ASSERTONLY_RHI_FENCES
			int32 AFenceVal = AssertFence.GetValue();
			GLAF_CHECK(AFenceVal == 0 || AFenceVal == 2);
			AssertFence.Set(1);
#endif
		}
	}
	FORCEINLINE_DEBUGGABLE void SetRHIThreadFence()
	{
		if (IsRunningRHIInSeparateThread())
		{
			check(IsInRenderingThread() || IsInRHIThread());

#if USE_CHEAP_ASSERTONLY_RHI_FENCES
			int32 AFenceVal = AssertFence.GetValue();
			GLAF_CHECK(AFenceVal == 1 || AFenceVal == 2);
#endif
#if USE_REAL_RHI_FENCES
			GLAF_CHECK(!RealRHIFence.GetReference() || RealRHIFence->IsComplete());
			// Only get the fence if running on RT.
			if (IsRunningRHIInSeparateThread() && IsInRenderingThread())
			{
				RealRHIFence = FRHICommandListExecutor::GetImmediateCommandList().RHIThreadFence(false);
			}
#endif
		}
	}
	FORCEINLINE_DEBUGGABLE void WriteAssertFence()
	{
		if (IsRunningRHIInSeparateThread())
		{
			check((IsInRenderingThread() && !IsRunningRHIInSeparateThread()) || (IsInRHIThread() && IsRunningRHIInSeparateThread()));
#if USE_CHEAP_ASSERTONLY_RHI_FENCES
			int32 NewValue = AssertFence.Increment();
			GLAF_CHECK(NewValue == 2);
#endif
		}
	}
	FORCEINLINE_DEBUGGABLE void WaitFence()
	{
		if (IsRunningRHIInSeparateThread())
		{
			check(IsInRenderingThread() || IsInRHIThread());
			if (!IsRunningRHIInSeparateThread() && !FRHICommandListExecutor::GetImmediateCommandList().Bypass() && !GRHINeedsExtraDeletionLatency) // if we don't have an RHI thread, but we are doing parallel rendering, then we need to flush now because we are not deferring resource destruction
			{
				FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::FlushRHIThread);
			}
#if USE_CHEAP_ASSERTONLY_RHI_FENCES
			GLAF_CHECK(AssertFence.GetValue() == 0 || AssertFence.GetValue() == 2);
#endif
#if USE_REAL_RHI_FENCES
			GLAF_CHECK(!RealRHIFence.GetReference() || RealRHIFence->IsComplete());
			if (RealRHIFence.GetReference())
			{
				FRHICommandListExecutor::WaitOnRHIThreadFence(RealRHIFence);
				RealRHIFence = nullptr;
			}
#endif
		}
	}

	FORCEINLINE_DEBUGGABLE void WaitFenceRenderThreadOnly()
	{
		if (IsRunningRHIInSeparateThread())
		{
			// Do not check if running on RHI thread.
			// all rhi thread operations will be in order, check for RHIT isnt required.
			if (IsInRenderingThread())
			{
				WaitFence();
			}
		}
	}
};


//////////////////////////////////////////////////////////////////////////
// Proxy object that fulfils immediate requirements of RHIResource creation whilst allowing deferment of GL resource creation on to the RHI thread.

template<typename TRHIType, typename TOGLResourceType>
class TOpenGLResourceProxy : public TRHIType
{
public:
	TOpenGLResourceProxy(TFunction<TOGLResourceType*(TRHIType*)> CreateFunc)
		: GLResourceObject(nullptr)
	{
		check((bool)CreateFunc);
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
		if (ShouldRunGLRenderContextOpOnThisThread(RHICmdList))
		{
			GLResourceObject = CreateFunc(this);
			GLResourceObject->AddRef();
			bQueuedCreation = false;
		}
		else
		{
			CreationFence.Reset();
			ALLOC_COMMAND_CL(RHICmdList, FRHICommandGLCommand)([this, CreateFunc = MoveTemp(CreateFunc)]()
			{
				GLResourceObject = CreateFunc(this);
				GLResourceObject->AddRef();
				CreationFence.WriteAssertFence();
			});
			CreationFence.SetRHIThreadFence();
			bQueuedCreation = true;
		}
	}

	virtual ~TOpenGLResourceProxy()
	{
		// Wait for any queued creation calls.
		WaitIfQueued();

		check(GLResourceObject);

		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
		if (ShouldRunGLRenderContextOpOnThisThread(RHICmdList))
		{
			GLResourceObject->Release();
		}
		else
		{
			RunOnGLRenderContextThread([GLResourceObject = GLResourceObject]()
			{
				GLResourceObject->Release();
			});
			GLResourceObject = nullptr;
		}
	}

	TOpenGLResourceProxy(const TOpenGLResourceProxy&) = delete;
	TOpenGLResourceProxy& operator = (const TOpenGLResourceProxy&) = delete;

	TOGLResourceType* GetGLResourceObject()
	{
		CreationFence.WaitFenceRenderThreadOnly();
		return GLResourceObject;
	}

	FORCEINLINE TOGLResourceType* GetGLResourceObject_OnRHIThread()
	{
		check(IsInRHIThread());
		return GLResourceObject;
	}

	typedef TOGLResourceType ContainedGLType;
private:
	void WaitIfQueued()
	{
		if (bQueuedCreation)
		{
			CreationFence.WaitFence();
		}
	}

	//FOpenGLRHIThreadResourceFence CreationFence;
	FOpenGLAssertRHIThreadFence CreationFence;
	TRefCountPtr<TOGLResourceType> GLResourceObject;
	bool bQueuedCreation;
};

typedef TOpenGLResourceProxy<FRHIVertexShader, FOpenGLVertexShader> FOpenGLVertexShaderProxy;
typedef TOpenGLResourceProxy<FRHIPixelShader, FOpenGLPixelShader> FOpenGLPixelShaderProxy;
typedef TOpenGLResourceProxy<FRHIGeometryShader, FOpenGLGeometryShader> FOpenGLGeometryShaderProxy;
typedef TOpenGLResourceProxy<FRHIHullShader, FOpenGLHullShader> FOpenGLHullShaderProxy;
typedef TOpenGLResourceProxy<FRHIDomainShader, FOpenGLDomainShader> FOpenGLDomainShaderProxy;
typedef TOpenGLResourceProxy<FRHIComputeShader, FOpenGLComputeShader> FOpenGLComputeShaderProxy;


template <typename T>
struct TIsGLProxyObject
{
	enum { Value = false };
};

template<typename TRHIType, typename TOGLResourceType>
struct TIsGLProxyObject<TOpenGLResourceProxy<TRHIType, TOGLResourceType>>
{
	enum { Value = true };
};

typedef void (*BufferBindFunction)( GLuint Buffer );

template<typename BaseType>
class TOpenGLTexture;

template <typename BaseType, GLenum Type, BufferBindFunction BufBind>
class TOpenGLBuffer : public BaseType
{
	void LoadData( uint32 InOffset, uint32 InSize, const void* InData)
	{
		VERIFY_GL_SCOPE();
		const uint8* Data = (const uint8*)InData;
		const uint32 BlockSize = OpenGLConsoleVariables::MaxSubDataSize;

		if (BlockSize > 0)
		{
			while ( InSize > 0)
			{
				const uint32 BufferSize = FMath::Min<uint32>( BlockSize, InSize);

				FOpenGL::BufferSubData( Type, InOffset, BufferSize, Data);

				InOffset += BufferSize;
				InSize -= BufferSize;
				Data += BufferSize;
			}
		}
		else
		{
			FOpenGL::BufferSubData( Type, InOffset, InSize, InData);
		}
	}

	GLenum GetAccess()
	{
		// Previously there was special-case logic to always use GL_STATIC_DRAW for vertex buffers allocated from staging buffer.
		// However it seems to be incorrect as NVidia drivers complain (via debug output callback) about VIDEO->HOST copying for buffers with such hints
		return bStreamDraw ? GL_STREAM_DRAW : (IsDynamic() ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
	}
public:

	GLuint Resource;

	/** Needed on OS X to force a rebind of the texture buffer to the texture name to workaround radr://18379338 */
	uint64 ModificationCount;

	TOpenGLBuffer()
		: Resource(0)
		, ModificationCount(0)
		, bIsLocked(false)
		, bIsLockReadOnly(false)
		, bStreamDraw(false)
		, bLockBufferWasAllocated(false)
		, LockSize(0)
		, LockOffset(0)
		, LockBuffer(NULL)
		, RealSize(0)
	{ }

	TOpenGLBuffer(uint32 InStride,uint32 InSize,uint32 InUsage,
		const void *InData = NULL, bool bStreamedDraw = false, GLuint ResourceToUse = 0, uint32 ResourceSize = 0)
	: BaseType(InStride,InSize,InUsage)
	, Resource(0)
	, ModificationCount(0)
	, bIsLocked(false)
	, bIsLockReadOnly(false)
	, bStreamDraw(bStreamedDraw)
	, bLockBufferWasAllocated(false)
	, LockSize(0)
	, LockOffset(0)
	, LockBuffer(NULL)
	, RealSize(InSize)
	{

		RealSize = ResourceSize ? ResourceSize : InSize;

		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

		if (ShouldRunGLRenderContextOpOnThisThread(RHICmdList))
		{
			CreateGLBuffer(InData, ResourceToUse, ResourceSize);
		}
		else
		{
			void* BuffData = nullptr;
			if (InData)
			{
				BuffData = RHICmdList.Alloc(RealSize, 16);
				FMemory::Memcpy(BuffData, InData, RealSize);
			}
			TransitionFence.Reset();
			ALLOC_COMMAND_CL(RHICmdList, FRHICommandGLCommand)([=]() 
			{
				CreateGLBuffer(BuffData, ResourceToUse, ResourceSize); 
				TransitionFence.WriteAssertFence();
			});
			TransitionFence.SetRHIThreadFence();

		}
	}

	void CreateGLBuffer(const void *InData, const GLuint ResourceToUse, const uint32 ResourceSize)
	{
			VERIFY_GL_SCOPE();
		uint32 InSize = BaseType::GetSize();
			RealSize = ResourceSize ? ResourceSize : InSize;
			if( ResourceToUse )
			{
				Resource = ResourceToUse;
				check( Type != GL_UNIFORM_BUFFER || !IsUniformBufferBound(Resource) );
				Bind();
				FOpenGL::BufferSubData(Type, 0, InSize, InData);
			}
			else
			{
				if (BaseType::GLSupportsType())
				{
					FOpenGL::GenBuffers(1, &Resource);
					check( Type != GL_UNIFORM_BUFFER || !IsUniformBufferBound(Resource) );
					Bind();
#if !RESTRICT_SUBDATA_SIZE
					if( InData == NULL || RealSize <= InSize )
					{
						glBufferData(Type, RealSize, InData, GetAccess());
					}
					else
					{
						glBufferData(Type, RealSize, NULL, GetAccess());
						FOpenGL::BufferSubData(Type, 0, InSize, InData);
					}
#else
					glBufferData(Type, RealSize, NULL, GetAccess());
					if ( InData != NULL )
					{
						LoadData( 0, FMath::Min<uint32>(InSize,RealSize), InData);
					}
#endif
					IncrementBufferMemory(Type, BaseType::IsStructuredBuffer(), RealSize);
				}
				else
				{
					BaseType::CreateType(Resource, InData, InSize);
				}
			}
		}

	virtual ~TOpenGLBuffer()
	{
		// this is a bit of a special case, normally the RT destroys all rhi resources...but this isn't an rhi resource
		TransitionFence.WaitFenceRenderThreadOnly();

		if (Resource != 0)
		{
			auto DeleteGLResources = [Resource=Resource, RealSize= RealSize, bStreamDraw= (bool)bStreamDraw, LockBuffer = LockBuffer, bLockBufferWasAllocated=bLockBufferWasAllocated]()
			{
				VERIFY_GL_SCOPE();
				if (BaseType::OnDelete(Resource, RealSize, bStreamDraw, 0))
				{
					FOpenGL::DeleteBuffers(1, &Resource);
				}
				if (LockBuffer != NULL)
				{
					if (bLockBufferWasAllocated)
					{
						FMemory::Free(LockBuffer);
					}
					else
					{
						UE_LOG(LogRHI,Warning,TEXT("Destroying TOpenGLBuffer without returning memory to the driver; possibly called RHIMapStagingSurface() but didn't call RHIUnmapStagingSurface()? Resource %u"), Resource);
					}
				}
			};

			RunOnGLRenderContextThread(MoveTemp(DeleteGLResources));
			LockBuffer = nullptr;
			DecrementBufferMemory(Type, BaseType::IsStructuredBuffer(), RealSize);

			ReleaseCachedBuffer();
		}

	}

	void Bind()
	{
		VERIFY_GL_SCOPE();
		BufBind(Resource);
	}

	uint8 *Lock(uint32 InOffset, uint32 InSize, bool bReadOnly, bool bDiscard)
	{
		//SCOPE_CYCLE_COUNTER_DETAILED(STAT_OpenGLMapBufferTime);
		check(InOffset + InSize <= this->GetSize());
		//check( LockBuffer == NULL );	// Only one outstanding lock is allowed at a time!
		check( !bIsLocked );	// Only one outstanding lock is allowed at a time!
		VERIFY_GL_SCOPE();

		Bind();

		bIsLocked = true;
		bIsLockReadOnly = bReadOnly;
		uint8 *Data = NULL;

		// Discard if the input size is the same as the backing store size, regardless of the input argument, as orphaning the backing store will typically be faster.
		bDiscard = (bDiscard || (!bReadOnly && InSize == RealSize)) && FOpenGL::DiscardFrameBufferToResize();

		// Map buffer is faster in some circumstances and slower in others, decide when to use it carefully.
		bool const bUseMapBuffer = BaseType::GLSupportsType() && (bReadOnly || OpenGLConsoleVariables::bUseMapBuffer);

		// If we're able to discard the current data, do so right away
		// If we can then we should orphan the buffer name & reallocate the backing store only once as calls to glBufferData may do so even when the size is the same.
		uint32 DiscardSize = (bDiscard && !bUseMapBuffer && InSize == RealSize && !RESTRICT_SUBDATA_SIZE) ? 0 : RealSize;

		// Don't call BufferData if Bindless is on, as bindless texture buffers make buffers immutable
		if ( bDiscard && !OpenGLConsoleVariables::bBindlessTexture && OpenGLConsoleVariables::bUseBufferDiscard)
		{
			if (BaseType::GLSupportsType())
			{
				// @todo Lumin hack:
				// When not hinted with GL_STATIC_DRAW, glBufferData() would introduce long uploading times
				// that would show up in TGD. Without the workaround of hinting glBufferData() with the static buffer usage, 
				// the buffer mapping / unmapping has an unexpected cost(~5 - 10ms) that manifests itself in light grid computation 
				// and vertex buffer mapping for bone matrices. We believe this issue originates from the driver as the OpenGL spec 
				// specifies the following on the usage hint parameter of glBufferData() :
				//
				// > usage is a hint to the GL implementation as to how a buffer object's data store will be accessed. 
				// > This enables the GL implementation to make more intelligent decisions that may significantly impact buffer object performance. 
				// > It does not, however, constrain the actual usage of the data store.
				//
				// As the alternative approach of using uniform buffers for bone matrix uploading (isntead of buffer mapping/unmapping)
				// limits the number of bone matrices to 75 in the current engine architecture and that is not desirable, 
				// we can stick with the STATIC_DRAW hint workaround for glBufferData().
				//
				// We haven't seen the buffer mapping/unmapping issue show up elsewhere in the pipeline in our test scenes. 
				// However, depending on the UE4 features that are used, this issue might pop up elsewhere that we're yet to see.
				// As there are concerns for maximum number of bone matrices, going for the GL_STATIC_DRAW hint should be safer, 
				// given the fact that it won't constrain the actual usage of the data store as per the OpenGL4 spec.
#if PLATFORM_LUMINGL4
				glBufferData(Type, DiscardSize, NULL, GL_STATIC_DRAW);
#else
				glBufferData(Type, DiscardSize, NULL, GetAccess());
#endif			
			}
		}

		if ( bUseMapBuffer)
		{
			FOpenGL::EResourceLockMode LockMode = bReadOnly ? FOpenGL::EResourceLockMode::RLM_ReadOnly : FOpenGL::EResourceLockMode::RLM_WriteOnly;
			Data = static_cast<uint8*>( FOpenGL::MapBufferRange( Type, InOffset, InSize, LockMode ) );
//			checkf(Data != NULL, TEXT("FOpenGL::MapBufferRange Failed, glError %d (0x%x)"), glGetError(), glGetError());

			LockOffset = InOffset;
			LockSize = InSize;
			LockBuffer = Data;
			bLockBufferWasAllocated = false;
		}
		else
		{
			// Allocate a temp buffer to write into
			LockOffset = InOffset;
			LockSize = InSize;
			if (CachedBuffer && InSize <= CachedBufferSize)
			{
				LockBuffer = CachedBuffer;
				CachedBuffer = nullptr;
				// Keep CachedBufferSize to keep the actual size allocated.
			}
			else
			{
				ReleaseCachedBuffer();
				LockBuffer = FMemory::Malloc( InSize );
				CachedBufferSize = InSize; // Safegard
			}
			Data = static_cast<uint8*>( LockBuffer );
			bLockBufferWasAllocated = true;
		}

		check(Data != NULL);
		return Data;
	}

	uint8 *LockWriteOnlyUnsynchronized(uint32 InOffset, uint32 InSize, bool bDiscard)
	{
		//SCOPE_CYCLE_COUNTER_DETAILED(STAT_OpenGLMapBufferTime);
		check(InOffset + InSize <= this->GetSize());
		//check( LockBuffer == NULL );	// Only one outstanding lock is allowed at a time!
		check( !bIsLocked );	// Only one outstanding lock is allowed at a time!
		VERIFY_GL_SCOPE();

		Bind();

		bIsLocked = true;
		bIsLockReadOnly = false;
		uint8 *Data = NULL;

		// Discard if the input size is the same as the backing store size, regardless of the input argument, as orphaning the backing store will typically be faster.
		bDiscard = (bDiscard || InSize == RealSize) && FOpenGL::DiscardFrameBufferToResize();

		// Map buffer is faster in some circumstances and slower in others, decide when to use it carefully.
		bool const bUseMapBuffer = BaseType::GLSupportsType() && OpenGLConsoleVariables::bUseMapBuffer;

		// If we're able to discard the current data, do so right away
		// If we can then we should orphan the buffer name & reallocate the backing store only once as calls to glBufferData may do so even when the size is the same.
		uint32 DiscardSize = (bDiscard && !bUseMapBuffer && InSize == RealSize && !RESTRICT_SUBDATA_SIZE) ? 0 : RealSize;

		// Don't call BufferData if Bindless is on, as bindless texture buffers make buffers immutable
		if ( bDiscard && !OpenGLConsoleVariables::bBindlessTexture && OpenGLConsoleVariables::bUseBufferDiscard)
		{
			if (BaseType::GLSupportsType())
			{
				glBufferData( Type, DiscardSize, NULL, GetAccess());
			}
		}

		if ( bUseMapBuffer)
		{
			FOpenGL::EResourceLockMode LockMode = bDiscard ? FOpenGL::EResourceLockMode::RLM_WriteOnly : FOpenGL::EResourceLockMode::RLM_WriteOnlyUnsynchronized;
			Data = static_cast<uint8*>( FOpenGL::MapBufferRange( Type, InOffset, InSize, LockMode ) );
			LockOffset = InOffset;
			LockSize = InSize;
			LockBuffer = Data;
			bLockBufferWasAllocated = false;
		}
		else
		{
			// Allocate a temp buffer to write into
			LockOffset = InOffset;
			LockSize = InSize;
			if (CachedBuffer && InSize <= CachedBufferSize)
			{
				LockBuffer = CachedBuffer;
				CachedBuffer = nullptr;
				// Keep CachedBufferSize to keep the actual size allocated.
			}
			else
			{
				ReleaseCachedBuffer();
				LockBuffer = FMemory::Malloc( InSize );
				CachedBufferSize = InSize; // Safegard
			}
			Data = static_cast<uint8*>( LockBuffer );
			bLockBufferWasAllocated = true;
		}

		check(Data != NULL);
		return Data;
	}

	void Unlock()
	{
		//SCOPE_CYCLE_COUNTER_DETAILED(STAT_OpenGLUnmapBufferTime);
		VERIFY_GL_SCOPE();
		if (bIsLocked)
		{
			Bind();

			if (BaseType::GLSupportsType() && (OpenGLConsoleVariables::bUseMapBuffer || bIsLockReadOnly))
			{
				check(!bLockBufferWasAllocated);
				if (Type == GL_ARRAY_BUFFER || Type == GL_ELEMENT_ARRAY_BUFFER)
				{
					FOpenGL::UnmapBufferRange(Type, LockOffset, LockSize);
				}
				else
				{
					FOpenGL::UnmapBuffer(Type);
				}
				LockBuffer = NULL;
			}
			else
			{
				if (BaseType::GLSupportsType())
				{
#if !RESTRICT_SUBDATA_SIZE
					// Check for the typical, optimized case
					if( LockSize == RealSize )
					{
						if (FOpenGL::DiscardFrameBufferToResize())
						{
							glBufferData(Type, RealSize, LockBuffer, GetAccess());
						}
						else
						{
							FOpenGL::BufferSubData(Type, 0, LockSize, LockBuffer);
						}
						check( LockBuffer != NULL );
					}
					else
					{
						// Only updating a subset of the data
						FOpenGL::BufferSubData(Type, LockOffset, LockSize, LockBuffer);
						check( LockBuffer != NULL );
					}
#else
					LoadData( LockOffset, LockSize, LockBuffer);
					check( LockBuffer != NULL);
#endif
				}
				check(bLockBufferWasAllocated);

				if ((this->GetUsage() & BUF_Volatile) != 0)
				{
					ReleaseCachedBuffer(); // Safegard

					CachedBuffer = LockBuffer;
					// Possibly > LockSize when reusing cached allocation.
					CachedBufferSize = FMath::Max<GLuint>(CachedBufferSize, LockSize);
				}
				else
				{
					FMemory::Free(LockBuffer);
				}
				LockBuffer = NULL;
				bLockBufferWasAllocated = false;
				LockSize = 0;
			}
			ModificationCount += (bIsLockReadOnly ? 0 : 1);
			bIsLocked = false;
		}
	}

	void Update(void *InData, uint32 InOffset, uint32 InSize, bool bDiscard)
	{
		check(InOffset + InSize <= this->GetSize());
		VERIFY_GL_SCOPE();
		Bind();
#if !RESTRICT_SUBDATA_SIZE
		FOpenGL::BufferSubData(Type, InOffset, InSize, InData);
#else
		LoadData( InOffset, InSize, InData);
#endif
		ModificationCount++;
	}

	bool IsDynamic() const { return (this->GetUsage() & BUF_AnyDynamic) != 0; }
	bool IsLocked() const { return bIsLocked; }
	bool IsLockReadOnly() const { return bIsLockReadOnly; }
	void* GetLockedBuffer() const { return LockBuffer; }

	void ReleaseCachedBuffer()
	{
		if (CachedBuffer)
		{
			FMemory::Free(CachedBuffer);
			CachedBuffer = nullptr;
			CachedBufferSize = 0;
		}
		// Don't reset CachedBufferSize if !CachedBuffer since it could be the locked buffer allocation size.
	}

	void Swap(TOpenGLBuffer& Other)
	{
		BaseType::Swap(Other);
		::Swap(Resource, Other.Resource);
		::Swap(RealSize, Other.RealSize);
	}

private:

	uint32 bIsLocked : 1;
	uint32 bIsLockReadOnly : 1;
	uint32 bStreamDraw : 1;
	uint32 bLockBufferWasAllocated : 1;

	GLuint LockSize;
	GLuint LockOffset;
	void* LockBuffer;

	// A cached allocation that can be reused. The same allocation can never be in CachedBuffer and LockBuffer at the same time.
	void* CachedBuffer = nullptr;
	// The size of the cached buffer allocation. Can be non zero even though CachedBuffer is  null, to preserve the allocation size.
	GLuint CachedBufferSize = 0;

	uint32 RealSize;	// sometimes (for example, for uniform buffer pool) we allocate more in OpenGL than is requested of us.

	FOpenGLAssertRHIThreadFence TransitionFence;
};

class FOpenGLBasePixelBuffer : public FRefCountedObject
{
public:
	FOpenGLBasePixelBuffer(uint32 InStride,uint32 InSize,uint32 InUsage)
	: Size(InSize)
	, Usage(InUsage)
	{}
	static bool OnDelete(GLuint Resource,uint32 Size,bool bStreamDraw,uint32 Offset)
	{
		OnPixelBufferDeletion(Resource);
		return true;
	}
	uint32 GetSize() const { return Size; }
	uint32 GetUsage() const { return Usage; }

	static FORCEINLINE bool GLSupportsType()
	{
		return true;
	}

	static void CreateType(GLuint& Resource, const void* InData, uint32 InSize)
	{
		// @todo-mobile
	}

	static bool IsStructuredBuffer() { return false; }

private:
	uint32 Size;
	uint32 Usage;
};

class FOpenGLBaseVertexBuffer : public FRHIVertexBuffer
{
public:
	FOpenGLBaseVertexBuffer()
	{}

	FOpenGLBaseVertexBuffer(uint32 InStride,uint32 InSize,uint32 InUsage): FRHIVertexBuffer(InSize,InUsage)
	{
#if ENABLE_LOW_LEVEL_MEM_TRACKER
		LLM_SCOPED_PAUSE_TRACKING_WITH_ENUM_AND_AMOUNT(ELLMTag::GraphicsPlatform, InSize, ELLMTracker::Platform, ELLMAllocType::None);
		LLM_SCOPED_PAUSE_TRACKING_WITH_ENUM_AND_AMOUNT(ELLMTag::Meshes, InSize, ELLMTracker::Default, ELLMAllocType::None);
#endif
	}

	~FOpenGLBaseVertexBuffer( void )
	{
#if ENABLE_LOW_LEVEL_MEM_TRACKER
		LLM_SCOPED_PAUSE_TRACKING_WITH_ENUM_AND_AMOUNT(ELLMTag::GraphicsPlatform, -(int64)GetSize(), ELLMTracker::Platform, ELLMAllocType::None);
		LLM_SCOPED_PAUSE_TRACKING_WITH_ENUM_AND_AMOUNT(ELLMTag::Meshes, -(int64)GetSize(), ELLMTracker::Default, ELLMAllocType::None);
#endif
	}

	static bool OnDelete(GLuint Resource,uint32 Size,bool bStreamDraw,uint32 Offset)
	{
		OnVertexBufferDeletion(Resource);
		return true;
	}

	static FORCEINLINE bool GLSupportsType()
	{
		return true;
	}

	static void CreateType(GLuint& Resource, const void* InData, uint32 InSize)
	{
		// @todo-mobile
	}

	static bool IsStructuredBuffer() { return false; }
};

struct FOpenGLEUniformBufferData : public FRefCountedObject
{
	FOpenGLEUniformBufferData(uint32 SizeInBytes)
	{
		uint32 SizeInUint32s = (SizeInBytes + 3) / 4;
		Data.Empty(SizeInUint32s);
		Data.AddUninitialized(SizeInUint32s);
		IncrementBufferMemory(GL_UNIFORM_BUFFER,false,Data.GetAllocatedSize());
	}

	~FOpenGLEUniformBufferData()
	{
		DecrementBufferMemory(GL_UNIFORM_BUFFER,false,Data.GetAllocatedSize());
	}

	TArray<uint32> Data;
};
typedef TRefCountPtr<FOpenGLEUniformBufferData> FOpenGLEUniformBufferDataRef;

class FOpenGLUniformBuffer : public FRHIUniformBuffer
{
public:
	/** The GL resource for this uniform buffer. */
	GLuint Resource;

	/** The offset of the uniform buffer's contents in the resource. */
	uint32 Offset;

	/** When using a persistently mapped buffer this is a pointer to the CPU accessible data. */
	uint8* PersistentlyMappedBuffer;

	/** Unique ID for state shadowing purposes. */
	uint32 UniqueID;

	/** Resource table containing RHI references. */
	TArray<TRefCountPtr<FRHIResource> > ResourceTable;

	/** Emulated uniform data for ES2. */
	FOpenGLEUniformBufferDataRef EmulatedBufferData;

	/** The size of the buffer allocated to hold the uniform buffer contents. May be larger than necessary. */
	uint32 AllocatedSize;

	/** True if the uniform buffer is not used across frames. */
	bool bStreamDraw;

	/** Initialization constructor. */
	FOpenGLUniformBuffer(const FRHIUniformBufferLayout& InLayout);

	void SetGLUniformBufferParams(GLuint InResource, uint32 InOffset, uint8* InPersistentlyMappedBuffer, uint32 InAllocatedSize, FOpenGLEUniformBufferDataRef InEmulatedBuffer, bool bInStreamDraw);

	/** Destructor. */
	~FOpenGLUniformBuffer();

	FOpenGLAssertRHIThreadFence AccessFence;
	FOpenGLAssertRHIThreadFence CopyFence;
};


class FOpenGLBaseIndexBuffer : public FRHIIndexBuffer
{
public:
	FOpenGLBaseIndexBuffer()
	{}

	FOpenGLBaseIndexBuffer(uint32 InStride,uint32 InSize,uint32 InUsage): FRHIIndexBuffer(InStride,InSize,InUsage)
	{
#if ENABLE_LOW_LEVEL_MEM_TRACKER
		LLM_SCOPED_PAUSE_TRACKING_WITH_ENUM_AND_AMOUNT(ELLMTag::GraphicsPlatform, InSize, ELLMTracker::Platform, ELLMAllocType::None);
		LLM_SCOPED_PAUSE_TRACKING_WITH_ENUM_AND_AMOUNT(ELLMTag::Meshes, InSize, ELLMTracker::Default, ELLMAllocType::None);
#endif
	}

	~FOpenGLBaseIndexBuffer(void)
	{
#if ENABLE_LOW_LEVEL_MEM_TRACKER
		LLM_SCOPED_PAUSE_TRACKING_WITH_ENUM_AND_AMOUNT(ELLMTag::GraphicsPlatform, -(int64)GetSize(), ELLMTracker::Platform, ELLMAllocType::None);
		LLM_SCOPED_PAUSE_TRACKING_WITH_ENUM_AND_AMOUNT(ELLMTag::Meshes, -(int64)GetSize(), ELLMTracker::Default, ELLMAllocType::None);
#endif
	}

	static bool OnDelete(GLuint Resource,uint32 Size,bool bStreamDraw,uint32 Offset)
	{
		OnIndexBufferDeletion(Resource);
		return true;
	}

	static FORCEINLINE bool GLSupportsType()
	{
		return true;
	}

	static void CreateType(GLuint& Resource, const void* InData, uint32 InSize)
	{
		// @todo-mobile
	}

	static bool IsStructuredBuffer() { return false; }
};

class FOpenGLBaseStructuredBuffer : public FRHIStructuredBuffer
{
public:
	FOpenGLBaseStructuredBuffer(uint32 InStride,uint32 InSize,uint32 InUsage): FRHIStructuredBuffer(InStride,InSize,InUsage) {}
	static bool OnDelete(GLuint Resource,uint32 Size,bool bStreamDraw,uint32 Offset)
	{
		OnVertexBufferDeletion(Resource);
		return true;
	}

	static FORCEINLINE bool GLSupportsType()
	{
		return FOpenGL::SupportsStructuredBuffers();
	}

	static void CreateType(GLuint& Resource, const void* InData, uint32 InSize)
	{
		// @todo-mobile
	}

	static bool IsStructuredBuffer() { return true; }
};

typedef TOpenGLBuffer<FOpenGLBasePixelBuffer, GL_PIXEL_UNPACK_BUFFER, CachedBindPixelUnpackBuffer> FOpenGLPixelBuffer;
typedef TOpenGLBuffer<FOpenGLBaseVertexBuffer, GL_ARRAY_BUFFER, CachedBindArrayBuffer> FOpenGLVertexBuffer;
typedef TOpenGLBuffer<FOpenGLBaseIndexBuffer,GL_ELEMENT_ARRAY_BUFFER,CachedBindElementArrayBuffer> FOpenGLIndexBuffer;
typedef TOpenGLBuffer<FOpenGLBaseStructuredBuffer,GL_ARRAY_BUFFER,CachedBindArrayBuffer> FOpenGLStructuredBuffer;

#define MAX_STREAMED_BUFFERS_IN_ARRAY 2	// must be > 1!
#define MIN_DRAWS_IN_SINGLE_BUFFER 16

template <typename BaseType, uint32 Stride>
class TOpenGLStreamedBufferArray
{
public:

	TOpenGLStreamedBufferArray( void ) {}
	virtual ~TOpenGLStreamedBufferArray( void ) {}

	void Init( uint32 InitialBufferSize )
	{
		for( int32 BufferIndex = 0; BufferIndex < MAX_STREAMED_BUFFERS_IN_ARRAY; ++BufferIndex )
		{
			Buffer[BufferIndex] = new BaseType(Stride, InitialBufferSize, BUF_Volatile, NULL, true);
		}
		CurrentBufferIndex = 0;
		CurrentOffset = 0;
		LastOffset = 0;
		MinNeededBufferSize = InitialBufferSize / MIN_DRAWS_IN_SINGLE_BUFFER;
	}

	void Cleanup( void )
	{
		for( int32 BufferIndex = 0; BufferIndex < MAX_STREAMED_BUFFERS_IN_ARRAY; ++BufferIndex )
		{
			Buffer[BufferIndex].SafeRelease();
		}
	}

	uint8* Lock( uint32 DataSize )
	{
		check(!Buffer[CurrentBufferIndex]->IsLocked());
		DataSize = Align(DataSize, (1<<8));	// to keep the speed up, let's start data for each next draw at 256-byte aligned offset

		// Keep our dynamic buffers at least MIN_DRAWS_IN_SINGLE_BUFFER times bigger than max single request size
		uint32 NeededBufSize = Align( MIN_DRAWS_IN_SINGLE_BUFFER * DataSize, (1 << 20) );	// 1MB increments
		if (NeededBufSize > MinNeededBufferSize)
		{
			MinNeededBufferSize = NeededBufSize;
		}

		// Check if we need to switch buffer, as the current draw data won't fit in current one
		bool bDiscard = false;
		if (Buffer[CurrentBufferIndex]->GetSize() < CurrentOffset + DataSize)
		{
			// We do.
			++CurrentBufferIndex;
			if (CurrentBufferIndex == MAX_STREAMED_BUFFERS_IN_ARRAY)
			{
				CurrentBufferIndex = 0;
			}
			CurrentOffset = 0;

			// Check if we should extend the next buffer, as max request size has changed
			if (MinNeededBufferSize > Buffer[CurrentBufferIndex]->GetSize())
			{
				Buffer[CurrentBufferIndex].SafeRelease();
				Buffer[CurrentBufferIndex] = new BaseType(Stride, MinNeededBufferSize, BUF_Volatile);
			}

			bDiscard = true;
		}

		LastOffset = CurrentOffset;
		CurrentOffset += DataSize;

		return Buffer[CurrentBufferIndex]->LockWriteOnlyUnsynchronized(LastOffset, DataSize, bDiscard);
	}

	void Unlock( void )
	{
		check(Buffer[CurrentBufferIndex]->IsLocked());
		Buffer[CurrentBufferIndex]->Unlock();
	}

	BaseType* GetPendingBuffer( void ) { return Buffer[CurrentBufferIndex]; }
	uint32 GetPendingOffset( void ) { return LastOffset; }

private:
	TRefCountPtr<BaseType> Buffer[MAX_STREAMED_BUFFERS_IN_ARRAY];
	uint32 CurrentBufferIndex;
	uint32 CurrentOffset;
	uint32 LastOffset;
	uint32 MinNeededBufferSize;
};

typedef TOpenGLStreamedBufferArray<FOpenGLVertexBuffer,0> FOpenGLStreamedVertexBufferArray;
typedef TOpenGLStreamedBufferArray<FOpenGLIndexBuffer,sizeof(uint16)> FOpenGLStreamedIndexBufferArray;

struct FOpenGLVertexElement
{
	GLenum Type;
	GLuint StreamIndex;
	GLuint Offset;
	GLuint Size;
	GLuint Divisor;
	GLuint HashStride;
	uint8 bNormalized;
	uint8 AttributeIndex;
	uint8 bShouldConvertToFloat;
	uint8 Padding;

	FOpenGLVertexElement()
		: Padding(0)
	{
	}
};

/** Convenience typedef: preallocated array of OpenGL input element descriptions. */
typedef TArray<FOpenGLVertexElement,TFixedAllocator<MaxVertexElementCount> > FOpenGLVertexElements;

/** This represents a vertex declaration that hasn't been combined with a specific shader to create a bound shader. */
class FOpenGLVertexDeclaration : public FRHIVertexDeclaration
{
public:
	/** Elements of the vertex declaration. */
	FOpenGLVertexElements VertexElements;

	uint16 StreamStrides[MaxVertexElementCount];

	/** Initialization constructor. */
	FOpenGLVertexDeclaration(const FOpenGLVertexElements& InElements, const uint16* InStrides)
		: VertexElements(InElements)
	{
		FMemory::Memcpy(StreamStrides, InStrides, sizeof(StreamStrides));
	}
	
	virtual bool GetInitializer(FVertexDeclarationElementList& Init) override final;
};


/**
 * Combined shader state and vertex definition for rendering geometry.
 * Each unique instance consists of a vertex decl, vertex shader, and pixel shader.
 */
class FOpenGLBoundShaderState : public FRHIBoundShaderState
{
public:

	FCachedBoundShaderStateLink CacheLink;

	uint16 StreamStrides[MaxVertexElementCount];

	FOpenGLLinkedProgram* LinkedProgram;
	TRefCountPtr<FOpenGLVertexDeclaration> VertexDeclaration;
	TRefCountPtr<FOpenGLVertexShaderProxy> VertexShaderProxy;
	TRefCountPtr<FOpenGLPixelShaderProxy> PixelShaderProxy;
	TRefCountPtr<FOpenGLGeometryShaderProxy> GeometryShaderProxy;
	TRefCountPtr<FOpenGLHullShaderProxy> HullShaderProxy;
	TRefCountPtr<FOpenGLDomainShaderProxy> DomainShaderProxy;

	/** Initialization constructor. */
	FOpenGLBoundShaderState(
		FOpenGLLinkedProgram* InLinkedProgram,
		FRHIVertexDeclaration* InVertexDeclarationRHI,
		FRHIVertexShader* InVertexShaderRHI,
		FRHIPixelShader* InPixelShaderRHI,
		FRHIGeometryShader* InGeometryShaderRHI,
		FRHIHullShader* InHullShaderRHI,
		FRHIDomainShader* InDomainShaderRHI
		);

	const TBitArray<>& GetTextureNeeds(int32& OutMaxTextureStageUsed);
	const TBitArray<>& GetUAVNeeds(int32& OutMaxUAVUnitUsed) const;
	void GetNumUniformBuffers(int32 NumVertexUniformBuffers[SF_Compute]);

	bool NeedsTextureStage(int32 TextureStageIndex);
	int32 MaxTextureStageUsed();
	bool RequiresDriverInstantiation();

	FOpenGLVertexShader* GetVertexShader()
	{
		check(IsValidRef(VertexShaderProxy));
		return VertexShaderProxy->GetGLResourceObject();
	}

	FOpenGLPixelShader* GetPixelShader()
	{
		check(IsValidRef(PixelShaderProxy));
		return PixelShaderProxy->GetGLResourceObject();
	}

	FOpenGLGeometryShader* GetGeometryShader()	{ return GeometryShaderProxy ? GeometryShaderProxy->GetGLResourceObject() : nullptr;}
	FOpenGLHullShader* GetHullShader()	{ return HullShaderProxy ? HullShaderProxy->GetGLResourceObject() : nullptr; }
	FOpenGLDomainShader* GetDomainShader()	{ return DomainShaderProxy ? DomainShaderProxy->GetGLResourceObject() : nullptr;}

	virtual ~FOpenGLBoundShaderState();
};


inline GLenum GetOpenGLTargetFromRHITexture(FRHITexture* Texture)
{
	if(!Texture)
	{
		return GL_NONE;
	}
	else if(Texture->GetTexture2D())
	{
		return GL_TEXTURE_2D;
	}
	else if(Texture->GetTexture2DArray())
	{
		return GL_TEXTURE_2D_ARRAY;
	}
	else if(Texture->GetTexture3D())
	{
		return GL_TEXTURE_3D;
	}
	else if(Texture->GetTextureCube())
	{
		return GL_TEXTURE_CUBE_MAP;
	}
	else
	{
		UE_LOG(LogRHI,Fatal,TEXT("Unknown RHI texture type"));
		return GL_NONE;
	}
}

class OPENGLDRV_API FTextureEvictionInterface
{
public:
	virtual bool CanCreateAsEvicted() = 0;
	virtual void RestoreEvictedGLResource(bool bAttemptToRetainMips) = 0;
	virtual bool CanBeEvicted() = 0;
	virtual void TryEvictGLResource() = 0;
};

class FTextureEvictionLRU
{
private:
	typedef TPsoLruCache<class FOpenGLTextureBase*, class FOpenGLTextureBase*> FOpenGLTextureLRUContainer;
	FCriticalSection TextureLRULock;

	static FORCEINLINE_DEBUGGABLE FOpenGLTextureLRUContainer& GetLRUContainer()
	{
		const int32 MaxNumLRUs = 10000;
		static FOpenGLTextureLRUContainer TextureLRU(MaxNumLRUs);
		return TextureLRU;
	}

public:

	static FORCEINLINE_DEBUGGABLE FTextureEvictionLRU& Get()
	{
		static FTextureEvictionLRU Lru;
		return Lru;
	}
	uint32 Num() const { return GetLRUContainer().Num(); }

	void Remove(class FOpenGLTextureBase* TextureBase);
	bool Add(class FOpenGLTextureBase* TextureBase);
	void Touch(class FOpenGLTextureBase* TextureBase);
	void TickEviction();
	class FOpenGLTextureBase* GetLeastRecent();
};
class FTextureEvictionParams
{
public:
	FTextureEvictionParams(uint32 NumMips);
	~FTextureEvictionParams();
	TArray<TArray<uint8>> MipImageData;

 	uint32 bHasRestored : 1;	
	FSetElementId LRUNode;
	uint32 FrameLastRendered;

#if GLDEBUG_LABELS_ENABLED
	FAnsiCharArray TextureDebugName;
	void SetDebugLabelName(const FAnsiCharArray& TextureDebugNameIn) { TextureDebugName = TextureDebugNameIn; }
	void SetDebugLabelName(const ANSICHAR * TextureDebugNameIn) { TextureDebugName.Append(TextureDebugNameIn, FCStringAnsi::Strlen(TextureDebugNameIn) + 1); }
	FAnsiCharArray& GetDebugLabelName() { return TextureDebugName; }
#else
	void SetDebugLabelName(FAnsiCharArray TextureDebugNameIn) { checkNoEntry(); }
	FAnsiCharArray& GetDebugLabelName() { checkNoEntry(); static FAnsiCharArray Dummy;  return Dummy; }
#endif

	void SetMipData(uint32 MipIndex, const void* Data, uint32 Bytes);
	void ReleaseMipData(uint32 RetainMips);

	void CloneMipData(const FTextureEvictionParams& Src, uint32 NumMips, int32 SrcOffset, int DstOffset);

	uint32 GetTotalAllocated() const {
		uint32 TotalAllocated = 0;
		for (const auto& MipData : MipImageData)
		{
			TotalAllocated += MipData.Num();
		}
		return TotalAllocated;
	}

	bool AreAllMipsPresent() const {
		bool bRet = MipImageData.Num() > 0;
		for (const auto& MipData : MipImageData)
		{
			bRet = bRet && MipData.Num() > 0;
		}
		return bRet;
	}
};

extern uint32 GTotalMipRestores;
class OPENGLDRV_API FOpenGLTextureBase : public FTextureEvictionInterface
{
protected:
	// storing this as static as we can be in the >10,000s instances range.
	static class FOpenGLDynamicRHI* OpenGLRHI;

public:
	// Pointer to current sampler state in this unit
	class FOpenGLSamplerState* SamplerState;

private:
	/** The OpenGL texture resource. */
	GLuint Resource;

	void TryRestoreGLResource()
	{
		if (EvictionParamsPtr.IsValid() && !EvictionParamsPtr->bHasRestored)
		{
			VERIFY_GL_SCOPE();
			if (!EvictionParamsPtr->bHasRestored)
			{
				RestoreEvictedGLResource(true);
			}
			else 
			{
				check(CanBeEvicted());
				FTextureEvictionLRU::Get().Touch(this);
			}
		}
	}
public:

	GLuint GetResource()
	{
		TryRestoreGLResource();
		return Resource;
	}

	GLuint& GetResourceRef() 
	{ 
		VERIFY_GL_SCOPE();
		TryRestoreGLResource();
		return Resource;
	}

	// GetRawResourceName - A const accessor to the resource name, this could potentially be an evicted resource.
	// It will not trigger the GL resource's creation.
	GLuint GetRawResourceName() const
	{
		return Resource;
	}

	// GetRawResourceNameRef - A const accessor to the resource name, this could potentially be an evicted resource.
	// It will not trigger the GL resource's creation.
	const GLuint& GetRawResourceNameRef() const
	{
		return Resource;
	}

	void SetResource(GLuint InResource)
	{
		VERIFY_GL_SCOPE();
		Resource = InResource;
	}

	/** The OpenGL texture target. */
	GLenum Target;

	/** The number of mips in the texture. */
	uint32 NumMips;

	/** The OpenGL attachment point. This should always be GL_COLOR_ATTACHMENT0 in case of color buffer, but the actual texture may be attached on other color attachments. */
	GLenum Attachment;

	/** OpenGL 3 Stencil/SRV workaround texture resource */
	GLuint SRVResource;

	/** Initialization constructor. */
	FOpenGLTextureBase(
		FOpenGLDynamicRHI* InOpenGLRHI,
		GLuint InResource,
		GLenum InTarget,
		uint32 InNumMips,
		GLenum InAttachment
		)
	: SamplerState(nullptr)
	, Resource(InResource)
	, Target(InTarget)
	, NumMips(InNumMips)
	, Attachment(InAttachment)
	, SRVResource( 0 )
	, MemorySize( 0 )
	, bIsPowerOfTwo(false)
	, bIsAliased(false)
	, bMemorySizeReady(false)
	{
		check(OpenGLRHI == nullptr || OpenGLRHI == InOpenGLRHI);
		OpenGLRHI = InOpenGLRHI;
	}

	virtual ~FOpenGLTextureBase()
	{
		FTextureEvictionLRU::Get().Remove(this);

		if (EvictionParamsPtr.IsValid())
		{
			RunOnGLRenderContextThread([EvictionParamsPtr = MoveTemp(EvictionParamsPtr)]() {
				// EvictionParamsPtr is deleted on RHIT after this.
			});
		}
	}

	int32 GetMemorySize() const
	{
		check(bMemorySizeReady);
		return MemorySize;
	}

	void SetMemorySize(uint32 InMemorySize)
	{
		check(!bMemorySizeReady);
		MemorySize = InMemorySize;
		bMemorySizeReady = true;
	}

	bool IsMemorySizeSet()
	{
		return bMemorySizeReady;
	}

	void SetIsPowerOfTwo(bool bInIsPowerOfTwo)
	{
		bIsPowerOfTwo  = bInIsPowerOfTwo ? 1 : 0;
	}

	bool IsPowerOfTwo() const
	{
		return bIsPowerOfTwo != 0;
	}

	void SetAliased(const bool bInAliased)
	{
		bIsAliased = bInAliased ? 1 : 0;
	}

	bool IsAliased() const
	{
		return bIsAliased != 0;
	}

	void AliasResources(class FOpenGLTextureBase* Texture)
	{
		VERIFY_GL_SCOPE();
		// restore the source texture, do not allow the texture to become evicted, the aliasing texture cannot re-create the resource.
		if (Texture->IsEvicted())
		{
			Texture->RestoreEvictedGLResource(false);
		}
		Resource = Texture->Resource;
		SRVResource = Texture->SRVResource;
		bIsAliased = 1;
	}

	TUniquePtr<FTextureEvictionParams> EvictionParamsPtr;
	FOpenGLAssertRHIThreadFence CreationFence;
	
	bool IsEvicted() const { VERIFY_GL_SCOPE(); return EvictionParamsPtr.IsValid() && !EvictionParamsPtr->bHasRestored; }
private:
	uint32 MemorySize		: 30;
	uint32 bIsPowerOfTwo	: 1;
	uint32 bIsAliased : 1;
	uint32 bMemorySizeReady : 1;
};

// Textures.
template<typename BaseType>
class OPENGLDRV_API TOpenGLTexture : public BaseType, public FOpenGLTextureBase
{
public:

	/** Initialization constructor. */
	TOpenGLTexture(
		class FOpenGLDynamicRHI* InOpenGLRHI,
		GLuint InResource,
		GLenum InTarget,
		GLenum InAttachment,
		uint32 InSizeX,
		uint32 InSizeY,
		uint32 InSizeZ,
		uint32 InNumMips,
		uint32 InNumSamples,
		uint32 InNumSamplesTileMem, /* For render targets on Android tiled GPUs, the number of samples to use internally */
		uint32 InArraySize,
		EPixelFormat InFormat,
		bool bInCubemap,
		bool bInAllocatedStorage,
		ETextureCreateFlags InFlags,
		const FClearValueBinding& InClearValue
		)
	: BaseType(InSizeX,InSizeY,InSizeZ,InNumMips,InNumSamples, InNumSamplesTileMem, InArraySize, InFormat,InFlags, InClearValue)
	, FOpenGLTextureBase(
		InOpenGLRHI,
		InResource,
		InTarget,
		InNumMips,
		InAttachment
		)
	, BaseLevel(0)
	, bCubemap(bInCubemap)
	{
		PixelBuffers.AddZeroed(this->GetNumMips() * (bCubemap ? 6 : 1) * GetEffectiveSizeZ());
		SetAllocatedStorage(bInAllocatedStorage);
	}

private:
	void DeleteGLResource()
	{
		auto DeleteGLResources = [OpenGLRHI = this->OpenGLRHI, Resource = this->GetRawResourceName(), SRVResource = this->SRVResource, Target = this->Target, Flags = this->GetFlags(), Aliased = this->IsAliased()]()
		{
			VERIFY_GL_SCOPE();
			if (Resource != 0)
			{
				switch (Target)
				{
					case GL_TEXTURE_2D:
					case GL_TEXTURE_2D_MULTISAMPLE:
					case GL_TEXTURE_3D:
					case GL_TEXTURE_CUBE_MAP:
					case GL_TEXTURE_2D_ARRAY:
					case GL_TEXTURE_CUBE_MAP_ARRAY:
	#if PLATFORM_ANDROID && !PLATFORM_LUMINGL4
					case GL_TEXTURE_EXTERNAL_OES:
	#endif
					{
						OpenGLRHI->InvalidateTextureResourceInCache(Resource);
						if (SRVResource)
						{
							OpenGLRHI->InvalidateTextureResourceInCache(SRVResource);
						}

						if (!Aliased)
						{
							FOpenGL::DeleteTextures(1, &Resource);
							if (SRVResource)
							{
								FOpenGL::DeleteTextures(1, &SRVResource);
							}
						}
						break;
					}
					case GL_RENDERBUFFER:
					{
						if (!(Flags & TexCreate_Presentable))
						{
							glDeleteRenderbuffers(1, &Resource);
						}
						break;
					}
					default:
					{
						checkNoEntry();
					}
				}
			}
		};

		RunOnGLRenderContextThread(MoveTemp(DeleteGLResources));
	}

public:

	virtual ~TOpenGLTexture()
	{
		if (GIsRHIInitialized)
		{
			if (IsInActualRenderingThread())
			{
				this->CreationFence.WaitFence();
			}

			if(!CanCreateAsEvicted())
			{
				// TODO: this should run on the RHIT now.
				ReleaseOpenGLFramebuffers(this->OpenGLRHI, this);
			}

			DeleteGLResource();
			OpenGLTextureDeleted(this);
		}
	}

	virtual void* GetTextureBaseRHI() override final
	{
		return static_cast<FOpenGLTextureBase*>(this);
	}

	/**
	 * Locks one of the texture's mip-maps.
	 * @return A pointer to the specified texture data.
	 */
	void* Lock(uint32 MipIndex,uint32 ArrayIndex,EResourceLockMode LockMode,uint32& DestStride);

	/**
	* Returns the size of the memory block that is returned from Lock, threadsafe
	*/
	uint32 GetLockSize(uint32 MipIndex, uint32 ArrayIndex, EResourceLockMode LockMode, uint32& DestStride);

	/** Unlocks a previously locked mip-map. */
	void Unlock(uint32 MipIndex,uint32 ArrayIndex);

	// Accessors.
	bool IsDynamic() const { return (this->GetFlags() & TexCreate_Dynamic) != 0; }
	bool IsCubemap() const { return bCubemap != 0; }
	bool IsStaging() const { return (this->GetFlags() & TexCreate_CPUReadback) != 0; }


	/** FRHITexture override.  See FRHITexture::GetNativeResource() */
	virtual void* GetNativeResource() const override
	{
		// this must become a full GL resource here, calling the non-const GetResourceRef ensures this.
		return const_cast<void*>(reinterpret_cast<const void*>(&const_cast<TOpenGLTexture*>(this)->GetResourceRef()));
	}

	/**
	 * Accessors to mark whether or not we have allocated storage for each mip/face.
	 * For non-cubemaps FaceIndex should always be zero.
	 */
	bool GetAllocatedStorageForMip(uint32 MipIndex, uint32 FaceIndex) const
	{
		return bAllocatedStorage[MipIndex * (bCubemap ? 6 : 1) + FaceIndex];
	}
	void SetAllocatedStorageForMip(uint32 MipIndex, uint32 FaceIndex)
	{
		bAllocatedStorage[MipIndex * (bCubemap ? 6 : 1) + FaceIndex] = true;
	}

	// Set allocated storage state for all mip/faces
	void SetAllocatedStorage(bool bInAllocatedStorage)
	{
		bAllocatedStorage.Init(bInAllocatedStorage, this->GetNumMips() * (bCubemap ? 6 : 1));
	}

	/**
	 * Clone texture from a source using CopyImageSubData
	 */
	void CloneViaCopyImage( TOpenGLTexture* Src, uint32 InNumMips, int32 SrcOffset, int32 DstOffset);

	/**
	 * Clone texture from a source going via PBOs
	 */
	void CloneViaPBO( TOpenGLTexture* Src, uint32 InNumMips, int32 SrcOffset, int32 DstOffset);

	/**
	 * Resolved the specified face for a read Lock, for non-renderable, CPU readable surfaces this eliminates the readback inside Lock itself.
	 */
	void Resolve(uint32 MipIndex,uint32 ArrayIndex);

	/*
	 * FTextureEvictionInterface
	 */
	virtual void RestoreEvictedGLResource(bool bAttemptToRetainMips) override;
	virtual bool CanCreateAsEvicted() override;
	virtual bool CanBeEvicted() override;
	virtual void TryEvictGLResource() override;
private:
	TArray< TRefCountPtr<FOpenGLPixelBuffer> > PixelBuffers;

	uint32 GetEffectiveSizeZ( void ) { return this->GetSizeZ() ? this->GetSizeZ() : 1; }

	/** Index of the largest mip-map in the texture */
	uint32 BaseLevel;

	/** Bitfields marking whether we have allocated storage for each mip */
	TBitArray<TInlineAllocator<1> > bAllocatedStorage;

	/** Whether the texture is a cube-map. */
	const uint32 bCubemap : 1;
};

template <typename T>
struct TIsGLResourceWithFence
{
	enum
	{
		Value = TOr<
		TPointerIsConvertibleFromTo<T, const FOpenGLTextureBase>
		//		,TIsDerivedFrom<T, FRHITexture>
		>::Value
	};
};

template<typename T>
static typename TEnableIf<!TIsGLResourceWithFence<T>::Value>::Type CheckRHITFence(T* Resource) {}

template<typename T>
static typename TEnableIf<TIsGLResourceWithFence<T>::Value>::Type CheckRHITFence(T* Resource)
{
	Resource->CreationFence.WaitFenceRenderThreadOnly();
}

class OPENGLDRV_API FOpenGLBaseTexture2D : public FRHITexture2D
{
public:
	FOpenGLBaseTexture2D(uint32 InSizeX, uint32 InSizeY, uint32 InSizeZ, uint32 InNumMips, uint32 InNumSamples, uint32 InNumSamplesTileMem, uint32 InArraySize, EPixelFormat InFormat, ETextureCreateFlags InFlags, const FClearValueBinding& InClearValue)
	: FRHITexture2D(InSizeX,InSizeY,InNumMips,InNumSamples,InFormat,InFlags, InClearValue)
	, SampleCount(InNumSamples)
	, SampleCountTileMem(InNumSamplesTileMem)
	{}
	uint32 GetSizeZ() const { return 0; }
	uint32 GetNumSamples() const { return SampleCount; }
	uint32 GetNumSamplesTileMem() const { return SampleCountTileMem; }
private:
	uint32 SampleCount;
	/* For render targets on Android tiled GPUs, the number of samples to use internally */
	uint32 SampleCountTileMem;
};

class FOpenGLBaseTexture2DArray : public FRHITexture2DArray
{
public:
	FOpenGLBaseTexture2DArray(uint32 InSizeX, uint32 InSizeY, uint32 InSizeZ, uint32 InNumMips, uint32 InNumSamples, uint32 InNumSamplesTileMem, uint32 InArraySize, EPixelFormat InFormat, ETextureCreateFlags InFlags, const FClearValueBinding& InClearValue)
	: FRHITexture2DArray(InSizeX,InSizeY,InSizeZ,InNumMips,InNumSamples,InFormat,InFlags, InClearValue)
	{
		check(InNumSamples == 1);	// OpenGL supports multisampled texture arrays, but they're currently not implemented in OpenGLDrv.
		check(InNumSamplesTileMem == 1);
	}
};

class FOpenGLBaseTextureCube : public FRHITextureCube
{
public:
	FOpenGLBaseTextureCube(uint32 InSizeX, uint32 InSizeY, uint32 InSizeZ, uint32 InNumMips, uint32 InNumSamples, uint32 InNumSamplesTileMem, uint32 InArraySize, EPixelFormat InFormat, ETextureCreateFlags InFlags, const FClearValueBinding& InClearValue)
	: FRHITextureCube(InSizeX,InNumMips,InFormat,InFlags,InClearValue)
	, ArraySize(InArraySize)
	{
		check(InNumSamples == 1);	// OpenGL doesn't currently support multisampled cube textures
		check(InNumSamplesTileMem == 1);
	}
	uint32 GetSizeX() const { return GetSize(); }
	uint32 GetSizeY() const { return GetSize(); } //-V524
	uint32 GetSizeZ() const { return ArraySize > 1 ? ArraySize : 0; }

	uint32 GetArraySize() const {return ArraySize;}
private:
	uint32 ArraySize;
};

class FOpenGLBaseTexture3D : public FRHITexture3D
{
public:
	FOpenGLBaseTexture3D(uint32 InSizeX, uint32 InSizeY, uint32 InSizeZ, uint32 InNumMips, uint32 InNumSamples, uint32 InNumSamplesTileMem, uint32 InArraySize, EPixelFormat InFormat, ETextureCreateFlags InFlags, const FClearValueBinding& InClearValue)
	: FRHITexture3D(InSizeX,InSizeY,InSizeZ,InNumMips,InFormat,InFlags,InClearValue)
	{
		check(InNumSamples == 1);	// Can't have multisampled texture 3D. Not supported anywhere.
		check(InNumSamplesTileMem == 1);
	}
};

class OPENGLDRV_API FOpenGLBaseTexture : public FRHITexture
{
public:
	FOpenGLBaseTexture(uint32 InSizeX, uint32 InSizeY, uint32 InSizeZ, uint32 InNumMips, uint32 InNumSamples, uint32 InNumSamplesTileMem, uint32 InArraySize, EPixelFormat InFormat, ETextureCreateFlags InFlags, const FClearValueBinding& InClearValue)
		: FRHITexture(InNumMips, InNumSamples, InFormat, InFlags, NULL, InClearValue)
	{}

	uint32 GetSizeX() const { return 0; }
	uint32 GetSizeY() const { return 0; }
	uint32 GetSizeZ() const { return 0; }
};

typedef TOpenGLTexture<FOpenGLBaseTexture>				FOpenGLTexture;
typedef TOpenGLTexture<FOpenGLBaseTexture2D>			FOpenGLTexture2D;
typedef TOpenGLTexture<FOpenGLBaseTexture2DArray>		FOpenGLTexture2DArray;
typedef TOpenGLTexture<FOpenGLBaseTexture3D>			FOpenGLTexture3D;
typedef TOpenGLTexture<FOpenGLBaseTextureCube>			FOpenGLTextureCube;

class FOpenGLTextureReference : public FRHITextureReference
{
	FOpenGLTextureBase* TexturePtr;

public:
	explicit FOpenGLTextureReference(FLastRenderTimeContainer* InLastRenderTime)
		: FRHITextureReference(InLastRenderTime)
		, TexturePtr(NULL)
	{}

	void SetReferencedTexture(FRHITexture* InTexture);
	FOpenGLTextureBase* GetTexturePtr() const { return TexturePtr; }

	virtual void* GetTextureBaseRHI() override final
	{
		return TexturePtr;
	}
};

/** Given a pointer to a RHI texture that was created by the OpenGL RHI, returns a pointer to the FOpenGLTextureBase it encapsulates. */
inline FOpenGLTextureBase* GetOpenGLTextureFromRHITexture(FRHITexture* Texture)
{
	if(!Texture)
	{
		return NULL;
	}
	else
	{
		CheckRHITFence(static_cast<FOpenGLTextureBase*>(Texture->GetTextureBaseRHI()));
		return static_cast<FOpenGLTextureBase*>(Texture->GetTextureBaseRHI());
	}
}

inline uint32 GetOpenGLTextureSizeXFromRHITexture(FRHITexture* Texture)
{
	if(!Texture)
	{
		return 0;
	}
	CheckRHITFence(static_cast<FOpenGLTextureBase*>(Texture->GetTextureBaseRHI()));
	if(Texture->GetTexture2D())
	{
		return ((FOpenGLTexture2D*)Texture)->GetSizeX();
	}
	else if(Texture->GetTexture2DArray())
	{
		return ((FOpenGLTexture2DArray*)Texture)->GetSizeX();
	}
	else if(Texture->GetTexture3D())
	{
		return ((FOpenGLTexture3D*)Texture)->GetSizeX();
	}
	else if(Texture->GetTextureCube())
	{
		return ((FOpenGLTextureCube*)Texture)->GetSize();
	}
	else
	{
		UE_LOG(LogRHI,Fatal,TEXT("Unknown RHI texture type"));
		return 0;
	}
}

inline uint32 GetOpenGLTextureSizeYFromRHITexture(FRHITexture* Texture)
{
	if(!Texture)
	{
		return 0;
	}

	CheckRHITFence(static_cast<FOpenGLTextureBase*>(Texture->GetTextureBaseRHI()));
	if(Texture->GetTexture2D())
	{
		return ((FOpenGLTexture2D*)Texture)->GetSizeY();
	}
	else if(Texture->GetTexture2DArray())
	{
		return ((FOpenGLTexture2DArray*)Texture)->GetSizeY();
	}
	else if(Texture->GetTexture3D())
	{
		return ((FOpenGLTexture3D*)Texture)->GetSizeY();
	}
	else if(Texture->GetTextureCube())
	{
		return ((FOpenGLTextureCube*)Texture)->GetSize();
	}
	else
	{
		UE_LOG(LogRHI,Fatal,TEXT("Unknown RHI texture type"));
		return 0;
	}
}

inline uint32 GetOpenGLTextureSizeZFromRHITexture(FRHITexture* Texture)
{
	if(!Texture)
	{
		return 0;
	}

	CheckRHITFence(Texture);
	if(Texture->GetTexture2D())
	{
		return 0;
	}
	else if(Texture->GetTexture2DArray())
	{
		return ((FOpenGLTexture2DArray*)Texture)->GetSizeZ();
	}
	else if(Texture->GetTexture3D())
	{
		return ((FOpenGLTexture3D*)Texture)->GetSizeZ();
	}
	else if(Texture->GetTextureCube())
	{
		return ((FOpenGLTextureCube*)Texture)->GetSizeZ();
	}
	else
	{
		UE_LOG(LogRHI,Fatal,TEXT("Unknown RHI texture type"));
		return 0;
	}
}

class FOpenGLRenderQuery : public FRHIRenderQuery
{
public:

	/** The query resource. */
	GLuint Resource;

	/** Identifier of the OpenGL context the query is a part of. */
	uint64 ResourceContext;

	/** The cached query result. */
	GLuint64 Result;

	FOpenGLAssertRHIThreadFence CreationFence;

	FThreadSafeCounter TotalBegins;
	FThreadSafeCounter TotalResults;

	/** true if the context the query is in was released from another thread */
	bool bResultWasSuccess;

	/** true if the context the query is in was released from another thread */
	bool bInvalidResource;

	// todo: memory optimize
	ERenderQueryType QueryType;

	FOpenGLRenderQuery(ERenderQueryType InQueryType);
	virtual ~FOpenGLRenderQuery();

	void AcquireResource();
	static void ReleaseResource(GLuint Resource, uint64 ResourceContext);
};

class FOpenGLUnorderedAccessView : public FRHIUnorderedAccessView
{

public:
	FOpenGLUnorderedAccessView():
		Resource(0),
		BufferResource(0),
		Format(0),
		UnrealFormat(0)
	{

	}

	GLuint	Resource;
	GLuint	BufferResource;
	GLenum	Format;
	uint8	UnrealFormat;

	virtual uint32 GetBufferSize()
	{
		return 0;
	}

	virtual bool IsLayered() const
	{
		return false;
	}

	virtual GLint GetLayer() const
	{
		return 0;
	}

};

class FOpenGLTextureUnorderedAccessView : public FOpenGLUnorderedAccessView
{
public:

	FOpenGLTextureUnorderedAccessView(FRHITexture* InTexture);

	FTextureRHIRef TextureRHI; // to keep the texture alive
	bool bLayered;

	virtual bool IsLayered() const override
	{
		return bLayered;
	}
};


class FOpenGLVertexBufferUnorderedAccessView : public FOpenGLUnorderedAccessView
{
public:

	FOpenGLVertexBufferUnorderedAccessView();

	FOpenGLVertexBufferUnorderedAccessView(	FOpenGLDynamicRHI* InOpenGLRHI, FRHIVertexBuffer* InVertexBuffer, uint8 Format);

	virtual ~FOpenGLVertexBufferUnorderedAccessView();

	FVertexBufferRHIRef VertexBufferRHI; // to keep the vertex buffer alive

	FOpenGLDynamicRHI* OpenGLRHI;

	virtual uint32 GetBufferSize() override;
};

class FOpenGLStructuredBufferUnorderedAccessView : public FOpenGLUnorderedAccessView
{
public:
	FOpenGLStructuredBufferUnorderedAccessView();

	FOpenGLStructuredBufferUnorderedAccessView(	FOpenGLDynamicRHI* InOpenGLRHI, FRHIStructuredBuffer* InBuffer, uint8 Format);

	virtual ~FOpenGLStructuredBufferUnorderedAccessView();

	FStructuredBufferRHIRef StructuredBufferRHI; // to keep the stuctured buffer alive

	FOpenGLDynamicRHI* OpenGLRHI;

	virtual uint32 GetBufferSize() override;
};

class FOpenGLShaderResourceView : public FRefCountedObject
{
	// In OpenGL 3.2, the only view that actually works is a Buffer<type> kind of view from D3D10,
	// and it's mapped to OpenGL's buffer texture.

public:

	/** OpenGL texture the buffer is bound with */
	GLuint Resource;
	GLenum Target;

	/** Needed on GL <= 4.2 to copy stencil data out of combined depth-stencil surfaces. */
	FTexture2DRHIRef Texture2D;

	int32 LimitMip;

	/** Needed on OS X to force a rebind of the texture buffer to the texture name to workaround radr://18379338 */
	FVertexBufferRHIRef VertexBuffer;
	FIndexBufferRHIRef IndexBuffer;
	uint64 ModificationVersion;
	uint8 Format;

	FOpenGLShaderResourceView( FOpenGLDynamicRHI* InOpenGLRHI, GLuint InResource, GLenum InTarget )
	:	Resource(InResource)
	,	Target(InTarget)
	,	LimitMip(-1)
	,	ModificationVersion(0)
	,	Format(0)
	,	OpenGLRHI(InOpenGLRHI)
	,	OwnsResource(true)
	{}

	FOpenGLShaderResourceView(FOpenGLDynamicRHI* InOpenGLRHI, GLuint InResource, GLenum InTarget, FRHIIndexBuffer* InIndexBuffer)
		: Resource(InResource)
		, Target(InTarget)
		, LimitMip(-1)
		, IndexBuffer(InIndexBuffer)
		, ModificationVersion(0)
		, Format(0)
		, OpenGLRHI(InOpenGLRHI)
		, OwnsResource(true)
	{
		if (IndexBuffer)
		{
			FOpenGLIndexBuffer* IB = (FOpenGLIndexBuffer*)IndexBuffer.GetReference();
			ModificationVersion = IB->ModificationCount;
		}
	}

	FOpenGLShaderResourceView( FOpenGLDynamicRHI* InOpenGLRHI, GLuint InResource, GLenum InTarget, FRHIVertexBuffer* InVertexBuffer, uint8 InFormat )
	:	Resource(InResource)
	,	Target(InTarget)
	,	LimitMip(-1)
	,	VertexBuffer(InVertexBuffer)
	,	ModificationVersion(0)
	,	Format(InFormat)
	,	OpenGLRHI(InOpenGLRHI)
	,	OwnsResource(true)
	{
		if (VertexBuffer)
		{
			FOpenGLVertexBuffer* VB = (FOpenGLVertexBuffer*)VertexBuffer.GetReference();
			ModificationVersion = VB->ModificationCount;
		}
	}

	FOpenGLShaderResourceView( FOpenGLDynamicRHI* InOpenGLRHI, GLuint InResource, GLenum InTarget, GLuint Mip, bool InOwnsResource )
	:	Resource(InResource)
	,	Target(InTarget)
	,	LimitMip(Mip)
	,	ModificationVersion(0)
	,	Format(0)
	,	OpenGLRHI(InOpenGLRHI)
	,	OwnsResource(InOwnsResource)
	{}

	virtual ~FOpenGLShaderResourceView( void );

protected:
	FOpenGLDynamicRHI* OpenGLRHI;
	bool OwnsResource;
};

// this class is required to remove the SRV from the shader cache upon deletion
class FOpenGLShaderResourceViewProxy : public TOpenGLResourceProxy<FRHIShaderResourceView, FOpenGLShaderResourceView>
{
public:
	FOpenGLShaderResourceViewProxy(TFunction<FOpenGLShaderResourceView*(FRHIShaderResourceView*)> CreateFunc)
		: TOpenGLResourceProxy<FRHIShaderResourceView, FOpenGLShaderResourceView>(CreateFunc)
	{}

	virtual ~FOpenGLShaderResourceViewProxy()
	{

	}
};

template<>
struct TIsGLProxyObject<FOpenGLShaderResourceViewProxy>
{
	enum { Value = true };
};

void OPENGLDRV_API OpenGLTextureDeleted(FRHITexture* Texture);
void OPENGLDRV_API OpenGLTextureAllocated( FRHITexture* Texture , ETextureCreateFlags Flags);

void OPENGLDRV_API ReleaseOpenGLFramebuffers(FOpenGLDynamicRHI* Device, FRHITexture* TextureRHI);

/** A OpenGL event query resource. */
class FOpenGLEventQuery : public FRenderResource
{
public:

	/** Initialization constructor. */
	FOpenGLEventQuery(class FOpenGLDynamicRHI* InOpenGLRHI)
		: OpenGLRHI(InOpenGLRHI)
		, Sync(UGLsync())
	{
	}

	/** Issues an event for the query to poll. */
	void IssueEvent();

	/** Waits for the event query to finish. */
	void WaitForCompletion();

	// FRenderResource interface.
	virtual void InitDynamicRHI() override;
	virtual void ReleaseDynamicRHI() override;

private:
	FOpenGLDynamicRHI* OpenGLRHI;
	UGLsync Sync;
};

class FOpenGLViewport : public FRHIViewport
{
public:

	FOpenGLViewport(class FOpenGLDynamicRHI* InOpenGLRHI,void* InWindowHandle,uint32 InSizeX,uint32 InSizeY,bool bInIsFullscreen,EPixelFormat PreferredPixelFormat);
	~FOpenGLViewport();

	void Resize(uint32 InSizeX,uint32 InSizeY,bool bInIsFullscreen);

	// Accessors.
	FIntPoint GetSizeXY() const { return FIntPoint(SizeX, SizeY); }
	FOpenGLTexture2D *GetBackBuffer() const { return BackBuffer; }
	bool IsFullscreen( void ) const { return bIsFullscreen; }

	virtual void WaitForFrameEventCompletion() override
	{
		FrameSyncEvent.WaitForCompletion();
	}

	virtual void IssueFrameEvent() override
	{
		FrameSyncEvent.IssueEvent();
	}

	virtual void* GetNativeWindow(void** AddParam) const override;

	struct FPlatformOpenGLContext* GetGLContext() const { return OpenGLContext; }
	FOpenGLDynamicRHI* GetOpenGLRHI() const { return OpenGLRHI; }

	virtual void SetCustomPresent(FRHICustomPresent* InCustomPresent) override
	{
		CustomPresent = InCustomPresent;
	}
	FRHICustomPresent* GetCustomPresent() const { return CustomPresent.GetReference(); }
private:

	friend class FOpenGLDynamicRHI;

	FOpenGLDynamicRHI* OpenGLRHI;
	struct FPlatformOpenGLContext* OpenGLContext;
	uint32 SizeX;
	uint32 SizeY;
	bool bIsFullscreen;
	EPixelFormat PixelFormat;
	bool bIsValid;
	TRefCountPtr<FOpenGLTexture2D> BackBuffer;
	FOpenGLEventQuery FrameSyncEvent;
	FCustomPresentRHIRef CustomPresent;
};

class FOpenGLGPUFence final : public FRHIGPUFence
{
public:
	FOpenGLGPUFence(FName InName);
	~FOpenGLGPUFence() override;

	void Clear() override;
	bool Poll() const override;
	
	void WriteInternal();
private:
	struct FOpenGLGPUFenceProxy* Proxy;
};

class FOpenGLStagingBuffer final : public FRHIStagingBuffer
{
	friend class FOpenGLDynamicRHI;
public:
	FOpenGLStagingBuffer() : FRHIStagingBuffer()
	{
		Initialize();
	}

	~FOpenGLStagingBuffer() override;

	// Locks the shadow of VertexBuffer for read. This will stall the RHI thread.
	void *Lock(uint32 Offset, uint32 NumBytes) override;

	// Unlocks the shadow. This is an error if it was not locked previously.
	void Unlock() override;
private:
	void Initialize();

	GLuint ShadowBuffer;
	uint32 ShadowSize;
	void* Mapping;
};

template<class T>
struct TOpenGLResourceTraits
{
};
template<>
struct TOpenGLResourceTraits<FRHIGPUFence>
{
	typedef FOpenGLGPUFence TConcreteType;
};
template<>
struct TOpenGLResourceTraits<FRHIStagingBuffer>
{
	typedef FOpenGLStagingBuffer TConcreteType;
};
template<>
struct TOpenGLResourceTraits<FRHIVertexDeclaration>
{
	typedef FOpenGLVertexDeclaration TConcreteType;
};
template<>
struct TOpenGLResourceTraits<FRHIVertexShader>
{
	typedef FOpenGLVertexShaderProxy TConcreteType;
};
template<>
struct TOpenGLResourceTraits<FRHIGeometryShader>
{
	typedef FOpenGLGeometryShaderProxy TConcreteType;
};
template<>
struct TOpenGLResourceTraits<FRHIHullShader>
{
	typedef FOpenGLHullShaderProxy TConcreteType;
};
template<>
struct TOpenGLResourceTraits<FRHIDomainShader>
{
	typedef FOpenGLDomainShaderProxy TConcreteType;
};
template<>
struct TOpenGLResourceTraits<FRHIPixelShader>
{
	typedef FOpenGLPixelShaderProxy TConcreteType;
};
template<>
struct TOpenGLResourceTraits<FRHIComputeShader>
{
	typedef FOpenGLComputeShaderProxy TConcreteType;
};
template<>
struct TOpenGLResourceTraits<FRHIBoundShaderState>
{
	typedef FOpenGLBoundShaderState TConcreteType;
};
template<>
struct TOpenGLResourceTraits<FRHITexture3D>
{
	typedef FOpenGLTexture3D TConcreteType;
};
template<>
struct TOpenGLResourceTraits<FRHITexture>
{
	typedef FOpenGLTexture TConcreteType;
};
template<>
struct TOpenGLResourceTraits<FRHITexture2D>
{
	typedef FOpenGLTexture2D TConcreteType;
};
template<>
struct TOpenGLResourceTraits<FRHITexture2DArray>
{
	typedef FOpenGLTexture2DArray TConcreteType;
};
template<>
struct TOpenGLResourceTraits<FRHITextureCube>
{
	typedef FOpenGLTextureCube TConcreteType;
};
template<>
struct TOpenGLResourceTraits<FRHIRenderQuery>
{
	typedef FOpenGLRenderQuery TConcreteType;
};
template<>
struct TOpenGLResourceTraits<FRHIUniformBuffer>
{
	typedef FOpenGLUniformBuffer TConcreteType;
};
template<>
struct TOpenGLResourceTraits<FRHIIndexBuffer>
{
	typedef FOpenGLIndexBuffer TConcreteType;
};
template<>
struct TOpenGLResourceTraits<FRHIStructuredBuffer>
{
	typedef FOpenGLStructuredBuffer TConcreteType;
};
template<>
struct TOpenGLResourceTraits<FRHIVertexBuffer>
{
	typedef FOpenGLVertexBuffer TConcreteType;
};
template<>
struct TOpenGLResourceTraits<FRHIShaderResourceView>
{
	//typedef FOpenGLShaderResourceView TConcreteType;
	typedef FOpenGLShaderResourceViewProxy TConcreteType;
};
template<>
struct TOpenGLResourceTraits<FRHIUnorderedAccessView>
{
	typedef FOpenGLUnorderedAccessView TConcreteType;
};

template<>
struct TOpenGLResourceTraits<FRHIViewport>
{
	typedef FOpenGLViewport TConcreteType;
};
