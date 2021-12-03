// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderGraph.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/WildcardString.h"
#include "BuildSettings.h"
#include "Serialization/JsonTypes.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if RDG_DUMP_RESOURCES

namespace
{

static TAutoConsoleVariable<FString> GDumpGPURootCVar(
	TEXT("r.DumpGPU.Root"),
	TEXT("*"),
	TEXT("Allows to filter the tree when using r.DumpGPU command, the pattern match is case sensitive."),
	ECVF_Default);

BEGIN_SHADER_PARAMETER_STRUCT(FDumpTexturePass, )
	RDG_TEXTURE_ACCESS(Texture, ERHIAccess::CopySrc)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FDumpBufferPass, )
	RDG_BUFFER_ACCESS(Buffer, ERHIAccess::CopySrc)
END_SHADER_PARAMETER_STRUCT()

struct FRDGResourceDumpContext
{
	FString DumpingDirectoryPath;
	FDateTime Time;
	int32 ResourcesDumped = 0;
	int32 PassesCount = 0;
	TMap<const FRDGResource*, const FRDGPass*> LastResourceVersion;

	bool bShowInExplore = false;

	bool IsDumpingFrame() const
	{
		check(IsInRenderingThread());
		return !DumpingDirectoryPath.IsEmpty();
	}

	static FString GetResourceName(const FRDGResource* Resource)
	{
		return FString::Printf(TEXT("%s.%p"), Resource->Name, Resource);
	}

	static FString GetResourceVersionDumpName(const FRDGPass* Pass, const FRDGResource* Resource)
	{
		return FString::Printf(TEXT("%s.%p.v%p.bin"), Resource->Name, Resource, Pass);
	}

	FString GetResourceDumpDirectory() const
	{
		return DumpingDirectoryPath + TEXT("Resources/");
	}

	void GetResourceDumpInfo(
		const FRDGPass* Pass,
		const FRDGResource* Resource,
		bool bIsOutputResource,
		bool* bOutDumpResourceInfos,
		FString* OutResourceVersionDumpName)
	{
		*bOutDumpResourceInfos = false;

		if (bIsOutputResource)
		{
			*OutResourceVersionDumpName = GetResourceVersionDumpName(Pass, Resource);
		}

		if (!LastResourceVersion.Contains(Resource))
		{
			// First time we ever see this resource, so dump it's info to disk
			*bOutDumpResourceInfos = true;

			// If not an output, it might be a resource undumped by r.DumpGPU.Root or external texture so still dump it as v0.
			if (!bIsOutputResource)
			{
				*OutResourceVersionDumpName = GetResourceVersionDumpName(/* Pass = */ nullptr, Resource);
			}

			if (LastResourceVersion.Num() % 1024 == 0)
			{
				LastResourceVersion.Reserve(LastResourceVersion.Num() + 1024);
			}
			LastResourceVersion.Add(Resource, Pass);
		}
		else
		{
			LastResourceVersion[Resource] = Pass;
		}
	}

	void AddDumpTexturePass(
		FRDGBuilder& GraphBuilder,
		TArray<TSharedPtr<FJsonValue>>& InputResourceNames,
		TArray<TSharedPtr<FJsonValue>>& OutputResourceNames,
		const FRDGPass* Pass,
		FRDGTexture* Texture,
		bool bIsOutputResource)
	{
		FString UniqueResourceName = GetResourceName(Texture);

		if (bIsOutputResource)
		{
			OutputResourceNames.AddUnique(MakeShareable(new FJsonValueString(UniqueResourceName)));
		}
		else
		{
			InputResourceNames.AddUnique(MakeShareable(new FJsonValueString(UniqueResourceName)));
		}

		const FRDGTextureDesc& Desc = Texture->Desc;
		int32 ByteSize = 0;
		{
			bool bIsUnsupported = false;

			if (GPixelFormats[Desc.Format].BlockSizeX != 1 || 
				GPixelFormats[Desc.Format].BlockSizeY != 1 ||
				GPixelFormats[Desc.Format].BlockSizeZ != 1)
			{
				bIsUnsupported = true;
			}

			if (Desc.Format == PF_DepthStencil)
			{
				bIsUnsupported = true;
			}

			if (!bIsUnsupported && Desc.IsTexture2D() && !Desc.IsMultisample() && !Desc.IsTextureArray())
			{
				ByteSize = Desc.Extent.X * Desc.Extent.Y * GPixelFormats[Desc.Format].BlockBytes;
			}
		}

		bool bDumpResourceInfos;
		FString ResourceVersionDumpName;
		GetResourceDumpInfo(Pass, Texture, bIsOutputResource, &bDumpResourceInfos, &ResourceVersionDumpName);

		FString ResourceDumpDirectory = GetResourceDumpDirectory();

		// Dump the information of the texture to json file.
		if (bDumpResourceInfos)
		{
			FString PixelFormat = FString::Printf(TEXT("PF_%s"), GPixelFormats[Desc.Format].Name);

			TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
			JsonObject->SetStringField(TEXT("Name"), Texture->Name);
			JsonObject->SetNumberField(TEXT("ByteSize"), ByteSize);
			JsonObject->SetStringField(TEXT("Type"), GetTextureDimensionString(Desc.Dimension));
			JsonObject->SetStringField(TEXT("Format"), *PixelFormat);
			JsonObject->SetNumberField(TEXT("ExtentX"), Desc.Extent.X);
			JsonObject->SetNumberField(TEXT("ExtentY"), Desc.Extent.Y);
			JsonObject->SetNumberField(TEXT("Depth"), Desc.Depth);
			JsonObject->SetNumberField(TEXT("ArraySize"), Desc.ArraySize);
			JsonObject->SetNumberField(TEXT("NumMips"), Desc.NumMips);
			JsonObject->SetNumberField(TEXT("NumSamples"), Desc.NumSamples);

			FString OutputString;
			TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutputString);
			FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

			FString ResourceInfoJsonPath = ResourceDumpDirectory + UniqueResourceName + TEXT(".json");
			FFileHelper::SaveStringToFile(OutputString, *ResourceInfoJsonPath);
		}

		if (ByteSize == 0)
		{
			return;
		}

		// Dump the resource's binary to a .bin file.
		if (!ResourceVersionDumpName.IsEmpty())
		{
			FString DumpFilePath = ResourceDumpDirectory + ResourceVersionDumpName;

			FDumpTexturePass* PassParameters = GraphBuilder.AllocParameters<FDumpTexturePass>();
			PassParameters->Texture = Texture;

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("RDG DumpTexture(%s -> %s)", Texture->Name, *DumpFilePath),
				PassParameters,
				ERDGPassFlags::Readback,
				[DumpFilePath, Texture, ByteSize](FRHICommandListImmediate& RHICmdList)
			{
				check(IsInRenderingThread());
				TUniquePtr<FRHIGPUTextureReadback> Readback = MakeUnique<FRHIGPUTextureReadback>(TEXT("RDGTextureDumpReadback"));;

				Readback->EnqueueCopyRDG(RHICmdList, Texture->GetRHI(), FResolveRect());

				int32 RowPitchInPixels = 0;
				void* Content = nullptr;
				Readback->LockTexture(RHICmdList, /* out */ Content, /* out */ RowPitchInPixels);
				if (Content)
				{
					TArray<uint8> Array;
					Array.SetNumUninitialized(ByteSize);

					int32 BytePerPixel = GPixelFormats[Texture->Desc.Format].BlockBytes;

					const uint8* SrcData = reinterpret_cast<const uint8*>(Content);

					for (int32 y = 0; y < Texture->Desc.Extent.Y; y++)
					{
						// Flip the data to be bottom left corner for the WebGL viewer.
						const uint8* SrcPos = SrcData + (Texture->Desc.Extent.Y - 1 - y) * RowPitchInPixels * BytePerPixel;
						uint8* DstPos = (&Array[0]) + y * Texture->Desc.Extent.X * BytePerPixel;

						FPlatformMemory::Memmove(DstPos, SrcPos, Texture->Desc.Extent.X * BytePerPixel);
					}

					TArrayView<const uint8> ArrayView(reinterpret_cast<const uint8*>(&Array[0]), ByteSize);
					FFileHelper::SaveArrayToFile(ArrayView, *DumpFilePath);

					Readback->Unlock();
				}
			});
		}

		ResourcesDumped++;
	}

	void AddDumpBufferPass(
		FRDGBuilder& GraphBuilder,
		TArray<TSharedPtr<FJsonValue>>& InputResourceNames,
		TArray<TSharedPtr<FJsonValue>>& OutputResourceNames,
		const FRDGPass* Pass,
		FRDGBuffer* Buffer,
		bool bIsOutputResource)
	{
		FString UniqueResourceName = GetResourceName(Buffer);

		if (bIsOutputResource)
		{
			OutputResourceNames.AddUnique(MakeShareable(new FJsonValueString(UniqueResourceName)));
		}
		else
		{
			InputResourceNames.AddUnique(MakeShareable(new FJsonValueString(UniqueResourceName)));
		}

		const FRDGBufferDesc& Desc = Buffer->Desc;
		int32 ByteSize = Desc.GetTotalNumBytes();

		FString ResourceDumpDirectory = GetResourceDumpDirectory();

		bool bDumpResourceInfos;
		FString ResourceVersionDumpName;
		GetResourceDumpInfo(Pass, Buffer, bIsOutputResource, &bDumpResourceInfos, &ResourceVersionDumpName);

		// Dump the information of the buffer to json file.
		if (bDumpResourceInfos)
		{
			TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
			JsonObject->SetStringField(TEXT("Name"), Buffer->Name);
			JsonObject->SetNumberField(TEXT("ByteSize"), ByteSize);
			JsonObject->SetStringField(TEXT("Type"), GetBufferUnderlyingTypeName(Desc.UnderlyingType));
			JsonObject->SetNumberField(TEXT("BytesPerElement"), Desc.BytesPerElement);
			JsonObject->SetNumberField(TEXT("NumElements"), Desc.NumElements);

			FString OutputString;
			TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutputString);
			FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

			FString ResourceInfoJsonPath = ResourceDumpDirectory + UniqueResourceName + TEXT(".json");
			FFileHelper::SaveStringToFile(OutputString, *ResourceInfoJsonPath);
		}

		if (Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::AccelerationStructure)
		{
			return;
		}

		// Dump the resource's binary to a .bin file.
		if (!ResourceVersionDumpName.IsEmpty())
		{
			FString DumpFilePath = ResourceDumpDirectory + ResourceVersionDumpName;

			FDumpBufferPass* PassParameters = GraphBuilder.AllocParameters<FDumpBufferPass>();
			PassParameters->Buffer = Buffer;

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("RDG DumpBuffer(%s -> %s)", Buffer->Name, *DumpFilePath),
				PassParameters,
				ERDGPassFlags::Readback,
				[DumpFilePath, Buffer, ByteSize](FRHICommandListImmediate& RHICmdList)
			{
				check(IsInRenderingThread());
				TUniquePtr<FRHIGPUBufferReadback> Readback = MakeUnique<FRHIGPUBufferReadback>(TEXT("RDGBufferDumpReadback"));;

				// TODO: work arround BUF_SourceCopy ensure()
				Readback->EnqueueCopy(RHICmdList, Buffer->GetRHI(), ByteSize);

				void* Content = Readback->Lock(ByteSize);
				if (Content)
				{
					TArrayView<const uint8> ArrayView(reinterpret_cast<const uint8*>(Content), ByteSize);
					FFileHelper::SaveArrayToFile(ArrayView, *DumpFilePath);

					Readback->Unlock();
				}
				else
				{
					ensureMsgf(false, TEXT("%s couldn't be dumped"), Buffer->Name);
				}
			});
		}

		ResourcesDumped++;
	}
};

FRDGResourceDumpContext GRDGResourceDumpContext;

static FAutoConsoleCommand GDumpGPUCommand(
	TEXT("r.DumpGPU"),
	TEXT("Dump a multi-platform GPU capture of RDG resources, to be used when creating a JIRA of an artifact/rendering bug."),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args)
		{
			check(IsInGameThread());

			ENQUEUE_RENDER_COMMAND(FSceneRendererCleanUp)(
				[Args](FRHICommandListImmediate& RHICmdList)
			{
				FRDGBuilder::BeginResourceDump(Args);
			});
		}
	)
);

}

void FRDGBuilder::BeginResourceDump(const TArray<FString>& Args)
{
	if (GRDGResourceDumpContext.IsDumpingFrame())
	{
		return;
	}

	GRDGResourceDumpContext.Time = FDateTime::Now();
	GRDGResourceDumpContext.DumpingDirectoryPath = FPaths::ProjectSavedDir() + TEXT("GPUDumps/") + FApp::GetProjectName() + TEXT("-") + FPlatformProperties::PlatformName() + TEXT("-") + GRDGResourceDumpContext.Time.ToString() + TEXT("/");

	GRDGResourceDumpContext.bShowInExplore = Args.Contains(TEXT("-show"));

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*GRDGResourceDumpContext.DumpingDirectoryPath))
	{
		PlatformFile.CreateDirectoryTree(*GRDGResourceDumpContext.DumpingDirectoryPath);
	}
	PlatformFile.CreateDirectoryTree(*(GRDGResourceDumpContext.DumpingDirectoryPath + TEXT("Resources/")));

	// Output informations
	{
		TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
		JsonObject->SetStringField(TEXT("Project"), FApp::GetProjectName());
		JsonObject->SetStringField(TEXT("BuildBranch"), BuildSettings::GetBranchName());
		JsonObject->SetStringField(TEXT("BuildDate"), BuildSettings::GetBuildDate());
		JsonObject->SetStringField(TEXT("BuildVersion"), BuildSettings::GetBuildVersion());
		JsonObject->SetStringField(TEXT("DumpTime"), GRDGResourceDumpContext.Time.ToString());
		JsonObject->SetStringField(TEXT("Platform"), FPlatformProperties::IniPlatformName());
		JsonObject->SetStringField(TEXT("RHI"), FApp::GetGraphicsRHI());
		JsonObject->SetStringField(TEXT("GPUVendor"), RHIVendorIdToString());
		JsonObject->SetStringField(TEXT("ComputerName"), FPlatformProcess::ComputerName());

		FString OutputString;
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutputString);
		FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

		FString ResourceInfoJsonPath = GRDGResourceDumpContext.DumpingDirectoryPath + TEXT("Infos.json");
		FFileHelper::SaveStringToFile(OutputString, *ResourceInfoJsonPath);
	}

	// Copy the viewer
	{
		const TCHAR* OpenGPUDumpViewerWindowsName = TEXT("OpenGPUDumpViewer.bat");
		const TCHAR* ViewerHTML = TEXT("GPUDumpViewer.html");
		FString DumpGPUViewerSourcePath = FPaths::EngineDir() + FString(TEXT("Extras")) / TEXT("GPUDumpViewer");

		PlatformFile.CopyFile(*(GRDGResourceDumpContext.DumpingDirectoryPath / ViewerHTML), *(DumpGPUViewerSourcePath / ViewerHTML));
		PlatformFile.CopyFile(*(GRDGResourceDumpContext.DumpingDirectoryPath / OpenGPUDumpViewerWindowsName), *(DumpGPUViewerSourcePath / OpenGPUDumpViewerWindowsName));
	}
}

void FRDGBuilder::EndResourceDump()
{
	if (!GRDGResourceDumpContext.IsDumpingFrame())
	{
		return;
	}

	FString AbsDumpingDirectoryPath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*GRDGResourceDumpContext.DumpingDirectoryPath);
	UE_LOG(LogRendererCore, Display, TEXT("Dumped %d resources to %s"), GRDGResourceDumpContext.ResourcesDumped, *AbsDumpingDirectoryPath);

	if (GRDGResourceDumpContext.bShowInExplore)
	#if PLATFORM_DESKTOP
	{
		FPlatformProcess::ExploreFolder(*AbsDumpingDirectoryPath);
	}
	#else
	{
		UE_LOG(LogRendererCore, Warning, TEXT("r.DumpGPU parameter -show is not supported on this platform."));
	}
	#endif

	GRDGResourceDumpContext = FRDGResourceDumpContext();
}

void FRDGBuilder::DumpResourcePassOutputs(const FRDGPass* Pass)
{
	if (bInDebugPassScope)
	{
		return;
	}

	if (!GRDGResourceDumpContext.IsDumpingFrame())
	{
		return;
	}

	// Look whether the pass matches matches r.DumpGPU.Root
	{
		FString RootWildcardString = GDumpGPURootCVar.GetValueOnRenderThread();
		FWildcardString WildcardFilter(RootWildcardString);

		bool bDumpPass = (RootWildcardString == TEXT("*"));

		if (!bDumpPass)
		{
			bDumpPass = WildcardFilter.IsMatch(Pass->GetEventName().GetTCHAR());
		}
		
		#if RDG_GPU_SCOPES
		if (!bDumpPass)
		{
			const FRDGEventScope* ParentScope = Pass->GetGPUScopes().Event;

			while (ParentScope)
			{
				bDumpPass = bDumpPass || WildcardFilter.IsMatch(ParentScope->Name.GetTCHAR());
				ParentScope = ParentScope->ParentScope;
			}
		}
		#endif

		if (!bDumpPass)
		{
			return;
		}
	}

	bInDebugPassScope = true;

	TArray<TSharedPtr<FJsonValue>> InputResourceNames;
	TArray<TSharedPtr<FJsonValue>> OutputResourceNames;
	Pass->GetParameters().Enumerate([&](FRDGParameter Parameter)
	{
		switch (Parameter.GetType())
		{
		case UBMT_RDG_TEXTURE:
		{
			if (FRDGTextureRef Texture = Parameter.GetAsTexture())
			{
				GRDGResourceDumpContext.AddDumpTexturePass(*this, InputResourceNames, OutputResourceNames, Pass, Texture, /* bIsOutputResource = */ false);
			}
		}
		break;
		case UBMT_RDG_TEXTURE_SRV:
		{
			if (FRDGTextureSRVRef SRV = Parameter.GetAsTextureSRV())
			{
				FRDGTextureRef Texture = SRV->Desc.Texture;
				GRDGResourceDumpContext.AddDumpTexturePass(*this, InputResourceNames, OutputResourceNames, Pass, Texture, /* bIsOutputResource = */ false);
			}
		}
		break;
		case UBMT_RDG_TEXTURE_UAV:
		{
			if (FRDGTextureUAVRef UAV = Parameter.GetAsTextureUAV())
			{
				FRDGTextureRef Texture = UAV->Desc.Texture;
				GRDGResourceDumpContext.AddDumpTexturePass(*this, InputResourceNames, OutputResourceNames, Pass, Texture, /* bIsOutputResource = */ true);
			}
		}
		break;
		case UBMT_RDG_TEXTURE_ACCESS:
		{
			if (FRDGTextureAccess TextureAccess = Parameter.GetAsTextureAccess())
			{
				bool bIsOutputResource = (
					TextureAccess.GetAccess() == ERHIAccess::UAVCompute ||
					TextureAccess.GetAccess() == ERHIAccess::UAVGraphics ||
					TextureAccess.GetAccess() == ERHIAccess::RTV);

				GRDGResourceDumpContext.AddDumpTexturePass(*this, InputResourceNames, OutputResourceNames, Pass, TextureAccess, bIsOutputResource);
			}
		}
		break;
		case UBMT_RDG_TEXTURE_ACCESS_ARRAY:
		{
			const FRDGTextureAccessArray& TextureAccessArray = Parameter.GetAsTextureAccessArray();

			for (FRDGTextureAccess TextureAccess : TextureAccessArray)
			{
				bool bIsOutputResource = (
					TextureAccess.GetAccess() == ERHIAccess::UAVCompute ||
					TextureAccess.GetAccess() == ERHIAccess::UAVGraphics ||
					TextureAccess.GetAccess() == ERHIAccess::RTV);

				GRDGResourceDumpContext.AddDumpTexturePass(*this, InputResourceNames, OutputResourceNames, Pass, TextureAccess, bIsOutputResource);
			}
		}
		break;

		case UBMT_RDG_BUFFER_SRV:
		{
			if (FRDGBufferSRVRef SRV = Parameter.GetAsBufferSRV())
			{
				FRDGBufferRef Buffer = SRV->Desc.Buffer;
				GRDGResourceDumpContext.AddDumpBufferPass(*this, InputResourceNames, OutputResourceNames, Pass, Buffer, /* bIsOutputResource = */ false);
			}
		}
		break;
		case UBMT_RDG_BUFFER_UAV:
		{
			if (FRDGBufferUAVRef UAV = Parameter.GetAsBufferUAV())
			{
				FRDGBufferRef Buffer = UAV->Desc.Buffer;
				GRDGResourceDumpContext.AddDumpBufferPass(*this, InputResourceNames, OutputResourceNames, Pass, Buffer, /* bIsOutputResource = */ true);
			}
		}
		break;
		case UBMT_RDG_BUFFER_ACCESS:
		{
			if (FRDGBufferAccess BufferAccess = Parameter.GetAsBufferAccess())
			{
				bool bIsOutputResource = (
					BufferAccess.GetAccess() == ERHIAccess::UAVCompute ||
					BufferAccess.GetAccess() == ERHIAccess::UAVGraphics);

				GRDGResourceDumpContext.AddDumpBufferPass(*this, InputResourceNames, OutputResourceNames, Pass, BufferAccess, bIsOutputResource);
			}
		}
		break;
		case UBMT_RDG_BUFFER_ACCESS_ARRAY:
		{
			const FRDGBufferAccessArray& BufferAccessArray = Parameter.GetAsBufferAccessArray();

			for (FRDGBufferAccess BufferAccess : BufferAccessArray)
			{
				bool bIsOutputResource = (
					BufferAccess.GetAccess() == ERHIAccess::UAVCompute ||
					BufferAccess.GetAccess() == ERHIAccess::UAVGraphics);

				GRDGResourceDumpContext.AddDumpBufferPass(*this, InputResourceNames, OutputResourceNames, Pass, BufferAccess, bIsOutputResource);
			}
		}
		break;

		case UBMT_RENDER_TARGET_BINDING_SLOTS:
		{
			const FRenderTargetBindingSlots& RenderTargets = Parameter.GetAsRenderTargetBindingSlots();

			RenderTargets.Enumerate([&](FRenderTargetBinding RenderTarget)
			{
				FRDGTextureRef Texture = RenderTarget.GetTexture();
				GRDGResourceDumpContext.AddDumpTexturePass(*this, InputResourceNames, OutputResourceNames, Pass, Texture, /* bIsOutputResource = */ true);
			});

			const FDepthStencilBinding& DepthStencil = RenderTargets.DepthStencil;

			if (FRDGTextureRef Texture = DepthStencil.GetTexture())
			{
				const bool bHasStoreAction = DepthStencil.GetDepthStencilAccess().IsAnyWrite();
				GRDGResourceDumpContext.AddDumpTexturePass(*this, InputResourceNames, OutputResourceNames, Pass, Texture, /* bIsOutputResource = */ bHasStoreAction);
			}
		}
		break;
		}
	});

	{
		TArray<TSharedPtr<FJsonValue>> ParentEventScopeNames;
		#if RDG_GPU_SCOPES
		{
			const FRDGEventScope* ParentScope = Pass->GetGPUScopes().Event;

			while (ParentScope)
			{
				ParentEventScopeNames.Add(MakeShareable(new FJsonValueString(ParentScope->Name.GetTCHAR())));
				ParentScope = ParentScope->ParentScope;
			}
		}
		#endif

		TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
		JsonObject->SetStringField(TEXT("EventName"), Pass->GetEventName().GetTCHAR());
		JsonObject->SetStringField(TEXT("ParametersName"), Pass->GetParameters().GetLayout().GetDebugName());
		JsonObject->SetStringField(TEXT("Pointer"), FString::Printf(TEXT("%p"), Pass));
		JsonObject->SetNumberField(TEXT("Id"), GRDGResourceDumpContext.PassesCount);
		JsonObject->SetArrayField(TEXT("ParentEventScopes"), ParentEventScopeNames);
		JsonObject->SetArrayField(TEXT("InputResources"), InputResourceNames);
		JsonObject->SetArrayField(TEXT("OutputResources"), OutputResourceNames);

		FString OutputString;
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutputString);
		FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

		FString JsonPath = GRDGResourceDumpContext.DumpingDirectoryPath + TEXT("Passes.json");
		FFileHelper::SaveStringToFile(
			OutputString, *JsonPath,
			FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
	}

	GRDGResourceDumpContext.PassesCount++;

	bInDebugPassScope = false;
}

#endif //! RDG_DUMP_RESOURCES