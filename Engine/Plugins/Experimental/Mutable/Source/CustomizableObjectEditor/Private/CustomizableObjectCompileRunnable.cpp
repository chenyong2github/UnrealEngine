// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomizableObjectCompileRunnable.h"
#include "CustomizableObjectCompiler.h"
#include "CustomizableObjectEditorModule.h"
#include "CustomizableObjectSystem.h"
#include "CustomizableObject.h"
#include "UnrealMutableModelDiskStreamer.h"

#include "StaticMeshResources.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "MessageLogModule.h"
#include "Logging/MessageLog.h"
#include "Application/ThrottleManager.h"
#include "HAL/Runnable.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "HAL/RunnableThread.h"
#include "MeshUtilities.h"
#include "Misc/ConfigCacheIni.h"

#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"

#include "UnrealEditorPortabilityHelpers.h"
#include "UnrealPortabilityHelpers.h"
#include "PlatformInfo.h"
#include "Engine/AssetManager.h"
#include "AssetRegistry/ARFilter.h"

#include "MutableTools/Public/Compiler.h"
#include "MutableTools/Public/ErrorLog.h"
#include "MutableRuntime/Public/System.h"
#include "MutableRuntime/Public/Ptr.h"


#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


FCustomizableObjectCompileRunnable::FCustomizableObjectCompileRunnable(mu::NodePtr Root, bool bInDisableTextureLayout)
	: MutableRoot(Root)
	, bDisableTextureLayout(bInDisableTextureLayout)
	, bThreadCompleted(false)
	, MutableIsDisabled(false)
{
}


uint32 FCustomizableObjectCompileRunnable::Run()
{
	UE_LOG(LogMutable, Verbose, TEXT("PROFILE: [ %16.8f ] FCustomizableObjectCompileRunnable::Run start."), FPlatformTime::Seconds());

	uint32 Result = 1;
	ErrorMsg = FString();

	if (MutableIsDisabled)
	{
		bThreadCompleted = true;
		UE_LOG(LogMutable, Verbose, TEXT("PROFILE: [ %16.8f ] FCustomizableObjectCompileRunnable::Run end. NOTE: Mutable compile is deactivated in Editor. To reactivate it, go to Project Settings -> Plugins -> Mutable and unmark the option Disable Mutable Compile In Editor"), FPlatformTime::Seconds());
		return true;
	}

	mu::CompilerOptionsPtr CompilerOptions = new mu::CompilerOptions();

	CompilerOptions->SetUseDiskCache(Options.bUseDiskCompilation);

	if (Options.OptimizationLevel > 3)
	{
		UE_LOG(LogMutable, Verbose, TEXT("Mutable compile optimization level out of range. Clamping to maximum."));
		Options.OptimizationLevel = 3;
	}

	switch (Options.OptimizationLevel)
	{
	case 0:
		CompilerOptions->SetOptimisationEnabled(false);
		CompilerOptions->SetConstReductionEnabled(false);
		CompilerOptions->SetOptimisationMaxIteration(1);
		break;

	case 1:
		CompilerOptions->SetOptimisationEnabled(false);
		CompilerOptions->SetConstReductionEnabled(true);
		CompilerOptions->SetOptimisationMaxIteration(1);
		break;

	case 2:
		CompilerOptions->SetOptimisationEnabled(true);
		CompilerOptions->SetConstReductionEnabled(true);
		CompilerOptions->SetOptimisationMaxIteration(16);
		break;

	case 3:
		CompilerOptions->SetOptimisationEnabled(true);
		CompilerOptions->SetConstReductionEnabled(true);
		CompilerOptions->SetOptimisationMaxIteration(0);
		break;

	default:
		CompilerOptions->SetOptimisationEnabled(false);
		CompilerOptions->SetConstReductionEnabled(true);
		CompilerOptions->SetOptimisationMaxIteration(1);
		break;
	}

	// Minimum resident mip count.
	const int MinResidentMips = UTexture::GetStaticMinTextureResidentMipCount();
	// Data smaller than this will always be loaded, as part of the customizable object compiled model.
	const int MinRomSize = 128;
	CompilerOptions->SetDataPackingStrategy(MinRomSize, MinResidentMips);

	CompilerOptions->SetTextureLayoutStrategy(bDisableTextureLayout
		? mu::CompilerOptions::TextureLayoutStrategy::None
		: mu::CompilerOptions::TextureLayoutStrategy::Pack);

	// \TODO For now force it to be disabled.
	//CompilerOptions->SetEnableConcurrency(Options.bUseParallelCompilation);
	CompilerOptions->SetEnableConcurrency(false);

	mu::CompilerPtr Compiler = new mu::Compiler(CompilerOptions);

	UE_LOG(LogMutable, Verbose, TEXT("PROFILE: [ %16.8f ] FCustomizableObjectCompileRunnable Compile start."), FPlatformTime::Seconds());
	Model = Compiler->Compile(MutableRoot);

	// Dump all the log messages from the compiler
	mu::Ptr<const mu::ErrorLog> pLog = Compiler->GetLog();
	for (int i = 0; i < pLog->GetMessageCount(); ++i)
	{
		const char* strMessage = pLog->GetMessageText(i);
		mu::ErrorLogMessageType MessageType = pLog->GetMessageType(i);
		mu::ErrorLogMessageAttachedDataView MessageAttachedData = pLog->GetMessageAttachedData(i);

		if (MessageType == mu::ELMT_WARNING)
		{
			if (MessageAttachedData.m_unassignedUVs && MessageAttachedData.m_unassignedUVsSize > 0) 
			{			
				TSharedPtr<FErrorAttachedData> ErrorAttachedData = MakeShared<FErrorAttachedData>();
				ErrorAttachedData->UnassignedUVs.Reset();
				ErrorAttachedData->UnassignedUVs.Append(MessageAttachedData.m_unassignedUVs, MessageAttachedData.m_unassignedUVsSize);
				ArrayWarning.Add(FError(FText::AsCultureInvariant(strMessage), ErrorAttachedData, pLog->GetMessageContext(i)));
			}
			else
			{
				ArrayWarning.Add(FError(FText::AsCultureInvariant(strMessage), pLog->GetMessageContext(i)));
			}
		}
		else if (MessageType == mu::ELMT_ERROR)
		{
			if (MessageAttachedData.m_unassignedUVs && MessageAttachedData.m_unassignedUVsSize > 0) 
			{			
				TSharedPtr<FErrorAttachedData> ErrorAttachedData = MakeShared<FErrorAttachedData>();
				ErrorAttachedData->UnassignedUVs.Reset();
				ErrorAttachedData->UnassignedUVs.Append(MessageAttachedData.m_unassignedUVs, MessageAttachedData.m_unassignedUVsSize);
				ArrayError.Add(FError(FText::AsCultureInvariant(strMessage), ErrorAttachedData, pLog->GetMessageContext(i)));
			}
			else
			{
				ArrayError.Add(FError(FText::AsCultureInvariant(strMessage), pLog->GetMessageContext(i)));
			}
		}
	}

	Compiler = nullptr;

	bThreadCompleted = true;

	UE_LOG(LogMutable, Verbose, TEXT("PROFILE: [ %16.8f ] FCustomizableObjectCompileRunnable::Run end."), FPlatformTime::Seconds());

	return Result;
}


bool FCustomizableObjectCompileRunnable::IsCompleted() const
{
	return bThreadCompleted;
}


const TArray<FCustomizableObjectCompileRunnable::FError>& FCustomizableObjectCompileRunnable::GetArrayError() const
{
	return ArrayError;
}


const TArray<FCustomizableObjectCompileRunnable::FError>& FCustomizableObjectCompileRunnable::GetArrayWarning() const
{
	return ArrayWarning;
}


FCustomizableObjectSaveDDRunnable::FCustomizableObjectSaveDDRunnable(UCustomizableObject* CustomizableObject, const FCompilationOptions& InOptions)
{
	Model = CustomizableObject->GetModel();
	Options = InOptions;
	
	CustomizableObjectHeader.InternalVersion = CustomizableObject->GetCurrentSupportedVersion();
	CustomizableObjectHeader.VersionId = Options.bIsCooking? FGuid::NewGuid() : CustomizableObject->GetVersionId();

	if (!Options.bIsCooking || Options.bSaveCookedDataToDisk)
	{
		// We will be saving all compilation data in two separate files, write CO Data
		FolderPath = CustomizableObject->GetCompiledDataFolderPath(!InOptions.bIsCooking);
		CompildeDataFullFileName = FolderPath + CustomizableObject->GetCompiledDataFileName(true, InOptions.TargetPlatform);
		StreamableDataFullFileName = FolderPath + CustomizableObject->GetCompiledDataFileName(false, InOptions.TargetPlatform);

		// Serialize Customizable Object's data
		FMemoryWriter64 MemoryWriter(Bytes);
		CustomizableObject->SaveCompiledData(MemoryWriter, Options.bIsCooking);
	}
}


uint32 FCustomizableObjectSaveDDRunnable::Run()
{
	bool bModelSerialized = Model.get() != nullptr;

	if (Options.bIsCooking && !Options.bSaveCookedDataToDisk)
	{
		// Serialize mu::Model and streamable resources 
		FMemoryWriter64 ModelMemoryWriter(Bytes, false, true);
		FMemoryWriter64 StreamableMemoryWriter(BulkDataBytes, false, true);

		ModelMemoryWriter << bModelSerialized;
		if (bModelSerialized)
		{
			FUnrealMutableModelBulkStreamer Streamer(&ModelMemoryWriter, &StreamableMemoryWriter);
			mu::Model::Serialise(Model.get(), Streamer);
		}
	}
	else if(bModelSerialized) // Save CO data + mu::Model and streamable resources to disk
	{
		// Create folder...
		IFileManager& FileManager = IFileManager::Get();
		FileManager.MakeDirectory(*FolderPath, true);

		// Delete files...
		FileManager.Delete(*CompildeDataFullFileName, true, false, true);
		FileManager.Delete(*StreamableDataFullFileName, true, false, true);

		// Create file writers...
		FArchive* ModelMemoryWriter = FileManager.CreateFileWriter(*CompildeDataFullFileName);
		FArchive* StreamableMemoryWriter = FileManager.CreateFileWriter(*StreamableDataFullFileName);

		// Serailize headers to validate data
		*ModelMemoryWriter << CustomizableObjectHeader;
		*StreamableMemoryWriter << CustomizableObjectHeader;

		// Serialize Customizable Object's Data to disk
		ModelMemoryWriter->Serialize(reinterpret_cast<void*>(Bytes.GetData()), Bytes.Num() * sizeof(uint8));
		Bytes.Empty();

		// Serialize mu::Model and streamable resources
		*ModelMemoryWriter << bModelSerialized;

		FUnrealMutableModelBulkStreamer Streamer(ModelMemoryWriter, StreamableMemoryWriter);
		mu::Model::Serialise(Model.get(), Streamer);

		// Save to disk
		ModelMemoryWriter->Flush();
		StreamableMemoryWriter->Flush();

		ModelMemoryWriter->Close();
		StreamableMemoryWriter->Close();

		delete ModelMemoryWriter;
		delete StreamableMemoryWriter;
	}

	bThreadCompleted = true;

	return 1;
}


TArray64<uint8>& FCustomizableObjectSaveDDRunnable::GetModelBytes()
{
	return Bytes;
}


TArray64<uint8>& FCustomizableObjectSaveDDRunnable::GetBulkBytes()
{
	return BulkDataBytes;
}


bool FCustomizableObjectSaveDDRunnable::IsCompleted() const
{
	return bThreadCompleted;
}


const ITargetPlatform* FCustomizableObjectSaveDDRunnable::GetTargetPlatform() const
{
	return Options.TargetPlatform;
}

#undef LOCTEXT_NAMESPACE
