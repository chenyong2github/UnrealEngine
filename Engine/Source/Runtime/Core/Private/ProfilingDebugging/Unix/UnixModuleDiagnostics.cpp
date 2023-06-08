// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "ProfilingDebugging/ModuleDiagnostics.h"
#include <link.h>

////////////////////////////////////////////////////////////////////////////////
void Modules_Initialize()
{
#if !UE_BUILD_SHIPPING
	using namespace UE::Trace;

	constexpr uint32 SizeOfSymbolFormatString = 5;
	UE_TRACE_LOG(Diagnostics, ModuleInit, ModuleChannel, sizeof(TCHAR) * SizeOfSymbolFormatString)
		<< ModuleInit.SymbolFormat("dwarf", SizeOfSymbolFormatString)
		<< ModuleInit.ModuleBaseShift(uint8(0));

	auto IterateCallback = [](struct dl_phdr_info *Info, size_t /*size*/, void *Data)
	{
		int32 TotalMemSize = 0;
		uint64 RealBase = Info->dlpi_addr;
		bool bRealBaseSet = false;

		constexpr uint32 GNUSectionNameSize = 4;
		constexpr uint32 BuildIdSize = 20;
		uint8 BuildId[BuildIdSize] = {0};
		FMemory::Memzero(BuildId, BuildIdSize);
		bool bBuildIdSet = false;
		for (int SectionIdx = 0; SectionIdx < Info->dlpi_phnum; SectionIdx++)
		{
			TotalMemSize += Info->dlpi_phdr[SectionIdx].p_memsz;

			uint64 Offset = Info->dlpi_addr + Info->dlpi_phdr[SectionIdx].p_vaddr;
			uint32 Type = Info->dlpi_phdr[SectionIdx].p_type;

			if (!bRealBaseSet && Type == PT_LOAD)
			{
				RealBase = Offset;
				bRealBaseSet = true;
			}
			if (!bBuildIdSet && Type == PT_NOTE)
			{
				ElfW(Nhdr)* Note = (ElfW(Nhdr)*)Offset;
				char* NoteName = (char*)Note + sizeof(ElfW(Nhdr));
				uint8* NoteDesc = (uint8*)Note + sizeof(ElfW(Nhdr)) + Note->n_namesz;
				if (Note->n_namesz == GNUSectionNameSize && Note->n_type == NT_GNU_BUILD_ID && strcmp(NoteName, ELF_NOTE_GNU) == 0)
				{
					FMemory::Memcpy(BuildId, NoteDesc, FMath::Min(BuildIdSize, Note->n_descsz));
					bBuildIdSet = true;
				}
			}
		}

		FString ImageName = FString(Info->dlpi_name);
		if (ImageName.IsEmpty())
		{
			constexpr bool bRemoveExtension = false;
			ImageName = FPlatformProcess::ExecutableName(bRemoveExtension);
		}
		ImageName = FPaths::GetCleanFilename(ImageName);

		UE_TRACE_LOG(Diagnostics, ModuleLoad, ModuleChannel, sizeof(TCHAR) * ImageName.Len() + BuildIdSize)
			<< ModuleLoad.Name(*ImageName, ImageName.Len())
			<< ModuleLoad.Base(RealBase)
			<< ModuleLoad.Size(TotalMemSize)
			<< ModuleLoad.ImageId(BuildId, BuildIdSize);

		return 0;
	};
	dl_iterate_phdr(IterateCallback, nullptr);
#endif
}
