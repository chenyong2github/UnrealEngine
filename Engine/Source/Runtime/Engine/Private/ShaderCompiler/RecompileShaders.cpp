// Copyright Epic Games, Inc. All Rights Reserved.

#include "RecompileShaders.h"

#include "GlobalShader.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "MaterialShared.h"
#include "Misc/FeedbackContext.h"
#if WITH_ODSC
#include "ODSC/ODSCManager.h"
#endif
#include "Serialization/NameAsStringProxyArchive.h"
#include "ShaderCompiler.h"
#include "ShaderCompilerShared.h"
#include "UObject/UObjectIterator.h"

/** Timer class used to report information on the 'recompileshaders' console command. */
class FRecompileShadersTimer
{
public:
	FRecompileShadersTimer(const TCHAR* InInfoStr = TEXT("Test")) :
		InfoStr(InInfoStr),
		bAlreadyStopped(false)
	{
		StartTime = FPlatformTime::Seconds();
	}

	FRecompileShadersTimer(const FString& InInfoStr) :
		InfoStr(InInfoStr),
		bAlreadyStopped(false)
	{
		StartTime = FPlatformTime::Seconds();
	}

	void Stop(bool DisplayLog = true)
	{
		if (!bAlreadyStopped)
		{
			bAlreadyStopped = true;
			EndTime = FPlatformTime::Seconds();
			TimeElapsed = EndTime - StartTime;
			if (DisplayLog)
			{
				UE_LOG(LogShaderCompilers, Warning, TEXT("		[%s] took [%.4f] s"), *InfoStr, TimeElapsed);
			}
		}
	}

	~FRecompileShadersTimer()
	{
		Stop(true);
	}

protected:
	double StartTime, EndTime;
	double TimeElapsed;
	FString InfoStr;
	bool bAlreadyStopped;
};

namespace
{
	ODSCRecompileCommand ParseRecompileCommandString(const TCHAR* CmdString, TArray<FString>& OutMaterialsToLoad)
	{
		FString CmdName = FParse::Token(CmdString, 0);

		ODSCRecompileCommand CommandType = ODSCRecompileCommand::None;
		OutMaterialsToLoad.Empty();

		if (!CmdName.IsEmpty() && FCString::Stricmp(*CmdName, TEXT("Material")) == 0)
		{
			CommandType = ODSCRecompileCommand::Material;

			// tell other side the material to load, by pathname
			FString RequestedMaterialName(FParse::Token(CmdString, 0));

			for (TObjectIterator<UMaterialInterface> It; It; ++It)
			{
				UMaterial* Material = It->GetMaterial();

				if (Material && Material->GetName() == RequestedMaterialName)
				{
					OutMaterialsToLoad.Add(It->GetPathName());
					break;
				}
			}
		}
		else if (!CmdName.IsEmpty() && FCString::Stricmp(*CmdName, TEXT("Global")) == 0)
		{
			CommandType = ODSCRecompileCommand::Global;
		}
		else if (!CmdName.IsEmpty() && FCString::Stricmp(*CmdName, TEXT("Changed")) == 0)
		{
			CommandType = ODSCRecompileCommand::Changed;

			// Compile all the shaders that have changed for the materials we have loaded.
			for (TObjectIterator<UMaterialInterface> It; It; ++It)
			{
				OutMaterialsToLoad.Add(It->GetPathName());
			}
		}
		else
		{
			CommandType = ODSCRecompileCommand::Material;

			// tell other side all the materials to load, by pathname
			for (TObjectIterator<UMaterialInterface> It; It; ++It)
			{
				OutMaterialsToLoad.Add(It->GetPathName());
			}
		}

		return CommandType;
	}
}

void ProcessCookOnTheFlyShaders(bool bReloadGlobalShaders, const TArray<uint8>& MeshMaterialMaps, const TArray<FString>& MaterialsToLoad, const TArray<uint8>& GlobalShaderMap)
{
	check(IsInGameThread());

	// now we need to refresh the RHI resources
	FlushRenderingCommands();

	// reload the global shaders
	if (bReloadGlobalShaders)
	{
		// Some platforms rely on global shaders to be created to implement basic RHI functionality
		extern int32 GCreateShadersOnLoad;
		TGuardValue<int32> Guard(GCreateShadersOnLoad, 1);
		CompileGlobalShaderMap(true);
	}

	// load all the mesh material shaders if any were sent back
	if (MeshMaterialMaps.Num() > 0)
	{
		// parse the shaders
		FMemoryReader MemoryReader(MeshMaterialMaps, true);
		FNameAsStringProxyArchive Ar(MemoryReader);

		TArray<UMaterialInterface*> LoadedMaterials;
		FMaterialShaderMap::LoadForRemoteRecompile(Ar, GMaxRHIShaderPlatform, LoadedMaterials);

		// Only update materials if we need to.
		if (LoadedMaterials.Num())
		{
			// this will stop the rendering thread, and reattach components, in the destructor
			FMaterialUpdateContext UpdateContext(FMaterialUpdateContext::EOptions::RecreateRenderStates);

			// gather the shader maps to reattach
			for (UMaterialInterface* Material : LoadedMaterials)
			{
				Material->RecacheUniformExpressions(true);
				UpdateContext.AddMaterialInterface(Material);
			}
		}
	}

	// load all the global shaders if any were sent back
	if (GlobalShaderMap.Num() > 0)
	{
		// parse the shaders
		FMemoryReader MemoryReader(GlobalShaderMap, true);
		FNameAsStringProxyArchive Ar(MemoryReader);

		LoadGlobalShadersForRemoteRecompile(Ar, GMaxRHIShaderPlatform);
	}
}

/**
* Forces a recompile of the global shaders.
*/
void RecompileGlobalShaders()
{
	if (!FPlatformProperties::RequiresCookedData())
	{
		// Flush pending accesses to the existing global shaders.
		FlushRenderingCommands();

		UMaterialInterface::IterateOverActiveFeatureLevels([&](ERHIFeatureLevel::Type InFeatureLevel) {
			auto ShaderPlatform = GShaderPlatformForFeatureLevel[InFeatureLevel];
			GetGlobalShaderMap(ShaderPlatform)->Empty();
			VerifyGlobalShaders(ShaderPlatform, nullptr, false);
			});

		GShaderCompilingManager->ProcessAsyncResults(false, true);
	}
}

void GetOutdatedShaderTypes(TArray<const FShaderType*>& OutdatedShaderTypes, TArray<const FShaderPipelineType*>& OutdatedShaderPipelineTypes, TArray<const FVertexFactoryType*>& OutdatedFactoryTypes)
{
#if WITH_EDITOR
	for (int PlatformIndex = 0; PlatformIndex < SP_NumPlatforms; ++PlatformIndex)
	{
		const FGlobalShaderMap* ShaderMap = GGlobalShaderMap[PlatformIndex];
		if (ShaderMap)
		{
			ShaderMap->GetOutdatedTypes(OutdatedShaderTypes, OutdatedShaderPipelineTypes, OutdatedFactoryTypes);
		}
	}

	FMaterialShaderMap::GetAllOutdatedTypes(OutdatedShaderTypes, OutdatedShaderPipelineTypes, OutdatedFactoryTypes);

	for (int32 TypeIndex = 0; TypeIndex < OutdatedShaderTypes.Num(); TypeIndex++)
	{
		UE_LOG(LogShaders, Warning, TEXT("		Recompiling %s"), OutdatedShaderTypes[TypeIndex]->GetName());
	}
	for (int32 TypeIndex = 0; TypeIndex < OutdatedShaderPipelineTypes.Num(); TypeIndex++)
	{
		UE_LOG(LogShaders, Warning, TEXT("		Recompiling %s"), OutdatedShaderPipelineTypes[TypeIndex]->GetName());
	}
	for (int32 TypeIndex = 0; TypeIndex < OutdatedFactoryTypes.Num(); TypeIndex++)
	{
		UE_LOG(LogShaders, Warning, TEXT("		Recompiling %s"), OutdatedFactoryTypes[TypeIndex]->GetName());
	}
#endif // WITH_EDITOR
}

bool RecompileShaders(const TCHAR* Cmd, FOutputDevice& Ar)
{
	// if this platform can't compile shaders, then we try to send a message to a file/cooker server
	if (FPlatformProperties::RequiresCookedData())
	{
#if WITH_ODSC
		TArray<FString> MaterialsToLoad;
		ODSCRecompileCommand CommandType = ParseRecompileCommandString(Cmd, MaterialsToLoad);
		GODSCManager->AddThreadedRequest(MaterialsToLoad, GMaxRHIShaderPlatform, CommandType);
#endif
		return true;
	}

	FString FlagStr(FParse::Token(Cmd, 0));
	if (FlagStr.Len() > 0)
	{
		GWarn->BeginSlowTask(NSLOCTEXT("ShaderCompilingManager", "BeginRecompilingShadersTask", "Recompiling shaders"), true);

		// Flush the shader file cache so that any changes to shader source files will be detected
		FlushShaderFileCache();
		FlushRenderingCommands();

		if (FCString::Stricmp(*FlagStr, TEXT("Changed")) == 0)
		{
			TArray<const FShaderType*> OutdatedShaderTypes;
			TArray<const FVertexFactoryType*> OutdatedFactoryTypes;
			TArray<const FShaderPipelineType*> OutdatedShaderPipelineTypes;
			{
				FRecompileShadersTimer SearchTimer(TEXT("Searching for changed files"));
				GetOutdatedShaderTypes(OutdatedShaderTypes, OutdatedShaderPipelineTypes, OutdatedFactoryTypes);
			}

			if (OutdatedShaderPipelineTypes.Num() > 0 || OutdatedShaderTypes.Num() > 0 || OutdatedFactoryTypes.Num() > 0)
			{
				FRecompileShadersTimer TestTimer(TEXT("RecompileShaders Changed"));

				UMaterialInterface::IterateOverActiveFeatureLevels([&](ERHIFeatureLevel::Type InFeatureLevel) {
					auto ShaderPlatform = GShaderPlatformForFeatureLevel[InFeatureLevel];
					BeginRecompileGlobalShaders(OutdatedShaderTypes, OutdatedShaderPipelineTypes, ShaderPlatform);
					});

				// Block on global shaders
				FinishRecompileGlobalShaders();

				// Kick off global shader recompiles
				UMaterialInterface::IterateOverActiveFeatureLevels([&](ERHIFeatureLevel::Type InFeatureLevel) {
					auto ShaderPlatform = GShaderPlatformForFeatureLevel[InFeatureLevel];
					UMaterial::UpdateMaterialShaders(OutdatedShaderTypes, OutdatedShaderPipelineTypes, OutdatedFactoryTypes, ShaderPlatform);
					});

				GWarn->StatusUpdate(0, 1, NSLOCTEXT("ShaderCompilingManager", "CompilingGlobalShaderStatus", "Compiling global shaders..."));
			}
			else
			{
				UE_LOG(LogShaderCompilers, Warning, TEXT("No Shader changes found."));
			}
		}
		else if (FCString::Stricmp(*FlagStr, TEXT("Global")) == 0)
		{
			FRecompileShadersTimer TestTimer(TEXT("RecompileShaders Global"));
			RecompileGlobalShaders();
		}
		else if (FCString::Stricmp(*FlagStr, TEXT("Material")) == 0)
		{
			FString RequestedMaterialName(FParse::Token(Cmd, 0));
			FRecompileShadersTimer TestTimer(FString::Printf(TEXT("Recompile Material %s"), *RequestedMaterialName));

			ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();
			FString TargetPlatformName(FParse::Token(Cmd, 0));
			const ITargetPlatform* TargetPlatform = nullptr;
			if (TargetPlatformName.Len() > 0)
			{
				TargetPlatform = TPM.FindTargetPlatform(TargetPlatformName);
			}

			bool bMaterialFound = false;
			for (TObjectIterator<UMaterialInterface> It; It; ++It)
			{
				UMaterialInterface* Material = *It;
				if (Material && Material->GetName() == RequestedMaterialName)
				{
					bMaterialFound = true;
#if WITH_EDITOR
					// <Pre/Post>EditChange will force a re-creation of the resource,
					// in turn recompiling the shader.
					if (TargetPlatform)
					{
						Material->BeginCacheForCookedPlatformData(TargetPlatform);
						while (!Material->IsCachedCookedPlatformDataLoaded(TargetPlatform))
						{
							FPlatformProcess::Sleep(0.1f);
							GShaderCompilingManager->ProcessAsyncResults(false, false);
						}
						Material->ClearCachedCookedPlatformData(TargetPlatform);
					}
					else
					{
						Material->PreEditChange(NULL);
						Material->PostEditChange();
					}
#endif // WITH_EDITOR
					break;
				}
			}

			if (!bMaterialFound)
			{
				TestTimer.Stop(false);
				UE_LOG(LogShaderCompilers, Warning, TEXT("Couldn't find Material %s!"), *RequestedMaterialName);
			}
		}
		else if (FCString::Stricmp(*FlagStr, TEXT("All")) == 0)
		{
			FRecompileShadersTimer TestTimer(TEXT("RecompileShaders"));
			RecompileGlobalShaders();

			FMaterialUpdateContext UpdateContext(0);
			for (TObjectIterator<UMaterial> It; It; ++It)
			{
				UMaterial* Material = *It;
				if (Material)
				{
					UE_LOG(LogShaderCompilers, Log, TEXT("recompiling [%s]"), *Material->GetFullName());
					UpdateContext.AddMaterial(Material);
#if WITH_EDITOR
					// <Pre/Post>EditChange will force a re-creation of the resource,
					// in turn recompiling the shader.
					Material->PreEditChange(NULL);
					Material->PostEditChange();
#endif // WITH_EDITOR
				}
			}
		}
		else
		{
			TArray<const FShaderType*> ShaderTypes = FShaderType::GetShaderTypesByFilename(*FlagStr);
			TArray<const FShaderPipelineType*> ShaderPipelineTypes = FShaderPipelineType::GetShaderPipelineTypesByFilename(*FlagStr);
			if (ShaderTypes.Num() > 0 || ShaderPipelineTypes.Num() > 0)
			{
				FRecompileShadersTimer TestTimer(TEXT("RecompileShaders SingleShader"));

				TArray<const FVertexFactoryType*> FactoryTypes;

				UMaterialInterface::IterateOverActiveFeatureLevels([&](ERHIFeatureLevel::Type InFeatureLevel) {
					auto ShaderPlatform = GShaderPlatformForFeatureLevel[InFeatureLevel];
					BeginRecompileGlobalShaders(ShaderTypes, ShaderPipelineTypes, ShaderPlatform);
					//UMaterial::UpdateMaterialShaders(ShaderTypes, ShaderPipelineTypes, FactoryTypes, ShaderPlatform);
					FinishRecompileGlobalShaders();
					});
			}
		}

		GWarn->EndSlowTask();

		return 1;
	}

	UE_LOG(LogShaderCompilers, Warning, TEXT("Invalid parameter. Options are: \n'Changed', 'Global', 'Material [name]', 'All'."));
	return 1;
}

bool RecompileChangedShadersForPlatform(const FString& PlatformName)
{
	// figure out what shader platforms to recompile
	ITargetPlatformManagerModule* TPM = GetTargetPlatformManager();
	ITargetPlatform* TargetPlatform = TPM->FindTargetPlatform(PlatformName);
	if (TargetPlatform == NULL)
	{
		UE_LOG(LogShaders, Display, TEXT("Failed to find target platform module for %s"), *PlatformName);
		return false;
	}

	TArray<FName> DesiredShaderFormats;
	TargetPlatform->GetAllTargetedShaderFormats(DesiredShaderFormats);



	// figure out which shaders are out of date
	TArray<const FShaderType*> OutdatedShaderTypes;
	TArray<const FVertexFactoryType*> OutdatedFactoryTypes;
	TArray<const FShaderPipelineType*> OutdatedShaderPipelineTypes;

	// Pick up new changes to shader files
	FlushShaderFileCache();

	GetOutdatedShaderTypes(OutdatedShaderTypes, OutdatedShaderPipelineTypes, OutdatedFactoryTypes);
	UE_LOG(LogShaders, Display, TEXT("We found %d out of date shader types, %d outdated pipeline types, and %d out of date VF types!"), OutdatedShaderTypes.Num(), OutdatedShaderPipelineTypes.Num(), OutdatedFactoryTypes.Num());

	for (int32 FormatIndex = 0; FormatIndex < DesiredShaderFormats.Num(); FormatIndex++)
	{
		// get the shader platform enum
		const EShaderPlatform ShaderPlatform = ShaderFormatToLegacyShaderPlatform(DesiredShaderFormats[FormatIndex]);

		// Only compile for the desired platform if requested
		// Kick off global shader recompiles
		BeginRecompileGlobalShaders(OutdatedShaderTypes, OutdatedShaderPipelineTypes, ShaderPlatform);

		// Block on global shaders
		FinishRecompileGlobalShaders();
#if WITH_EDITOR
		// we only want to actually compile mesh shaders if we have out of date ones
		if (OutdatedShaderTypes.Num() || OutdatedFactoryTypes.Num())
		{
			for (TObjectIterator<UMaterialInterface> It; It; ++It)
			{
				(*It)->ClearCachedCookedPlatformData(TargetPlatform);
			}
		}
#endif
	}

	if (OutdatedFactoryTypes.Num() || OutdatedShaderTypes.Num())
	{
		return true;
	}
	return false;
}

FArchive& operator<<(FArchive& Ar, FODSCRequestPayload& Elem)
{
	uint32 ConvertedShaderPlatform = (uint32)Elem.ShaderPlatform;
	Ar << ConvertedShaderPlatform;
	Ar << Elem.MaterialName;
	Ar << Elem.VertexFactoryName;
	Ar << Elem.PipelineName;
	Ar << Elem.ShaderTypeNames;
	Ar << Elem.RequestHash;

	return Ar;
}

FShaderRecompileData::FShaderRecompileData(const FString& InPlatformName, TArray<FString>* OutModifiedFiles, TArray<uint8>* OutMeshMaterialMaps, TArray<uint8>* OutGlobalShaderMap)
	: PlatformName(InPlatformName),
	ModifiedFiles(OutModifiedFiles),
	MeshMaterialMaps(OutMeshMaterialMaps),
	GlobalShaderMap(OutGlobalShaderMap)
{
}

FShaderRecompileData::FShaderRecompileData(const FString& InPlatformName, EShaderPlatform InShaderPlatform, ODSCRecompileCommand InCommandType, TArray<FString>* OutModifiedFiles, TArray<uint8>* OutMeshMaterialMaps, TArray<uint8>* OutGlobalShaderMap)
	: PlatformName(InPlatformName),
	ShaderPlatform(InShaderPlatform),
	ModifiedFiles(OutModifiedFiles),
	MeshMaterialMaps(OutMeshMaterialMaps),
	CommandType(InCommandType),
	GlobalShaderMap(OutGlobalShaderMap)
{
}

void SaveGlobalShadersForRemoteRecompile(FArchive& Ar, EShaderPlatform ShaderPlatform)
{
	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(ShaderPlatform);
	uint8 bIsValid = GlobalShaderMap != nullptr;
	Ar << bIsValid;

	if (GlobalShaderMap)
	{
		GlobalShaderMap->SaveToGlobalArchive(Ar);
	}
}

void LoadGlobalShadersForRemoteRecompile(FArchive& Ar, EShaderPlatform ShaderPlatform)
{
	uint8 bIsValid = 0;
	Ar << bIsValid;

	if (bIsValid)
	{
		FlushRenderingCommands();

		FGlobalShaderMap* NewGlobalShaderMap = new FGlobalShaderMap(ShaderPlatform);
		if (NewGlobalShaderMap)
		{
			NewGlobalShaderMap->LoadFromGlobalArchive(Ar);

			if (GGlobalShaderMap[ShaderPlatform])
			{
				GGlobalShaderMap[ShaderPlatform]->ReleaseAllSections();

				delete GGlobalShaderMap[ShaderPlatform];
				GGlobalShaderMap[ShaderPlatform] = nullptr;
				GGlobalShaderMap[ShaderPlatform] = NewGlobalShaderMap;

				VerifyGlobalShaders(ShaderPlatform, nullptr, false);

				// Invalidate global bound shader states so they will be created with the new shaders the next time they are set (in SetGlobalBoundShaderState)
				for (TLinkedList<FGlobalBoundShaderStateResource*>::TIterator It(FGlobalBoundShaderStateResource::GetGlobalBoundShaderStateList()); It; It.Next())
				{
					BeginUpdateResourceRHI(*It);
				}

				PropagateGlobalShadersToAllPrimitives();
			}
			else
			{
				delete NewGlobalShaderMap;
			}
		}
	}
}

#if WITH_EDITOR
void RecompileShadersForRemote(
	FShaderRecompileData& Args,
	const FString& OutputDirectory)
{
	// figure out what shader platforms to recompile
	ITargetPlatformManagerModule* TPM = GetTargetPlatformManager();
	ITargetPlatform* TargetPlatform = TPM->FindTargetPlatform(Args.PlatformName);
	if (TargetPlatform == NULL)
	{
		UE_LOG(LogShaders, Display, TEXT("Failed to find target platform module for %s"), *Args.PlatformName);
		return;
	}

	const double StartTime = FPlatformTime::Seconds();

	UE_LOG(LogShaders, Display, TEXT(""));
	UE_LOG(LogShaders, Display, TEXT("********************************"));
	UE_LOG(LogShaders, Display, TEXT("Received compile shader request."));

	const bool bPreviousState = GShaderCompilingManager->IsShaderCompilationSkipped();
	GShaderCompilingManager->SkipShaderCompilation(false);

	TArray<FName> DesiredShaderFormats;
	TargetPlatform->GetAllTargetedShaderFormats(DesiredShaderFormats);

	UE_LOG(LogShaders, Verbose, TEXT("Loading %d materials..."), Args.MaterialsToLoad.Num());
	// make sure all materials the client has loaded will be processed
	TArray<UMaterialInterface*> MaterialsToCompile;

	for (int32 Index = 0; Index < Args.MaterialsToLoad.Num(); Index++)
	{
		UE_LOG(LogShaders, Verbose, TEXT("   --> %s"), *Args.MaterialsToLoad[Index]);
		MaterialsToCompile.Add(LoadObject<UMaterialInterface>(NULL, *Args.MaterialsToLoad[Index]));
	}

	UE_LOG(LogShaders, Verbose, TEXT("  Done!"))

		// figure out which shaders are out of date
		TArray<const FShaderType*> OutdatedShaderTypes;
	TArray<const FVertexFactoryType*> OutdatedFactoryTypes;
	TArray<const FShaderPipelineType*> OutdatedShaderPipelineTypes;

	// Pick up new changes to shader files
	FlushShaderFileCache();

	if (Args.ShadersToRecompile.Num())
	{
		UE_LOG(LogShaders, Display, TEXT("Received %d shaders to compile."), Args.ShadersToRecompile.Num());
	}

	for (const FODSCRequestPayload& payload : Args.ShadersToRecompile)
	{
		UE_LOG(LogShaders, Display, TEXT(""));
		UE_LOG(LogShaders, Display, TEXT("\tMaterial:    %s "), *payload.MaterialName);
		UE_LOG(LogShaders, Display, TEXT("\tVF Type:     %s "), *payload.VertexFactoryName);

		MaterialsToCompile.Add(LoadObject<UMaterialInterface>(NULL, *payload.MaterialName));

		const FVertexFactoryType* VFType = FVertexFactoryType::GetVFByName(payload.VertexFactoryName);
		if (VFType)
		{
			OutdatedFactoryTypes.Add(VFType);
		}

		const FShaderPipelineType* PipelineType = FShaderPipelineType::GetShaderPipelineTypeByName(payload.PipelineName);
		if (PipelineType)
		{
			OutdatedShaderPipelineTypes.Add(PipelineType);
		}

		for (const FString& ShaderTypeName : payload.ShaderTypeNames)
		{
			UE_LOG(LogShaders, Display, TEXT("\tShader Type: %s"), *ShaderTypeName);

			const FShaderType* ShaderType = FShaderType::GetShaderTypeByName(*ShaderTypeName);
			if (ShaderType)
			{
				OutdatedShaderTypes.Add(ShaderType);
			}
		}
	}

	{
		for (int32 FormatIndex = 0; FormatIndex < DesiredShaderFormats.Num(); FormatIndex++)
		{
			// get the shader platform enum
			const EShaderPlatform ShaderPlatform = ShaderFormatToLegacyShaderPlatform(DesiredShaderFormats[FormatIndex]);

			// Only compile for the desired platform if requested
			if (ShaderPlatform == Args.ShaderPlatform || Args.ShaderPlatform == SP_NumPlatforms)
			{
				// If we are explicitly wanting to recompile global or if shaders have changed.
				if (Args.CommandType == ODSCRecompileCommand::Global ||
					Args.CommandType == ODSCRecompileCommand::Changed)
				{
					UE_LOG(LogShaders, Display, TEXT("Recompiling global shaders."));

					// Explicitly get outdated types for global shaders.
					const FGlobalShaderMap* ShaderMap = GGlobalShaderMap[ShaderPlatform];
					if (ShaderMap)
					{
						ShaderMap->GetOutdatedTypes(OutdatedShaderTypes, OutdatedShaderPipelineTypes, OutdatedFactoryTypes);
					}

					UE_LOG(LogShaders, Display, TEXT("\tFound %d outdated shader types."), OutdatedShaderTypes.Num() + OutdatedShaderPipelineTypes.Num());

					// Kick off global shader recompiles
					BeginRecompileGlobalShaders(OutdatedShaderTypes, OutdatedShaderPipelineTypes, ShaderPlatform, TargetPlatform);

					// Block on global shaders
					FinishRecompileGlobalShaders();

					// write the shader compilation info to memory, converting fnames to strings
					FMemoryWriter MemWriter(*Args.GlobalShaderMap, true);
					FNameAsStringProxyArchive Ar(MemWriter);
					Ar.SetCookingTarget(TargetPlatform);

					// save out the global shader map to the byte array
					SaveGlobalShadersForRemoteRecompile(Ar, ShaderPlatform);
				}

				// we only want to actually compile mesh shaders if a client directly requested it
				if ((Args.CommandType == ODSCRecompileCommand::Material || Args.CommandType == ODSCRecompileCommand::Changed) &&
					Args.MeshMaterialMaps != nullptr)
				{
					TMap<FString, TArray<TRefCountPtr<FMaterialShaderMap> > > CompiledShaderMaps;
					UMaterial::CompileMaterialsForRemoteRecompile(MaterialsToCompile, ShaderPlatform, TargetPlatform, CompiledShaderMaps);

					// write the shader compilation info to memory, converting fnames to strings
					FMemoryWriter MemWriter(*Args.MeshMaterialMaps, true);
					FNameAsStringProxyArchive Ar(MemWriter);
					Ar.SetCookingTarget(TargetPlatform);

					// save out the shader map to the byte array
					FMaterialShaderMap::SaveForRemoteRecompile(Ar, CompiledShaderMaps);
				}

				// save it out so the client can get it (and it's up to date next time)
				FString GlobalShaderFilename = SaveGlobalShaderFile(ShaderPlatform, OutputDirectory, TargetPlatform);

				// add this to the list of files to tell the other end about
				if (Args.ModifiedFiles)
				{
					// need to put it in non-sandbox terms
					FString SandboxPath(GlobalShaderFilename);
					check(SandboxPath.StartsWith(OutputDirectory));
					SandboxPath.ReplaceInline(*OutputDirectory, TEXT("../../../"));
					FPaths::NormalizeFilename(SandboxPath);
					Args.ModifiedFiles->Add(SandboxPath);
				}
			}
		}
	}

	UE_LOG(LogShaders, Display, TEXT(""));
	UE_LOG(LogShaders, Display, TEXT("Finished shader compile request in %.2f seconds."), FPlatformTime::Seconds() - StartTime);

	// Provide a log of what happened.
	GShaderCompilingManager->PrintStats(true);

	// Restore compilation state.
	GShaderCompilingManager->SkipShaderCompilation(bPreviousState);
}
#endif // WITH_EDITOR

void BeginRecompileGlobalShaders(const TArray<const FShaderType*>& OutdatedShaderTypes, const TArray<const FShaderPipelineType*>& OutdatedShaderPipelineTypes, EShaderPlatform ShaderPlatform, const ITargetPlatform* TargetPlatform)
{
	if (!FPlatformProperties::RequiresCookedData())
	{
		// Flush pending accesses to the existing global shaders.
		FlushRenderingCommands();

		// Calling CompileGlobalShaderMap will force starting the compile jobs if the map is empty (by calling VerifyGlobalShaders)
		CompileGlobalShaderMap(ShaderPlatform, TargetPlatform, false);
		FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(ShaderPlatform);

		// Now check if there is any work to be done wrt outdates types
		if (OutdatedShaderTypes.Num() > 0 || OutdatedShaderPipelineTypes.Num() > 0)
		{
			VerifyGlobalShaders(ShaderPlatform, TargetPlatform, false, &OutdatedShaderTypes, &OutdatedShaderPipelineTypes);
		}
	}
}

void FinishRecompileGlobalShaders()
{
	// Block until global shaders have been compiled and processed
	GShaderCompilingManager->ProcessAsyncResults(false, true);
}