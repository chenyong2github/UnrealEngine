// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Interfaces/ITextureFormatManagerModule.h"
#include "Interfaces/ITextureFormat.h"
#include "Interfaces/ITextureFormatModule.h"
#include "Modules/ModuleManager.h"
#include "TextureFormatManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogTextureFormatManager, Log, All);

/**
 * Module for the target platform manager
 */
class FTextureFormatManagerModule
	: public ITextureFormatManagerModule
{
public:

	/** Default constructor. */
	FTextureFormatManagerModule()
		: ModuleName(TEXT("TextureFormat"))
		, bForceCacheUpdate(true)
		, bIgnoreFirstDelegateCall(true)
	{
		// Calling a virtual function from a constructor, but with no expectation that a derived implementation of this
		// method would be called.  This is solely to avoid duplicating code in this implementation, not for polymorphism.
		FTextureFormatManagerModule::Invalidate();

		FModuleManager::Get().OnModulesChanged().AddRaw(this, &FTextureFormatManagerModule::ModulesChangesCallback);
	}

	/** Destructor. */
	virtual ~FTextureFormatManagerModule() = default;

	virtual void ShutdownModule()
	{
		FModuleManager::Get().OnModulesChanged().RemoveAll(this);
	}


	virtual const TArray<const ITextureFormat*>& GetTextureFormats() override
	{
		static bool bInitialized = false;

		if (!bInitialized || bForceCacheUpdate)
		{
			bInitialized = true;
			TextureFormats.Empty(TextureFormats.Num());
			TextureFormatMetadata.Empty(TextureFormatMetadata.Num());

			TArray<FName> Modules;

			FModuleManager::Get().FindModules(TEXT("*TextureFormat*"), Modules);

			if (!Modules.Num())
			{
				UE_LOG(LogTextureFormatManager, Error, TEXT("No texture formats found!"));
			}

			for (int32 Index = 0; Index < Modules.Num(); Index++)
			{
				if (Modules[Index] != ModuleName) // Avoid our own module when going through this list that was gathered by name
				{
					ITextureFormatModule* Module = FModuleManager::LoadModulePtr<ITextureFormatModule>(Modules[Index]);
					if (Module)
					{
						ITextureFormat* Format = Module->GetTextureFormat();
						if (Format != nullptr)
						{
							TextureFormats.Add(Format);
							FTextureFormatMetadata& NewMeta = TextureFormatMetadata.AddDefaulted_GetRef();
							NewMeta.ModuleName = Modules[Index];
							NewMeta.Module = Module;
						}
					}
				}
			}
		}

		return TextureFormats;
	}

	virtual const ITextureFormat* FindTextureFormat(FName Name) override
	{
		FName ModuleNameUnused;
		ITextureFormatModule* ModuleUnused;
		return FindTextureFormatAndModule(Name, ModuleNameUnused, ModuleUnused);
	}
	
	virtual const class ITextureFormat* FindTextureFormatAndModule(FName Name, FName& OutModuleName, ITextureFormatModule*& OutModule) override
	{
		// Called to ensure the arrays are populated
		GetTextureFormats();

		for (int32 Index = 0; Index < TextureFormats.Num(); Index++)
		{
			TArray<FName> Formats;

			TextureFormats[Index]->GetSupportedFormats(Formats);

			for (int32 FormatIndex = 0; FormatIndex < Formats.Num(); FormatIndex++)
			{
				if (Formats[FormatIndex] == Name)
				{
					const FTextureFormatMetadata& FoundMeta = TextureFormatMetadata[Index];
					OutModuleName = FoundMeta.ModuleName;
					OutModule = FoundMeta.Module;
					return TextureFormats[Index];
				}
			}
		}

		return nullptr;
	}

	virtual void Invalidate() override
	{
		bForceCacheUpdate = true;
		GetTextureFormats();
		bForceCacheUpdate = false;
	}

private:

	void ModulesChangesCallback(FName InModuleName, EModuleChangeReason ReasonForChange)
	{
		if (!bIgnoreFirstDelegateCall && (InModuleName != ModuleName) && InModuleName.ToString().Contains(TEXT("TextureFormat")))
		{
			Invalidate();
		}
		bIgnoreFirstDelegateCall = false;
	}

	FName ModuleName;

	TArray<const ITextureFormat*> TextureFormats;

	struct FTextureFormatMetadata
	{
		FName ModuleName;
		ITextureFormatModule* Module;
	};
	TArray<FTextureFormatMetadata> TextureFormatMetadata;

	// Flag to force reinitialization of all cached data. This is needed to have up-to-date caches
	// in case of a module reload of a TextureFormat-Module.
	bool bForceCacheUpdate;

	// Flag to avoid redunant reloads
	bool bIgnoreFirstDelegateCall;

};

IMPLEMENT_MODULE(FTextureFormatManagerModule, TextureFormat);
