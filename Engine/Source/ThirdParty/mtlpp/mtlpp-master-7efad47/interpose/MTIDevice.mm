// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#import <Metal/Metal.h>
#include "MTIDevice.hpp"
#include "MTIBuffer.hpp"
#include "MTITexture.hpp"
#include "MTICommandQueue.hpp"
#include "MTIArgumentEncoder.hpp"
#include "MTIHeap.hpp"
#include "MTILibrary.hpp"
#include "MTIRenderPipeline.hpp"
#include "MTIComputePipeline.hpp"
#include "MTISampler.hpp"
#include "MTIFence.hpp"
#include "MTIDepthStencil.hpp"
#include "MTITrace.hpp"

MTLPP_BEGIN

#define DYLD_INTERPOSE(_replacment,_replacee) \
__attribute__((used)) struct{ const void* replacment; const void* replacee; } _interpose_##_replacee \
__attribute__ ((section ("__DATA,__interpose"))) = { (const void*)(unsigned long)&_replacment, (const void*)(unsigned long)&_replacee };

INTERPOSE_PROTOCOL_REGISTER(MTIDeviceTrace, id<MTLDevice>);

struct MTITraceDefaultDeviceCommand : public MTITraceCommand
{
	uintptr_t Device;
};

std::fstream& operator>>(std::fstream& fs, MTITraceDefaultDeviceCommand& dt)
{
	fs >> dt.Device;
	return fs;
}

std::fstream& operator<<(std::fstream& fs, const MTITraceDefaultDeviceCommand& dt)
{
	fs << dt.Device;
	return fs;
}

struct MTITraceDefaultDeviceCommandHandler : public MTITraceCommandHandler
{
	MTITraceDefaultDeviceCommandHandler()
	: MTITraceCommandHandler("", "MTLCreateSystemDefaultDevice")
	{
		
	}
	
	id <MTLDevice> __nullable Trace(id <MTLDevice> __nullable Device)
	{
		std::fstream& fs = MTITrace::Get().BeginWrite();
		MTITraceCommandHandler::Trace(fs, 0);
		MTITraceDefaultDeviceCommand Command;
		Command.Device = (uintptr_t)Device;
		fs << Command;
		MTITrace::Get().EndWrite();
		return Device;
	}
	
	virtual void Handle(MTITraceCommand& Header, std::fstream& fs)
	{
		MTITraceDefaultDeviceCommand Command;
		fs >> Command;
		id Object = MTLCreateSystemDefaultDevice();
		MTITrace::Get().RegisterObject(Command.Device, Object);
	}
};
static MTITraceDefaultDeviceCommandHandler GMTITraceDefaultDeviceCommandHandler;

MTL_EXTERN id <MTLDevice> __nullable MTLTCreateSystemDefaultDevice(void)
{
	return GMTITraceDefaultDeviceCommandHandler.Trace(MTIDeviceTrace::Register(MTLCreateSystemDefaultDevice()));
}
DYLD_INTERPOSE(MTLTCreateSystemDefaultDevice, MTLCreateSystemDefaultDevice)

struct MTITraceCopyDeviceCommand : public MTITraceCommand
{
	unsigned Num;
	NSArray* Devices;
	std::vector<uintptr_t> Backing;
};

std::fstream& operator>>(std::fstream& fs, MTITraceCopyDeviceCommand& dt)
{
	fs >> dt.Num;
	if (dt.Num)
	{
		dt.Backing.resize(dt.Num);
		for (unsigned i = 0; i < dt.Num; i++)
		{
			fs >> dt.Backing[i];
		}
	}
	return fs;
}

std::fstream& operator<<(std::fstream& fs, const MTITraceCopyDeviceCommand& dt)
{
	fs << dt.Num;
	for (id Device in dt.Devices)
	{
		fs << (uintptr_t)Device;
	}
	return fs;
}

struct MTITraceCopyDeviceCommandHandler : public MTITraceCommandHandler
{
	MTITraceCopyDeviceCommandHandler()
	: MTITraceCommandHandler("", "MTLCopyAllDevices")
	{
		
	}
	
	NSArray <id<MTLDevice>> * Trace(NSArray <id<MTLDevice>> * Devices)
	{
		std::fstream& fs = MTITrace::Get().BeginWrite();
		MTITraceCommandHandler::Trace(fs, 0);
		MTITraceCopyDeviceCommand Command;
		Command.Num = (unsigned)Devices.count;
		Command.Devices = Devices;
		fs << Command;
		MTITrace::Get().EndWrite();
		return Devices;
	}
	
	virtual void Handle(MTITraceCommand& Header, std::fstream& fs)
	{
		MTITraceCopyDeviceCommand Command;
		fs >> Command;
		
		NSArray <id<MTLDevice>> * Devices = MTLCopyAllDevices();
		for (unsigned i = 0; i < Devices.count && i < Command.Num; i++)
		{
			MTITrace::Get().RegisterObject(Command.Backing[i], Devices[i]);
		}
	}
};
static MTITraceCopyDeviceCommandHandler GMTITraceCopyDeviceCommandHandler;

MTL_EXTERN NSArray <id<MTLDevice>> *MTLTCopyAllDevices(void)
{
	NSArray <id<MTLDevice>> * Devices = GMTITraceCopyDeviceCommandHandler.Trace(MTLCopyAllDevices());
	for (id<MTLDevice> Device in Devices)
	{
		MTIDeviceTrace::Register(Device);
	}
	return Devices;
}
DYLD_INTERPOSE(MTLTCopyAllDevices, MTLCopyAllDevices)

struct MTITraceNewCommandQueueHandler : public MTITraceCommandHandler
{
	MTITraceNewCommandQueueHandler()
	: MTITraceCommandHandler("MTLDevice", "newCommandQueue")
	{
		
	}
	
	id<MTLCommandQueue> Trace(id Object, id<MTLCommandQueue> Queue)
	{
		std::fstream& fs = MTITrace::Get().BeginWrite();
		MTITraceCommandHandler::Trace(fs, (uintptr_t)Object);

		fs << (uintptr_t)Queue;

		MTITrace::Get().EndWrite();
		return Queue;
	}
	
	virtual void Handle(MTITraceCommand& Header, std::fstream& fs)
	{
		uintptr_t Result;
		fs >> Result;
		
		id<MTLCommandQueue> Queue = [(id<MTLDevice>)MTITrace::Get().FetchObject(Header.Receiver) newCommandQueue];
		assert(Queue);
		
		MTITrace::Get().RegisterObject(Result, Queue);
	}
};
static MTITraceNewCommandQueueHandler GMTITraceNewCommandQueueHandler;

id<MTLCommandQueue> MTIDeviceTrace::NewCommandQueueImpl(id Object, SEL Selector, Super::NewCommandQueueType::DefinedIMP Original)
{
	return GMTITraceNewCommandQueueHandler.Trace(Object, MTICommandQueueTrace::Register(Original(Object, Selector)));
}

struct MTITraceNewCommandQueueWithMaxHandler : public MTITraceCommandHandler
{
	MTITraceNewCommandQueueWithMaxHandler()
	: MTITraceCommandHandler("MTLDevice", "newCommandQueueWithMaxCommandBufferCount")
	{
		
	}
	
	id<MTLCommandQueue> Trace(id Object, NSUInteger Num, id<MTLCommandQueue> Queue)
	{
		std::fstream& fs = MTITrace::Get().BeginWrite();
		MTITraceCommandHandler::Trace(fs, (uintptr_t)Object);
		
		fs << Num;
		fs << (uintptr_t)Queue;
		
		MTITrace::Get().EndWrite();
		return Queue;
	}
	
	virtual void Handle(MTITraceCommand& Header, std::fstream& fs)
	{
		NSUInteger Num;
		fs >> Num;
		
		uintptr_t Result;
		fs >> Result;
		
		id<MTLCommandQueue> Queue = [(id<MTLDevice>)MTITrace::Get().FetchObject(Header.Receiver) newCommandQueueWithMaxCommandBufferCount:Num];
		assert(Queue);
		
		MTITrace::Get().RegisterObject(Result, Queue);
	}
};
static MTITraceNewCommandQueueWithMaxHandler GMTITraceNewCommandQueueWithMaxHandler;

id<MTLCommandQueue> MTIDeviceTrace::NewCommandQueueWithMaxCommandBufferCountImpl(id Object, SEL Selector, Super::NewCommandQueueWithMaxCommandBufferCountType::DefinedIMP Original, NSUInteger Num)
{
	return GMTITraceNewCommandQueueWithMaxHandler.Trace(Object, Num, MTICommandQueueTrace::Register(Original(Object, Selector, Num)));
}



struct MTITraceNewHeapHandler : public MTITraceCommandHandler
{
	MTITraceNewHeapHandler()
	: MTITraceCommandHandler("MTLDevice", "newHeapWithDescriptor")
	{
		
	}
	
	id<MTLHeap> Trace(id Object, MTLHeapDescriptor* Desc, id<MTLHeap> Heap)
	{
		std::fstream& fs = MTITrace::Get().BeginWrite();
		MTITraceCommandHandler::Trace(fs, (uintptr_t)Object);
		
		fs << Desc.size;
		fs << Desc.storageMode;
		fs << Desc.cpuCacheMode;
		fs << (uintptr_t)Heap;
		
		MTITrace::Get().EndWrite();
		return Heap;
	}
	
	virtual void Handle(MTITraceCommand& Header, std::fstream& fs)
	{
		NSUInteger size, storageMode, cpuCacheMode;
		fs >> size;
		fs >> storageMode;
		fs >> cpuCacheMode;

		MTLHeapDescriptor* Desc = [[MTLHeapDescriptor new] autorelease];
		Desc.size = size;
		Desc.storageMode = (MTLStorageMode)storageMode;
		Desc.cpuCacheMode = (MTLCPUCacheMode)cpuCacheMode;
		
		uintptr_t Result;
		fs >> Result;
		
		id<MTLHeap> Heap = [(id<MTLDevice>)MTITrace::Get().FetchObject(Header.Receiver) newHeapWithDescriptor:Desc];
		assert(Heap);
		
		MTITrace::Get().RegisterObject(Result, Heap);
	}
};
static MTITraceNewHeapHandler GMTITraceNewHeapHandler;

id<MTLHeap> MTIDeviceTrace::NewHeapWithDescriptorImpl(id Object, SEL Selector, Super::NewHeapWithDescriptorType::DefinedIMP Original, MTLHeapDescriptor* Desc)
{
	return GMTITraceNewHeapHandler.Trace(Object, Desc, MTIHeapTrace::Register(Original(Object, Selector, Desc)));
}


struct MTITraceNewBufferHandler : public MTITraceCommandHandler
{
	MTITraceNewBufferHandler()
	: MTITraceCommandHandler("MTLDevice", "newBufferWithLength")
	{
		
	}
	
	id<MTLBuffer> Trace(id Object, NSUInteger Len, MTLResourceOptions Opt, id<MTLBuffer> Buffer)
	{
		std::fstream& fs = MTITrace::Get().BeginWrite();
		MTITraceCommandHandler::Trace(fs, (uintptr_t)Object);
		
		fs << (NSUInteger)Len;
		fs << (NSUInteger)Opt;
		fs << (uintptr_t)Buffer;
		
		MTITrace::Get().EndWrite();
		return Buffer;
	}
	
	virtual void Handle(MTITraceCommand& Header, std::fstream& fs)
	{
		NSUInteger Len;
		fs >> Len;
		
		NSUInteger Opt;
		fs >> Opt;
		
		uintptr_t Result;
		fs >> Result;
		
		id<MTLBuffer> Buffer = [(id<MTLDevice>)MTITrace::Get().FetchObject(Header.Receiver) newBufferWithLength: Len options:(MTLResourceOptions)Opt];
		assert(Buffer);
		
		MTITrace::Get().RegisterObject(Result, Buffer);
	}
};
static MTITraceNewBufferHandler GMTITraceNewBufferHandler;


id<MTLBuffer> MTIDeviceTrace::NewBufferWithLengthImpl(id Object, SEL Selector, Super::NewBufferWithLengthType::DefinedIMP Original, NSUInteger Len, MTLResourceOptions Opt)
{
	return GMTITraceNewBufferHandler.Trace(Object, Len, Opt, MTIBufferTrace::Register(Original(Object, Selector, Len, Opt)));
}
id<MTLBuffer> MTIDeviceTrace::NewBufferWithBytesImpl(id Object, SEL Selector, Super::NewBufferWithBytesType::DefinedIMP Original, const void* Ptr, NSUInteger Len, MTLResourceOptions Opt)
{
	return MTIBufferTrace::Register(Original(Object, Selector, Ptr, Len, Opt));
}
id<MTLBuffer> MTIDeviceTrace::NewBufferWithBytesNoCopyImpl(id Object, SEL Selector, Super::NewBufferWithBytesNoCopyType::DefinedIMP Original, const void* Ptr, NSUInteger Len, MTLResourceOptions Opt, void (^__nullable Block)(void *pointer, NSUInteger length))
{
	return MTIBufferTrace::Register(Original(Object, Selector, Ptr, Len, Opt, Block));
}


struct MTITraceNewDepthStencilDescHandler : public MTITraceCommandHandler
{
	MTITraceNewDepthStencilDescHandler()
	: MTITraceCommandHandler("", "newDepthStencilDescriptor")
	{
		
	}
	
	MTLDepthStencilDescriptor* Trace(MTLDepthStencilDescriptor* Desc)
	{
		if (!MTITrace::Get().FetchObject((uintptr_t)Desc))
		{
			std::fstream& fs = MTITrace::Get().BeginWrite();
			MTITraceCommandHandler::Trace(fs, (uintptr_t)Desc);
			
			fs << Desc.depthCompareFunction;
			fs << Desc.depthWriteEnabled;
			fs << Desc.frontFaceStencil.stencilCompareFunction;
			fs << Desc.frontFaceStencil.stencilFailureOperation;
			fs << Desc.frontFaceStencil.depthFailureOperation;
			fs << Desc.frontFaceStencil.depthStencilPassOperation;
			fs << Desc.frontFaceStencil.readMask;
			fs << Desc.frontFaceStencil.writeMask;
			fs << Desc.backFaceStencil.stencilCompareFunction;
			fs << Desc.backFaceStencil.stencilFailureOperation;
			fs << Desc.backFaceStencil.depthFailureOperation;
			fs << Desc.backFaceStencil.depthStencilPassOperation;
			fs << Desc.backFaceStencil.readMask;
			fs << Desc.backFaceStencil.writeMask;
			fs << Desc.label ? [Desc.label UTF8String] : "";
			
			MTITrace::Get().RegisterObject((uintptr_t)Desc, Desc);
			MTITrace::Get().EndWrite();
		}
		return Desc;
	}
	
	virtual void Handle(MTITraceCommand& Header, std::fstream& fs)
	{
		uintptr_t Result;
		fs >> Result;
		
		NSUInteger depthCompareFunction;
		BOOL depthWriteEnabled;
		NSUInteger frontFaceStencilstencilCompareFunction;
		NSUInteger frontFaceStencilstencilFailureOperation;
		NSUInteger frontFaceStencildepthFailureOperation;
		NSUInteger frontFaceStencildepthStencilPassOperation;
		uint32_t frontFaceStencilreadMask;
		uint32_t frontFaceStencilwriteMask;
		NSUInteger backFaceStencilstencilCompareFunction;
		NSUInteger backFaceStencilstencilFailureOperation;
		NSUInteger backFaceStencildepthFailureOperation;
		NSUInteger backFaceStencildepthStencilPassOperation;
		uint32_t backFaceStencilreadMask;
		uint32_t backFaceStencilwriteMask;
		std::string label;
		
		MTLDepthStencilDescriptor* Desc = [MTLDepthStencilDescriptor new];
		fs >> depthCompareFunction;
		fs >> depthWriteEnabled;
		fs >> frontFaceStencilstencilCompareFunction;
		fs >> frontFaceStencilstencilFailureOperation;
		fs >> frontFaceStencildepthFailureOperation;
		fs >> frontFaceStencildepthStencilPassOperation;
		fs >> frontFaceStencilreadMask;
		fs >> frontFaceStencilwriteMask;
		fs >> backFaceStencilstencilCompareFunction;
		fs >> backFaceStencilstencilFailureOperation;
		fs >> backFaceStencildepthFailureOperation;
		fs >> backFaceStencildepthStencilPassOperation;
		fs >> backFaceStencilreadMask;
		fs >> backFaceStencilwriteMask;
		fs >> label;
		
		Desc.depthCompareFunction = 						(MTLCompareFunction)depthCompareFunction;
		Desc.depthWriteEnabled = 							depthWriteEnabled;
		Desc.frontFaceStencil.stencilCompareFunction = 		(MTLCompareFunction)frontFaceStencilstencilCompareFunction;
		Desc.frontFaceStencil.stencilFailureOperation = 	(MTLStencilOperation)frontFaceStencilstencilFailureOperation;
		Desc.frontFaceStencil.depthFailureOperation = 		(MTLStencilOperation)frontFaceStencildepthFailureOperation;
		Desc.frontFaceStencil.depthStencilPassOperation = 	(MTLStencilOperation)frontFaceStencildepthStencilPassOperation;
		Desc.frontFaceStencil.readMask = 					frontFaceStencilreadMask;
		Desc.frontFaceStencil.writeMask = 					frontFaceStencilwriteMask;
		Desc.backFaceStencil.stencilCompareFunction = 		(MTLCompareFunction)backFaceStencilstencilCompareFunction;
		Desc.backFaceStencil.stencilFailureOperation = 		(MTLStencilOperation)backFaceStencilstencilFailureOperation;
		Desc.backFaceStencil.depthFailureOperation = 		(MTLStencilOperation)backFaceStencildepthFailureOperation;
		Desc.backFaceStencil.depthStencilPassOperation = 	(MTLStencilOperation)backFaceStencildepthStencilPassOperation;
		Desc.backFaceStencil.readMask = 					backFaceStencilreadMask;
		Desc.backFaceStencil.writeMask = 					backFaceStencilwriteMask;
		Desc.label = [NSString stringWithUTF8String:label.c_str()];
		
		MTITrace::Get().RegisterObject(Header.Receiver, Desc);
	}
};
static MTITraceNewDepthStencilDescHandler GMTITraceNewDepthStencilDescHandler;

struct MTITraceNewDepthStencilStateHandler : public MTITraceCommandHandler
{
	MTITraceNewDepthStencilStateHandler()
	: MTITraceCommandHandler("MTLDevice", "newDepthStencilStateWithDescriptor")
	{
		
	}
	
	id<MTLDepthStencilState> Trace(id Object, MTLDepthStencilDescriptor* Desc, id<MTLDepthStencilState> State)
	{
		GMTITraceNewDepthStencilDescHandler.Trace(Desc);
		
		std::fstream& fs = MTITrace::Get().BeginWrite();
		MTITraceCommandHandler::Trace(fs, (uintptr_t)Object);
		
		fs << (uintptr_t)Desc;
		fs << (uintptr_t)State;
		
		MTITrace::Get().EndWrite();
		return State;
	}
	
	virtual void Handle(MTITraceCommand& Header, std::fstream& fs)
	{
		uintptr_t Desc;
		fs >> Desc;
		
		uintptr_t Result;
		fs >> Result;
		
		id<MTLDepthStencilState> State = [(id<MTLDevice>)MTITrace::Get().FetchObject(Header.Receiver) newDepthStencilStateWithDescriptor:(MTLDepthStencilDescriptor*)MTITrace::Get().FetchObject(Desc)];
		assert(State);
		
		MTITrace::Get().RegisterObject(Result, State);
	}
};
static MTITraceNewDepthStencilStateHandler GMTITraceNewDepthStencilStateHandler;
id<MTLDepthStencilState> MTIDeviceTrace::NewDepthStencilStateWithDescriptorImpl(id Object, SEL Selector, Super::NewDepthStencilStateWithDescriptorType::DefinedIMP Original, MTLDepthStencilDescriptor* Desc)
{
	return GMTITraceNewDepthStencilStateHandler.Trace(Object, Desc, MTIDepthStencilStateTrace::Register(Original(Object, Selector, Desc)));
}


struct MTITraceNewTextureDescHandler : public MTITraceCommandHandler
{
	MTITraceNewTextureDescHandler()
	: MTITraceCommandHandler("", "newTextureDescriptor")
	{
		
	}
	
	MTLTextureDescriptor* Trace(MTLTextureDescriptor* Desc)
	{
		if (!MTITrace::Get().FetchObject((uintptr_t)Desc))
		{
			std::fstream& fs = MTITrace::Get().BeginWrite();
			MTITraceCommandHandler::Trace(fs, (uintptr_t)Desc);
			
			fs << Desc.textureType;
			fs << Desc.pixelFormat;
			fs << Desc.width;
			fs << Desc.height;
			fs << Desc.depth;
			fs << Desc.mipmapLevelCount;
			fs << Desc.sampleCount;
			fs << Desc.arrayLength;
			fs << Desc.resourceOptions;
			fs << Desc.cpuCacheMode;
			fs << Desc.storageMode;
			fs << Desc.usage;
			fs << Desc.allowGPUOptimizedContents;
			
			MTITrace::Get().RegisterObject((uintptr_t)Desc, Desc);
			MTITrace::Get().EndWrite();
		}
		return Desc;
	}
	
	virtual void Handle(MTITraceCommand& Header, std::fstream& fs)
	{
		uintptr_t Result;
		fs >> Result;
		
		NSUInteger textureType;
		NSUInteger pixelFormat;
		NSUInteger width;
		NSUInteger height;
		NSUInteger depth;
		NSUInteger mipmapLevelCount;
		NSUInteger sampleCount;
		NSUInteger arrayLength;
		NSUInteger resourceOptions;
		NSUInteger cpuCacheMode;
		NSUInteger storageMode;
		NSUInteger usage;
		BOOL allowGPUOptimizedContents;
		
		
		MTLTextureDescriptor* Desc = [MTLTextureDescriptor new];
		fs >> textureType;
		fs >> pixelFormat;
		fs >> width;
		fs >> height;
		fs >> depth;
		fs >> mipmapLevelCount;
		fs >> sampleCount;
		fs >> arrayLength;
		fs >> resourceOptions;
		fs >> cpuCacheMode;
		fs >> storageMode;
		fs >> usage;
		fs >> allowGPUOptimizedContents;
		
		Desc.textureType = (MTLTextureType)textureType;
		Desc.pixelFormat = (MTLPixelFormat)pixelFormat;
		Desc.width = width;
		Desc.height = height;
		Desc.depth = depth;
		Desc.mipmapLevelCount = mipmapLevelCount;
		Desc.sampleCount = sampleCount;
		Desc.arrayLength = arrayLength;
		Desc.resourceOptions = (MTLResourceOptions)resourceOptions;
		Desc.cpuCacheMode = (MTLCPUCacheMode)cpuCacheMode;
		Desc.storageMode = (MTLStorageMode)storageMode;
		Desc.usage = (MTLTextureUsage)usage;
		Desc.allowGPUOptimizedContents = allowGPUOptimizedContents;
		
		MTITrace::Get().RegisterObject(Header.Receiver, Desc);
	}
};
static MTITraceNewTextureDescHandler GMTITraceNewTextureDescHandler;

struct MTITraceNewTextureHandler : public MTITraceCommandHandler
{
	MTITraceNewTextureHandler()
	: MTITraceCommandHandler("MTLDevice", "newTextureWithDescriptor")
	{
		
	}
	
	id<MTLTexture> Trace(id Object, MTLTextureDescriptor* Desc, id<MTLTexture> Texture)
	{
		GMTITraceNewTextureDescHandler.Trace(Desc);
		
		std::fstream& fs = MTITrace::Get().BeginWrite();
		MTITraceCommandHandler::Trace(fs, (uintptr_t)Object);
		
		fs << (uintptr_t)Desc;
		fs << (uintptr_t)Texture;
		
		MTITrace::Get().EndWrite();
		return Texture;
	}
	
	virtual void Handle(MTITraceCommand& Header, std::fstream& fs)
	{
		uintptr_t Desc;
		fs >> Desc;
		
		uintptr_t Result;
		fs >> Result;
		
		id<MTLTexture> Texture = [(id<MTLDevice>)MTITrace::Get().FetchObject(Header.Receiver) newTextureWithDescriptor:(MTLTextureDescriptor*)MTITrace::Get().FetchObject(Desc)];
		assert(Texture);
		
		MTITrace::Get().RegisterObject(Result, Texture);
	}
};
static MTITraceNewTextureHandler GMTITraceNewTextureHandler;

id<MTLTexture> MTIDeviceTrace::NewTextureWithDescriptorImpl(id Object, SEL Selector, Super::NewTextureWithDescriptorType::DefinedIMP Original, MTLTextureDescriptor* Desc)
{
	return GMTITraceNewTextureHandler.Trace(Object, Desc, MTITextureTrace::Register(Original(Object, Selector, Desc)));
}
id<MTLTexture> MTIDeviceTrace::NewTextureWithDescriptorIOSurfaceImpl(id Object, SEL Selector, Super::NewTextureWithDescriptorIOSurfaceType::DefinedIMP Original, MTLTextureDescriptor* Desc, IOSurfaceRef IO, NSUInteger Plane)
{
	return MTITextureTrace::Register(Original(Object, Selector, Desc, IO, Plane));
}


struct MTITraceNewSamplerDescHandler : public MTITraceCommandHandler
{
	MTITraceNewSamplerDescHandler()
	: MTITraceCommandHandler("", "newSampleDescriptor")
	{
		
	}
	
	MTLSamplerDescriptor* Trace(MTLSamplerDescriptor* Desc)
	{
		if (!MTITrace::Get().FetchObject((uintptr_t)Desc))
		{
			std::fstream& fs = MTITrace::Get().BeginWrite();
			MTITraceCommandHandler::Trace(fs, (uintptr_t)Desc);
			
			fs << Desc.minFilter;
			fs << Desc.magFilter;
			fs << Desc.mipFilter;
			fs << Desc.maxAnisotropy;
			fs << Desc.sAddressMode;
			fs << Desc.tAddressMode;
			fs << Desc.rAddressMode;
			fs << Desc.borderColor;
			fs << Desc.normalizedCoordinates;
			fs << Desc.lodMinClamp;
			fs << Desc.lodMaxClamp;
			fs << Desc.compareFunction;
			fs << Desc.supportArgumentBuffers;
			fs << Desc.label ? [Desc.label UTF8String] : "";
			
			MTITrace::Get().RegisterObject((uintptr_t)Desc, Desc);
			MTITrace::Get().EndWrite();
		}
		return Desc;
	}
	
	virtual void Handle(MTITraceCommand& Header, std::fstream& fs)
	{
		uintptr_t Result;
		fs >> Result;
		
		NSUInteger minFilter;
		NSUInteger magFilter;
		NSUInteger mipFilter;
		NSUInteger maxAnisotropy;
		NSUInteger sAddressMode;
		NSUInteger tAddressMode;
		NSUInteger rAddressMode;
		NSUInteger borderColor;
		BOOL normalizedCoordinates;
		float lodMinClamp;
		float lodMaxClamp;
		NSUInteger compareFunction;
		BOOL supportArgumentBuffers;
		std::string label;
		
		
		MTLSamplerDescriptor* Desc = [MTLSamplerDescriptor new];
		fs >> minFilter;
		fs >> magFilter;
		fs >> mipFilter;
		fs >> maxAnisotropy;
		fs >> sAddressMode;
		fs >> tAddressMode;
		fs >> rAddressMode;
		fs >> borderColor;
		fs >> normalizedCoordinates;
		fs >> lodMinClamp;
		fs >> lodMaxClamp;
		fs >> compareFunction;
		fs >> supportArgumentBuffers;
		fs >> label;
		
		Desc.minFilter = (MTLSamplerMinMagFilter)minFilter;
		Desc.magFilter = (MTLSamplerMinMagFilter)magFilter;
		Desc.mipFilter = (MTLSamplerMipFilter)mipFilter;
		Desc.maxAnisotropy = maxAnisotropy;
		Desc.sAddressMode = (MTLSamplerAddressMode)sAddressMode;
		Desc.tAddressMode = (MTLSamplerAddressMode)tAddressMode;
		Desc.rAddressMode = (MTLSamplerAddressMode)rAddressMode;
		Desc.borderColor = (MTLSamplerBorderColor)borderColor;
		Desc.normalizedCoordinates = normalizedCoordinates;
		Desc.lodMinClamp = lodMinClamp;
		Desc.lodMaxClamp = lodMaxClamp;
		Desc.compareFunction = (MTLCompareFunction)compareFunction;
		Desc.supportArgumentBuffers = supportArgumentBuffers;
		Desc.label = [NSString stringWithUTF8String:label.c_str()];
		
		MTITrace::Get().RegisterObject(Header.Receiver, Desc);
	}
};
static MTITraceNewSamplerDescHandler GMTITraceNewSamplerDescHandler;

struct MTITraceNewSamplerStatHandler : public MTITraceCommandHandler
{
	MTITraceNewSamplerStatHandler()
	: MTITraceCommandHandler("MTLDevice", "newSamplerStateWithDescriptor")
	{
		
	}
	
	id<MTLSamplerState> Trace(id Object, MTLSamplerDescriptor* Desc, id<MTLSamplerState> Sampler)
	{
		GMTITraceNewSamplerDescHandler.Trace(Desc);
		
		std::fstream& fs = MTITrace::Get().BeginWrite();
		MTITraceCommandHandler::Trace(fs, (uintptr_t)Object);
		
		fs << (uintptr_t)Desc;
		fs << (uintptr_t)Sampler;
		
		MTITrace::Get().EndWrite();
		return Sampler;
	}
	
	virtual void Handle(MTITraceCommand& Header, std::fstream& fs)
	{
		uintptr_t Desc;
		fs >> Desc;
		
		uintptr_t Result;
		fs >> Result;
		
		id<MTLSamplerState> Sampler = [(id<MTLDevice>)MTITrace::Get().FetchObject(Header.Receiver) newSamplerStateWithDescriptor:(MTLSamplerDescriptor*)MTITrace::Get().FetchObject(Desc)];
		assert(Sampler);
		
		MTITrace::Get().RegisterObject(Result, Sampler);
	}
};
static MTITraceNewSamplerStatHandler GMTITraceNewSamplerStatHandler;


id<MTLSamplerState> MTIDeviceTrace::NewSamplerStateWithDescriptorImpl(id Object, SEL Selector, Super::NewSamplerStateWithDescriptorType::DefinedIMP Original, MTLSamplerDescriptor* Desc)
{
	return GMTITraceNewSamplerStatHandler.Trace(Object, Desc, MTISamplerStateTrace::Register(Original(Object, Selector, Desc)));
}




id<MTLLibrary> MTIDeviceTrace::NewDefaultLibraryImpl(id Object, SEL Selector, Super::NewDefaultLibraryType::DefinedIMP Original)
{
	return MTILibraryTrace::Register(Original(Object, Selector));
}
id<MTLLibrary> MTIDeviceTrace::NewDefaultLibraryWithBundleImpl(id Object, SEL Selector, Super::NewDefaultLibraryWithBundleType::DefinedIMP Original, NSBundle* Bndl, __autoreleasing NSError ** Err)
{
	return MTILibraryTrace::Register(Original(Object, Selector, Bndl, Err));
}


struct MTITraceNewLibraryFromPathHandler : public MTITraceCommandHandler
{
	MTITraceNewLibraryFromPathHandler()
	: MTITraceCommandHandler("MTLDevice", "newLibraryWithFile")
	{
		
	}
	
	id<MTLLibrary> Trace(id Object, NSString* Url, id<MTLLibrary> Lib)
	{
		std::fstream& fs = MTITrace::Get().BeginWrite();
		MTITraceCommandHandler::Trace(fs, (uintptr_t)Object);
		
		fs << [Url UTF8String];
		fs << (uintptr_t)Lib;
		
		MTITrace::Get().EndWrite();
		return Lib;
	}
	
	virtual void Handle(MTITraceCommand& Header, std::fstream& fs)
	{
		std::string URL;
		fs >> URL;
		
		uintptr_t Result;
		fs >> Result;
		
		NSString* newURL = [NSString stringWithUTF8String: URL.c_str()];
		
		id<MTLLibrary> Library = [(id<MTLDevice>)MTITrace::Get().FetchObject(Header.Receiver) newLibraryWithFile:newURL error:nil];
		assert(Library);
		
		MTITrace::Get().RegisterObject(Result, Library);
	}
};
static MTITraceNewLibraryFromPathHandler GMTITraceNewLibraryFromPathHandler;

id<MTLLibrary> MTIDeviceTrace::NewLibraryWithFileImpl(id Object, SEL Selector, Super::NewLibraryWithFileType::DefinedIMP Original, NSString* Str, __autoreleasing NSError ** Err)
{
	return GMTITraceNewLibraryFromPathHandler.Trace(Object, Str, MTILibraryTrace::Register(Original(Object, Selector, Str, Err)));
}


struct MTITraceNewLibraryFromURLHandler : public MTITraceCommandHandler
{
	MTITraceNewLibraryFromURLHandler()
	: MTITraceCommandHandler("MTLDevice", "newLibraryWithURL")
	{
		
	}
	
	id<MTLLibrary> Trace(id Object, NSURL* Url, id<MTLLibrary> Lib)
	{
		std::fstream& fs = MTITrace::Get().BeginWrite();
		MTITraceCommandHandler::Trace(fs, (uintptr_t)Object);
		
		fs << [[Url path] UTF8String];
		fs << (uintptr_t)Lib;
		
		MTITrace::Get().EndWrite();
		return Lib;
	}
	
	virtual void Handle(MTITraceCommand& Header, std::fstream& fs)
	{
		std::string URL;
		fs >> URL;
		
		uintptr_t Result;
		fs >> Result;
		
		NSURL* newURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String: URL.c_str()]];
		
		id<MTLLibrary> Library = [(id<MTLDevice>)MTITrace::Get().FetchObject(Header.Receiver) newLibraryWithURL:newURL error:nil];
		assert(Library);
		
		MTITrace::Get().RegisterObject(Result, Library);
	}
};
static MTITraceNewLibraryFromURLHandler GMTITraceNewLibraryFromURLHandler;

id<MTLLibrary> MTIDeviceTrace::NewLibraryWithURLImpl(id Object, SEL Selector, Super::NewLibraryWithURLType::DefinedIMP Original, NSURL* Url, __autoreleasing NSError ** Err)
{
	return GMTITraceNewLibraryFromURLHandler.Trace(Object, Url, MTILibraryTrace::Register(Original(Object, Selector, Url, Err)));
}

struct MTITraceNewLibraryFromDataHandler : public MTITraceCommandHandler
{
	MTITraceNewLibraryFromDataHandler()
	: MTITraceCommandHandler("MTLDevice", "newLibraryWithData")
	{
		
	}
	
	id<MTLLibrary> Trace(id Object, dispatch_data_t Data, id<MTLLibrary> Lib)
	{
		std::fstream& fs = MTITrace::Get().BeginWrite();
		MTITraceCommandHandler::Trace(fs, (uintptr_t)Object);
		
		MTITraceArray<uint8> Array;
		dispatch_data_t Temp = dispatch_data_create_map(Data,
								 (const void**)&Array.Data,
								 (size_t*)&Array.Length);
		
		fs << Array;
		fs << (uintptr_t)Lib;
		
		dispatch_release(Temp);
		
		MTITrace::Get().EndWrite();
		return Lib;
	}
	
	virtual void Handle(MTITraceCommand& Header, std::fstream& fs)
	{
		MTITraceArray<uint8> Data;
		fs >> Data;
		
		uintptr_t Result;
		fs >> Result;
		
		dispatch_data_t Obj = dispatch_data_create(Data.Backing.data(), Data.Length, nullptr, nullptr);
		
		id<MTLLibrary> Library = [(id<MTLDevice>)MTITrace::Get().FetchObject(Header.Receiver) newLibraryWithData:Obj error:nil];
		assert(Library);
		
		dispatch_release(Obj);
		
		MTITrace::Get().RegisterObject(Result, Library);
	}
};
static MTITraceNewLibraryFromDataHandler GMTITraceNewLibraryFromDataHandler;

id<MTLLibrary> MTIDeviceTrace::NewLibraryWithDataImpl(id Object, SEL Selector, Super::NewLibraryWithDataType::DefinedIMP Original, dispatch_data_t Data, __autoreleasing NSError ** Err)
{
	return GMTITraceNewLibraryFromDataHandler.Trace(Object, Data, MTILibraryTrace::Register(Original(Object, Selector, Data, Err)));
}
id<MTLLibrary> MTIDeviceTrace::NewLibraryWithSourceOptionsErrorImpl(id Object, SEL Selector, Super::NewLibraryWithSourceOptionsErrorType::DefinedIMP Original, NSString* Src, MTLCompileOptions* Opts, NSError** Err)
{
	return MTILibraryTrace::Register(Original(Object, Selector, Src, Opts, Err));
}
void MTIDeviceTrace::NewLibraryWithSourceOptionsCompletionHandlerImpl(id Object, SEL Selector, Super::NewLibraryWithSourceOptionsCompletionHandlerType::DefinedIMP Original, NSString* Src, MTLCompileOptions* Opts, MTLNewLibraryCompletionHandler Handler)
{
	Original(Object, Selector, Src, Opts, ^(id <MTLLibrary> __nullable library, NSError * __nullable error)
	{
		Handler(MTILibraryTrace::Register(library), error);
	});
}
id<MTLRenderPipelineState> MTIDeviceTrace::NewRenderPipelineStateWithDescriptorErrorImpl(id Object, SEL Selector, Super::NewRenderPipelineStateWithDescriptorErrorType::DefinedIMP Original, MTLRenderPipelineDescriptor* Desc, NSError** Err)
{
	return MTIRenderPipelineStateTrace::Register(Original(Object, Selector, Desc, Err));
}
id<MTLRenderPipelineState> MTIDeviceTrace::NewRenderPipelineStateWithDescriptorOptionsReflectionErrorImpl(id Object, SEL Selector, Super::NewRenderPipelineStateWithDescriptorOptionsReflectionErrorType::DefinedIMP Original, MTLRenderPipelineDescriptor* Desc, MTLPipelineOption Opts, MTLAutoreleasedRenderPipelineReflection* Refl, NSError** Err)
{
	return MTIRenderPipelineStateTrace::Register(Original(Object, Selector, Desc, Opts, Refl, Err));
}
void MTIDeviceTrace::NewRenderPipelineStateWithDescriptorCompletionHandlerImpl(id Object, SEL Selector, Super::NewRenderPipelineStateWithDescriptorCompletionHandlerType::DefinedIMP Original, MTLRenderPipelineDescriptor* Desc, MTLNewRenderPipelineStateCompletionHandler Handler)
{
	Original(Object, Selector, Desc, ^(id <MTLRenderPipelineState> __nullable renderPipelineState, NSError * __nullable error)
			 {
				 Handler(MTIRenderPipelineStateTrace::Register(renderPipelineState), error);
			 });
}
void MTIDeviceTrace::NewRenderPipelineStateWithDescriptorOptionsCompletionHandlerImpl(id Object, SEL Selector, Super::NewRenderPipelineStateWithDescriptorOptionsCompletionHandlerType::DefinedIMP Original, MTLRenderPipelineDescriptor* Desc, MTLPipelineOption Opts, MTLNewRenderPipelineStateWithReflectionCompletionHandler Handler)
{
	Original(Object, Selector, Desc, Opts, ^(id <MTLRenderPipelineState> __nullable renderPipelineState, MTLRenderPipelineReflection * __nullable reflection, NSError * __nullable error)
	{
		Handler(MTIRenderPipelineStateTrace::Register(renderPipelineState), reflection, error);
	});
}
id<MTLComputePipelineState> MTIDeviceTrace::NewComputePipelineStateWithFunctionErrorImpl(id Object, SEL Selector, Super::NewComputePipelineStateWithFunctionErrorType::DefinedIMP Original, id<MTLFunction> Func, NSError** Err)
{
	return MTIComputePipelineStateTrace::Register(Original(Object, Selector, Func, Err));
}
id<MTLComputePipelineState> MTIDeviceTrace::NewComputePipelineStateWithFunctionOptionsReflectionErrorImpl(id Object, SEL Selector, Super::NewComputePipelineStateWithFunctionOptionsReflectionErrorType::DefinedIMP Original, id<MTLFunction> Func, MTLPipelineOption Opts, MTLAutoreleasedComputePipelineReflection * Refl, NSError** Err)
{
	return MTIComputePipelineStateTrace::Register(Original(Object, Selector, Func, Opts, Refl, Err));
}
void MTIDeviceTrace::NewComputePipelineStateWithFunctionCompletionHandlerImpl(id Object, SEL Selector, Super::NewComputePipelineStateWithFunctionCompletionHandlerType::DefinedIMP Original, id<MTLFunction>  Func, MTLNewComputePipelineStateCompletionHandler Handler)
{
	Original(Object, Selector, Func, ^(id <MTLComputePipelineState> __nullable computePipelineState, NSError * __nullable error){
		Handler(MTIComputePipelineStateTrace::Register(computePipelineState ), error);
	});
}
void MTIDeviceTrace::NewComputePipelineStateWithFunctionOptionsCompletionHandlerImpl(id Object, SEL Selector, Super::NewComputePipelineStateWithFunctionOptionsCompletionHandlerType::DefinedIMP Original, id<MTLFunction> Func, MTLPipelineOption Opts, MTLNewComputePipelineStateWithReflectionCompletionHandler Handler)
{
	Original(Object, Selector, Func, Opts, ^(id <MTLComputePipelineState> __nullable computePipelineState, MTLComputePipelineReflection * __nullable reflection, NSError * __nullable error){
		Handler(MTIComputePipelineStateTrace::Register(computePipelineState ), reflection, error);
	});
}
id<MTLComputePipelineState> MTIDeviceTrace::NewComputePipelineStateWithDescriptorOptionsReflectionErrorImpl(id Object, SEL Selector, Super::NewComputePipelineStateWithDescriptorOptionsReflectionErrorType::DefinedIMP Original, MTLComputePipelineDescriptor* Desc, MTLPipelineOption Opts, MTLAutoreleasedComputePipelineReflection * Refl, NSError** Err)
{
	return MTIComputePipelineStateTrace::Register(Original(Object, Selector, Desc, Opts, Refl, Err));
}
void MTIDeviceTrace::NewComputePipelineStateWithDescriptorOptionsCompletionHandlerImpl(id Object, SEL Selector, Super::NewComputePipelineStateWithDescriptorOptionsCompletionHandlerType::DefinedIMP Original, MTLComputePipelineDescriptor* Desc, MTLPipelineOption Opts, MTLNewComputePipelineStateWithReflectionCompletionHandler Handler)
{
	Original(Object, Selector, Desc, Opts, ^(id <MTLComputePipelineState> __nullable computePipelineState, MTLComputePipelineReflection * __nullable reflection, NSError * __nullable error){
		Handler(MTIComputePipelineStateTrace::Register(computePipelineState ), reflection, error);
	});
}




struct MTITraceNewFenceHandler : public MTITraceCommandHandler
{
	MTITraceNewFenceHandler()
	: MTITraceCommandHandler("MTLDevice", "newFence")
	{
		
	}
	
	id<MTLFence> Trace(id Object, id<MTLFence> Fence)
	{
		std::fstream& fs = MTITrace::Get().BeginWrite();
		MTITraceCommandHandler::Trace(fs, (uintptr_t)Object);
		
		fs << (uintptr_t)Fence;
		
		MTITrace::Get().EndWrite();
		return Fence;
	}
	
	virtual void Handle(MTITraceCommand& Header, std::fstream& fs)
	{
		uintptr_t Result;
		fs >> Result;
		
		id<MTLFence> Fence = [(id<MTLDevice>)MTITrace::Get().FetchObject(Header.Receiver) newFence];
		assert(Fence);
		
		MTITrace::Get().RegisterObject(Result, Fence);
	}
};
static MTITraceNewFenceHandler GMTITraceNewFenceHandler;
id<MTLFence> MTIDeviceTrace::NewFenceImpl(id Object, SEL Selector, Super::NewFenceType::DefinedIMP Original)
{
	return GMTITraceNewFenceHandler.Trace(Object, MTIFenceTrace::Register(Original(Object, Selector)));
}


id<MTLArgumentEncoder> MTIDeviceTrace::NewArgumentEncoderWithArgumentsImpl(id Object, SEL Selector, Super::NewArgumentEncoderWithArgumentsType::DefinedIMP Original, NSArray <MTLArgumentDescriptor *> * Args)
{
	return MTIArgumentEncoderTrace::Register(Original(Object, Selector, Args));
}
INTERPOSE_DEFINITION(MTIDeviceTrace, getDefaultSamplePositionscount, void, MTLPPSamplePosition* s, NSUInteger c)
{
	Original(Obj, Cmd, s, c);
}

INTERPOSE_DEFINITION(MTIDeviceTrace, newRenderPipelineStateWithTileDescriptoroptionsreflectionerror, id <MTLRenderPipelineState>, MTLTileRenderPipelineDescriptor* d, MTLPipelineOption o, MTLAutoreleasedRenderPipelineReflection* r, NSError** e)
{
	return Original(Obj, Cmd, d, o, r, e);
}

INTERPOSE_DEFINITION(MTIDeviceTrace,  newRenderPipelineStateWithTileDescriptoroptionscompletionHandler, void, MTLTileRenderPipelineDescriptor* d, MTLPipelineOption o, MTLNewRenderPipelineStateWithReflectionCompletionHandler h)
{
	Original(Obj, Cmd, d, o, h);
}

MTLPP_END
