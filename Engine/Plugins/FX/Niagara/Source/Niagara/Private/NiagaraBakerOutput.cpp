// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraBakerOutput.h"
#include "NiagaraSystem.h"

FString UNiagaraBakerOutput::MakeOutputName() const
{
	return SanitizeOutputName(GetFName().ToString());
}

FString UNiagaraBakerOutput::SanitizeOutputName(FString Name)
{
	FString OutName;
	OutName.Reserve(Name.Len());
	for ( TCHAR ch : Name )
	{
		switch (ch)
		{
			case TCHAR(' '):
			case TCHAR(';'):
			case TCHAR(':'):
			case TCHAR(','):
				ch = TCHAR('_');
				break;
			default:
				break;
		}
		OutName.AppendChar(ch);
	}

	return OutName;
}

#if WITH_EDITOR
void UNiagaraBakerOutput::FindWarnings(TArray<FText>& OutWarnings) const
{
}

FString UNiagaraBakerOutput::GetAssetPath(FString PathFormat, int32 FrameIndex) const
{
	UNiagaraSystem* NiagaraSystem = GetTypedOuter<UNiagaraSystem>();
	check(NiagaraSystem);

	const TMap<FString, FStringFormatArg> PathFormatArgs =
	{
		{TEXT("AssetFolder"),	FString(FPathViews::GetPath(NiagaraSystem->GetPathName()))},
		{TEXT("AssetName"),		NiagaraSystem->GetName()},
		{TEXT("OutputName"),	SanitizeOutputName(OutputName)},
		{TEXT("FrameIndex"),	FString::Printf(TEXT("%03d"), FrameIndex)},
	};
	FString AssetPath = FString::Format(*PathFormat, PathFormatArgs);
	AssetPath.ReplaceInline(TEXT("//"), TEXT("/"));
	return AssetPath;
}

FString UNiagaraBakerOutput::GetAssetFolder(FString PathFormat, int32 FrameIndex) const
{
	const FString AssetPath = GetAssetPath(PathFormat, FrameIndex);
	return FString(FPathViews::GetPath(AssetPath));
}

FString UNiagaraBakerOutput::GetExportPath(FString PathFormat, int32 FrameIndex) const
{
	UNiagaraSystem* NiagaraSystem = GetTypedOuter<UNiagaraSystem>();
	check(NiagaraSystem);

	const TMap<FString, FStringFormatArg> PathFormatArgs =
	{
		{TEXT("SavedDir"),		FPaths::ProjectSavedDir()},
		{TEXT("ProjectDir"),	FPaths::GetProjectFilePath()},
		{TEXT("AssetName"),		NiagaraSystem->GetName()},
		{TEXT("OutputName"),	SanitizeOutputName(OutputName)},
		{TEXT("FrameIndex"),	FString::Printf(TEXT("%03d"), FrameIndex)},
	};
	FString ExportPath = FString::Format(*PathFormat, PathFormatArgs);
	ExportPath.ReplaceInline(TEXT("//"), TEXT("/"));
	return FPaths::ConvertRelativePathToFull(ExportPath);
}

FString UNiagaraBakerOutput::GetExportFolder(FString PathFormat, int32 FrameIndex) const
{
	const FString AssetPath = GetExportPath(PathFormat, FrameIndex);
	return FString(FPathViews::GetPath(AssetPath));
}
#endif

void UNiagaraBakerOutput::PostInitProperties()
{
	Super::PostInitProperties();

	OutputName = MakeOutputName();
}
