// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class MAGICLEAPHELPEROPENGL_API FExternalTextureRendererGL
{
public:
	FExternalTextureRendererGL();
	virtual ~FExternalTextureRendererGL();

	bool CreateImageKHR(void* NativeBufferHandle);
	void DestroyImageKHR();
	void BindImageKHRToTexture(int32 TextureID);

#if PLATFORM_LUMIN
private:
	class FMagicLeapExternalTextureRendererGLImpl* Impl;
#endif //PLATFORM_LUMIN
};
