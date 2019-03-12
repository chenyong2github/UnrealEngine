// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#import <Metal/Metal.h>
#include "MTITexture.hpp"
#include "MTITrace.hpp"


MTLPP_BEGIN

INTERPOSE_PROTOCOL_REGISTER(MTITextureTrace, id<MTLTexture>);

INTERPOSE_DEFINITION_VOID(MTITextureTrace, Rootresource, id <MTLResource>)
{
	return Original(Obj, Cmd );
}

INTERPOSE_DEFINITION_VOID(MTITextureTrace, Parenttexture, id <MTLTexture>)
{
	return Original(Obj, Cmd );
}

INTERPOSE_DEFINITION_VOID(MTITextureTrace, Parentrelativelevel, NSUInteger)
{
	return Original(Obj, Cmd );
}

INTERPOSE_DEFINITION_VOID(MTITextureTrace, Parentrelativeslice, NSUInteger)
{
	return Original(Obj, Cmd );
}

INTERPOSE_DEFINITION_VOID(MTITextureTrace, Buffer, id <MTLBuffer>)
{
	return Original(Obj, Cmd );
}

INTERPOSE_DEFINITION_VOID(MTITextureTrace, Bufferoffset, NSUInteger)
{
	return Original(Obj, Cmd );
}

INTERPOSE_DEFINITION_VOID(MTITextureTrace, Bufferbytesperrow, NSUInteger)
{
	return Original(Obj, Cmd );
}

INTERPOSE_DEFINITION_VOID(MTITextureTrace, Iosurface, IOSurfaceRef)
{
	return Original(Obj, Cmd );
}

INTERPOSE_DEFINITION_VOID(MTITextureTrace, Iosurfaceplane, NSUInteger)
{
	return Original(Obj, Cmd );
}

INTERPOSE_DEFINITION_VOID(MTITextureTrace, Texturetype, MTLTextureType)
{
	return Original(Obj, Cmd );
}

INTERPOSE_DEFINITION_VOID(MTITextureTrace, Pixelformat, MTLPixelFormat)
{
	return Original(Obj, Cmd );
}

INTERPOSE_DEFINITION_VOID(MTITextureTrace, Width, NSUInteger)
{
	return Original(Obj, Cmd );
}

INTERPOSE_DEFINITION_VOID(MTITextureTrace, Height, NSUInteger)
{
	return Original(Obj, Cmd );
}

INTERPOSE_DEFINITION_VOID(MTITextureTrace, Depth, NSUInteger)
{
	return Original(Obj, Cmd );
}

INTERPOSE_DEFINITION_VOID(MTITextureTrace, Mipmaplevelcount, NSUInteger)
{
	return Original(Obj, Cmd );
}

INTERPOSE_DEFINITION_VOID(MTITextureTrace, Samplecount, NSUInteger)
{
	return Original(Obj, Cmd );
}

INTERPOSE_DEFINITION_VOID(MTITextureTrace, Arraylength, NSUInteger)
{
	return Original(Obj, Cmd );
}

INTERPOSE_DEFINITION_VOID(MTITextureTrace, Usage, MTLTextureUsage)
{
	return Original(Obj, Cmd );
}

INTERPOSE_DEFINITION_VOID(MTITextureTrace, Isframebufferonly, BOOL)
{
	return Original(Obj, Cmd );
}

INTERPOSE_DEFINITION(MTITextureTrace, Getbytesbytesperrowbytesperimagefromregionmipmaplevelslice, void, void * pixelBytes , NSUInteger bytesPerRow , NSUInteger bytesPerImage , MTLPPRegion region , NSUInteger level , NSUInteger slice)
{
	Original(Obj, Cmd, pixelBytes, bytesPerRow, bytesPerImage, region, level, slice);
}

INTERPOSE_DEFINITION(MTITextureTrace, Replaceregionmipmaplevelslicewithbytesbytesperrowbytesperimage, void, MTLPPRegion region , NSUInteger level , NSUInteger slice , const void * pixelBytes , NSUInteger bytesPerRow , NSUInteger bytesPerImage)
{
	Original(Obj, Cmd, region, level, slice, pixelBytes, bytesPerRow, bytesPerImage);
}

INTERPOSE_DEFINITION(MTITextureTrace, Getbytesbytesperrowfromregionmipmaplevel, void, void * pixelBytes , NSUInteger bytesPerRow , MTLPPRegion region , NSUInteger level)
{
	Original(Obj, Cmd, pixelBytes, bytesPerRow, region, level);
}

INTERPOSE_DEFINITION(MTITextureTrace, Replaceregionmipmaplevelwithbytesbytesperrow, void, MTLPPRegion region , NSUInteger level , const void * pixelBytes , NSUInteger bytesPerRow)
{
	Original(Obj, Cmd, region, level, pixelBytes, bytesPerRow);
}

struct MTITraceTextureNewTextureViewHandler : public MTITraceCommandHandler
{
	MTITraceTextureNewTextureViewHandler()
	: MTITraceCommandHandler("MTLTexture", "newTextureViewWithPixelFormat")
	{
		
	}
	
	id<MTLTexture> Trace(id Object, MTLPixelFormat pixelFormat, id<MTLTexture> Texture)
	{
		std::fstream& fs = MTITrace::Get().BeginWrite();
		MTITraceCommandHandler::Trace(fs, (uintptr_t)Object);
		
		fs << pixelFormat;
		
		MTITrace::Get().EndWrite();
		return Texture;
	}
	
	virtual void Handle(MTITraceCommand& Header, std::fstream& fs)
	{
		NSUInteger pixelFormat;
		fs >> pixelFormat;
		
		uintptr_t Result;
		fs >> Result;
		
		id<MTLTexture> Texture = [(id<MTLTexture>)MTITrace::Get().FetchObject(Header.Receiver) newTextureViewWithPixelFormat:(MTLPixelFormat)pixelFormat];
		assert(Texture);
		
		MTITrace::Get().RegisterObject(Result, Texture);
	}
};
static MTITraceTextureNewTextureViewHandler GMTITraceTextureNewTextureViewHandler;
INTERPOSE_DEFINITION(MTITextureTrace, Newtextureviewwithpixelformat, id<MTLTexture>, MTLPixelFormat pixelFormat)
{
	return GMTITraceTextureNewTextureViewHandler.Trace(Obj, pixelFormat, MTITextureTrace::Register(Original(Obj, Cmd, pixelFormat)));
}

struct MTITraceTextureNewTextureViewTypeHandler : public MTITraceCommandHandler
{
	MTITraceTextureNewTextureViewTypeHandler()
	: MTITraceCommandHandler("MTLTexture", "newTextureViewWithPixelFormatAndType")
	{
		
	}
	
	id<MTLTexture> Trace(id Object, MTLPixelFormat pixelFormat , MTLTextureType textureType , NSRange levelRange , NSRange sliceRange, id<MTLTexture> Texture)
	{
		std::fstream& fs = MTITrace::Get().BeginWrite();
		MTITraceCommandHandler::Trace(fs, (uintptr_t)Object);
		
		fs << pixelFormat;
		fs << textureType;
		fs << levelRange.location;
		fs << levelRange.length;
		fs << sliceRange.location;
		fs << sliceRange.length;
		fs << (uintptr_t)Texture;
		
		MTITrace::Get().EndWrite();
		return Texture;
	}
	
	virtual void Handle(MTITraceCommand& Header, std::fstream& fs)
	{
		NSUInteger pixelFormat;
		NSUInteger textureType;
		NSRange levelRange;
		NSRange sliceRange;
		fs >> pixelFormat;
		fs >> textureType;
		fs >> levelRange.location;
		fs >> levelRange.length;
		fs >> sliceRange.location;
		fs >> sliceRange.length;
		
		uintptr_t Result;
		fs >> Result;
		
		id<MTLTexture> Texture = [(id<MTLTexture>)MTITrace::Get().FetchObject(Header.Receiver) newTextureViewWithPixelFormat:(MTLPixelFormat)pixelFormat textureType:(MTLTextureType)textureType levels:(NSRange)levelRange slices:(NSRange)sliceRange];
		assert(Texture);
		
		MTITrace::Get().RegisterObject(Result, Texture);
	}
};
static MTITraceTextureNewTextureViewTypeHandler GMTITraceTextureNewTextureViewTypeHandler;
INTERPOSE_DEFINITION(MTITextureTrace, Newtextureviewwithpixelformattexturetypelevelsslices, id<MTLTexture>, MTLPixelFormat pixelFormat , MTLTextureType textureType , NSRange levelRange , NSRange sliceRange)
{
	return GMTITraceTextureNewTextureViewTypeHandler.Trace(Obj, pixelFormat, textureType, levelRange, sliceRange, MTITextureTrace::Register(Original(Obj, Cmd, pixelFormat, textureType, levelRange, sliceRange)));
}

MTITraceNewTextureDescHandler::MTITraceNewTextureDescHandler::MTITraceNewTextureDescHandler()
: MTITraceCommandHandler("", "newTextureDescriptor")
{
	
}

MTLTextureDescriptor* MTITraceNewTextureDescHandler::MTITraceNewTextureDescHandler::Trace(MTLTextureDescriptor* Desc)
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

void MTITraceNewTextureDescHandler::MTITraceNewTextureDescHandler::Handle(MTITraceCommand& Header, std::fstream& fs)
{
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
MTITraceNewTextureDescHandler GMTITraceNewTextureDescHandler;

MTLPP_END

