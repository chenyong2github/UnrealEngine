// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBuildWorker.h"
#include "Features/IModularFeatures.h"
#include "HAL/FileManager.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"
#include "Misc/StringBuilder.h"
#include "Templates/Tuple.h"
#include "TextureBuildFunction.h"

// TODO: These explicit Win64 worker factories will be replaced a worker discovery mechanism possibly using the target receipt files produced by the build system.

class FBaseTextureBuildWorkerFactory final : public UE::DerivedData::IBuildWorkerFactory
{
public:
	FBaseTextureBuildWorkerFactory()
	: EngineDir(FPaths::EngineDir())
	, bEnabled(true)
	{
		GetAllExecutablePaths(ExecutablePaths);

		int32 MetaIndex = 0;
		ExecutableMeta.AddDefaulted(ExecutablePaths.Num());
		for (const FWorkerPath& ExecutablePath : ExecutablePaths)
		{
			FWorkerPathMeta& Meta = ExecutableMeta[MetaIndex];

			FString LocalExecutablePath = ExecutablePath.LocalPath();

			if (TUniquePtr<FArchive> Ar{IFileManager::Get().CreateFileReader(*LocalExecutablePath, FILEREAD_Silent)})
			{
				const int64 TotalSize = Ar->TotalSize();
				FUniqueBuffer MutableBuffer = FUniqueBuffer::Alloc(uint64(TotalSize));
				Ar->Serialize(MutableBuffer.GetData(), TotalSize);
				Meta.Key = FIoHash::HashBuffer(MutableBuffer.GetView());
				Meta.Value = uint64(TotalSize);
				Ar->Close();
			}
			else
			{
				bEnabled = false;
			}
			++MetaIndex;
		}

		if (bEnabled)
		{
			IModularFeatures::Get().RegisterModularFeature(IBuildWorkerFactory::GetFeatureName(), this);
		}
	}

	virtual ~FBaseTextureBuildWorkerFactory()
	{
		if (bEnabled)
		{
			IModularFeatures::Get().UnregisterModularFeature(IBuildWorkerFactory::GetFeatureName(), this);
		}
	}

	static FGuid ComputeTextureBuildVersion(const TCHAR* InGuidStr)
	{
		static const FGuid TextureDerivedDataVersion(TEXT("a24fc8e0-42cb-49e8-bcd2-c8c4aa064bbd"));
		UE::DerivedData::FBuildVersionBuilder Builder;
		Builder << TextureDerivedDataVersion;
		Builder << FGuid(InGuidStr);
		return Builder.Build();
	}

	void Build(UE::DerivedData::FBuildWorkerBuilder& Builder) final
	{
		Builder.SetName(TEXT("BaseTextureBuildWorker"));
		Builder.SetPath(ExecutablePaths[0].RemotePath());
		Builder.SetHostPlatform(TEXT("Win64"));
		Builder.SetBuildSystemVersion(FGuid(TEXT("ac0574e5-62bd-4c2e-84ec-f2efe48c0fef")));

		// Hard coded to match current contents of the various texture build functions linked to this worker.  Will be fetched from target receipt in the future.
		Builder.AddFunction(TEXT("UncompressedTexture"), ComputeTextureBuildVersion(TEXT("c04fe27a-53f6-402e-85b3-648ac6b1ad87")));
		Builder.AddFunction(TEXT("OodleTexture"), ComputeTextureBuildVersion(TEXT("e6b8884f-923a-44a1-8da1-298fb48865b2")));
		Builder.AddFunction(TEXT("DXTTexture"), ComputeTextureBuildVersion(TEXT("c2d5dbc5-131c-4525-a332-843230076d99")));
		Builder.AddFunction(TEXT("ATSCTexture"), ComputeTextureBuildVersion(TEXT("4788dab5-b99c-479f-bc34-6d7df1cf30e3")));
		Builder.AddFunction(TEXT("ETC2Texture"), ComputeTextureBuildVersion(TEXT("af5192f4-351f-422f-b539-f6bd4abadfae")));
		Builder.AddFunction(TEXT("IntelISPCTexCompTexture"), ComputeTextureBuildVersion(TEXT("19d413ad-f529-4687-902a-3b71919cfd72")));

		for (int32 ExecutableIndex = 0; ExecutableIndex < ExecutablePaths.Num(); ++ExecutableIndex)
		{
			Builder.AddExecutable(ExecutablePaths[ExecutableIndex].RemotePath(), ExecutableMeta[ExecutableIndex].Key, ExecutableMeta[ExecutableIndex].Value);
		}
	}

	UE::DerivedData::FRequest FindFileData(TConstArrayView<FIoHash> RawHashes, UE::DerivedData::EPriority Priority, UE::DerivedData::FOnBuildWorkerFileDataComplete&& OnComplete) final
	{
		if (OnComplete)
		{
			TArray<FCompressedBuffer> FileDataBuffers;
			UE::DerivedData::FBuildWorkerFileDataCompleteParams CompleteParams;
			for (const FIoHash& RawHash : RawHashes)
			{
				for (int32 ExecutableIndex = 0; ExecutableIndex < ExecutablePaths.Num(); ++ExecutableIndex)
				{
					FWorkerPathMeta& Meta = ExecutableMeta[ExecutableIndex];
					if (Meta.Key == RawHash)
					{
						if (TUniquePtr<FArchive> Ar{IFileManager::Get().CreateFileReader(*ExecutablePaths[ExecutableIndex].LocalPath(), FILEREAD_Silent)})
						{
							const int64 TotalSize = Ar->TotalSize();
							FUniqueBuffer MutableBuffer = FUniqueBuffer::Alloc(uint64(TotalSize));
							Ar->Serialize(MutableBuffer.GetData(), TotalSize);
							if (Ar->Close())
							{
								FileDataBuffers.Emplace(FCompressedBuffer::Compress(NAME_None, MutableBuffer.MoveToShared()));
							}
						}
					}
				}
			}

			if (FileDataBuffers.Num() == RawHashes.Num())
			{
				CompleteParams.Status = UE::DerivedData::EStatus::Ok;
				CompleteParams.Files = FileDataBuffers;
				OnComplete(MoveTemp(CompleteParams));
			}
			else
			{
				CompleteParams.Status = UE::DerivedData::EStatus::Error;
				OnComplete(MoveTemp(CompleteParams));
			}
		}

		return UE::DerivedData::FRequest();
	}

	struct FWorkerPath
	{
		FStringView LocalPathRoot;
		FStringView LocalPathSuffix;
		FStringView RemotePathRoot;
		FStringView RemotePathSuffix;

		FWorkerPath(FStringView InLocalPathRoot, FStringView InLocalPathSuffix, FStringView InRemotePathRoot = FStringView(), FStringView InRemotePathSuffix = FStringView())
		: LocalPathRoot(InLocalPathRoot)
		, LocalPathSuffix(InLocalPathSuffix)
		, RemotePathRoot(InRemotePathRoot.IsEmpty() ? InLocalPathRoot : InRemotePathRoot)
		, RemotePathSuffix(InRemotePathSuffix.IsEmpty() ? InLocalPathSuffix : InRemotePathSuffix)
		{
		}

		FString LocalPath() const
		{
			TStringBuilder<128> PathBuilder;
			FPathViews::Append(PathBuilder, LocalPathRoot, LocalPathSuffix);
			return PathBuilder.ToString();
		}

		FString RemotePath() const
		{
			TStringBuilder<128> PathBuilder;
			FPathViews::Append(PathBuilder, RemotePathRoot, RemotePathSuffix);
			return PathBuilder.ToString();
		}
	};

	void GetAllExecutablePaths(TArray<FWorkerPath>& OutPaths)
	{
		OutPaths.Emplace(EngineDir, TEXT("Binaries/Win64/BaseTextureBuildWorker.exe"), TEXT("Engine"));
		OutPaths.Emplace(EngineDir, TEXT("Binaries/ThirdParty/nvTextureTools/Win64/AVX2/nvtt_64.dll"), TEXT("Engine"));
		OutPaths.Emplace(EngineDir, TEXT("Binaries/ThirdParty/nvTextureTools/Win64/nvtt_64.dll"), TEXT("Engine"));
		OutPaths.Emplace(EngineDir, TEXT("Binaries/ThirdParty/Intel/ISPCTexComp/Win64-Release/ispc_texcomp.dll"), TEXT("Engine"));
		OutPaths.Emplace(EngineDir, TEXT("Binaries/ThirdParty/QualComm/Win64/TextureConverter.dll"), TEXT("Engine"));
		OutPaths.Emplace(EngineDir, TEXT("Binaries/ThirdParty/ARM/Win32/astcenc.exe"), TEXT("Engine"));
	}

private:
	const FString EngineDir;
	TArray<FWorkerPath> ExecutablePaths;
	using FWorkerPathMeta = TTuple<FIoHash, uint64>;
	TArray<FWorkerPathMeta> ExecutableMeta;
	bool bEnabled;
};

FBaseTextureBuildWorkerFactory GBaseTextureBuildWorkerFactory;
