// Copyright Epic Games, Inc. All Rights Reserved.
#include "CADKernel/Core/Session.h"

#include "CADKernel/UI/Message.h"

using namespace CADKernel;


void FSession::SaveDatabase(const TCHAR* FileName)
{
	TSharedPtr<FCADKernelArchive> Archive = FCADKernelArchive::CreateArchiveWriter(*this, FileName);
	if (!Archive.IsValid())
	{
		FMessage::Printf(Log, TEXT("The archive file %s is corrupted\n"), FileName);
	}

	Database.Serialize(*Archive.Get());
	Archive->Close();
}

TSharedRef<FModel> FSession::GetModel()
{
	return Database.GetModel();
}


void FSession::SaveDatabase(const TCHAR* FileName, const TArray<TSharedPtr<FEntity>>& SelectedEntities)
{
	TArray<FIdent> EntityIds;
	EntityIds.Reserve(SelectedEntities.Num());

	SpawnEntityIdent(SelectedEntities, true);

	for (const TSharedPtr<FEntity>& Entity : SelectedEntities)
	{
		EntityIds.Add(Entity->GetId());
	}

	TSharedPtr<FCADKernelArchive> Archive = FCADKernelArchive::CreateArchiveWriter(*this, FileName);

	Database.SerializeSelection(*Archive.Get(), EntityIds);
	Archive->Close();
}

void FSession::LoadDatabase(const TCHAR* FilePath)
{
	TSharedPtr<FCADKernelArchive> Archive = FCADKernelArchive::CreateArchiveReader(*this, FilePath);
	if (!Archive.IsValid())
	{
		FMessage::Printf(Log, TEXT("The archive file %s is corrupted\n"), FilePath);
	}

	Database.Deserialize(*Archive.Get());
}
