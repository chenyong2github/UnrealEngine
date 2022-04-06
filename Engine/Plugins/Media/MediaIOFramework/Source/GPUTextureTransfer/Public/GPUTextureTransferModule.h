// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GPUTextureTransfer.h"
#include "Logging/LogMacros.h"
#include "Modules/ModuleInterface.h"

DECLARE_LOG_CATEGORY_EXTERN(LogGPUTextureTransfer, Log, All);

class GPUTEXTURETRANSFER_API FGPUTextureTransferModule : public IModuleInterface
{
public:
	using TextureTransferPtr = TSharedPtr<UE::GPUTextureTransfer::ITextureTransfer>;
	
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	TextureTransferPtr GetTextureTransfer();

	static bool IsAvailable();
	static FGPUTextureTransferModule& Get();

private:
	bool LoadGPUDirectBinary();
	void InitializeTextureTransfer();
	void UninitializeTextureTransfer();

private:
	static constexpr uint8 RHI_MAX = static_cast<uint8>(UE::GPUTextureTransfer::ERHI::RHI_MAX);
	void* TextureTransferHandle = nullptr;
	bool bIsGPUTextureTransferAvailable = false;
	
	TArray<TextureTransferPtr> TransferObjects;
};
