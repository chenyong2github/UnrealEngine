// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "Logging/LogMacros.h"
#include "RHIStaticStates.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/ThreadSafeCounter.h"
#include "HAL/Thread.h"
#include "HAL/Event.h"
#include "Misc/ScopeExit.h"
#include "ShaderCore.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "RHI.h"
#include "RHIResources.h"
#include "ScreenRendering.h"
#include "CommonRenderResources.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAVEncoder, Log, All);
CSV_DECLARE_CATEGORY_EXTERN(AVEncoder);

namespace AVEncoder
{

template<typename F>
inline void ExecuteRHICommand(F&& Functor)
{
	FRHICommandList& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	if (RHICmdList.Bypass())
	{
		Functor();
	}
	else
	{
		FRHICOMMAND_MACRO(FLocalRHICommand)
		{
			F Functor;

			FLocalRHICommand(F InFunctor)
				: Functor(MoveTemp(InFunctor))
			{}

			void Execute(FRHICommandListBase& CmdList)
			{
				Functor();
			}
		};

		new (RHICmdList.AllocCommand<FLocalRHICommand>()) FLocalRHICommand(Functor);
	}
}

// Settings specific to  
struct FH264Settings
{
	enum ERateControlMode
	{
		ConstQP,
		VBR,
		CBR
	};

	int QP = 20;
	ERateControlMode RcMode = ERateControlMode::CBR;
};

bool ReadH264Setting(const FString& Name, const FString& Value, FH264Settings& OutSettings);
void ReadH264Settings(const TArray<TPair<FString, FString>>& Options, FH264Settings& OutSettings);

void CopyTextureImpl(const FTexture2DRHIRef& Src, FTexture2DRHIRef& Dst, FRHIGPUFence* GPUFence);

}


