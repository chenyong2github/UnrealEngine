// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Extensions.Logging;
using System.Collections.Generic;
using System.IO;
using UnrealBuildBase;

namespace UnrealBuildTool
{
	internal class VcSpecificFileAction : Action, ISpecificFileAction
	{
		DirectoryReference SourceDir;
		DirectoryReference OutputDir;
		VCCompileAction BaseAction;
		int SingleFileCounter;

		internal VcSpecificFileAction(DirectoryReference Source, DirectoryReference Output, VCCompileAction Action) : base(ActionType.Compile)
		{
			SourceDir = Source;
			OutputDir = Output;
			BaseAction = Action;
		}

		public VcSpecificFileAction(BinaryArchiveReader Reader) : base(ActionType.Compile)
		{
			SourceDir = Reader.ReadCompactDirectoryReference();
			OutputDir = Reader.ReadCompactDirectoryReference();
			BaseAction = new VCCompileAction(Reader);
		}

		public new void Write(BinaryArchiveWriter Writer)
		{
			Writer.WriteCompactDirectoryReference(SourceDir);
			Writer.WriteCompactDirectoryReference(OutputDir);
			BaseAction.Write(Writer);
		}

		public DirectoryReference RootDirectory { get => SourceDir; }

		public IExternalAction? CreateAction(FileItem SourceFile, ILogger Logger)
		{
			// If it is a header file we need to wrap it from another file.. because otherwise it will fail when there are circular dependencies
			// (There are a lot of those, very few of the .h files in core can be compiled without errors)
			if (SourceFile.HasExtension(".h"))
			{
				FileItem DummyFile = FileItem.GetItemByFileReference(FileReference.Combine(OutputDir, "SingleFile", SourceFile.Name));
				Directory.CreateDirectory(DummyFile.Directory.FullName);
				File.WriteAllText(DummyFile.FullName, $"#include \"{SourceFile.FullName.Replace('\\', '/')}\"");
				SourceFile = DummyFile;
			}
			else if (!SourceFile.HasExtension(".cpp"))
			{
				return null;
			}

			FileItem ResponseFile = FileItem.GetItemByFileReference(FileReference.Combine(OutputDir, $"SingleFile{SingleFileCounter}.rsp"));
			FileItem ObjectFile = FileItem.GetItemByFileReference(FileReference.Combine(OutputDir, $"SingleFile{SingleFileCounter}.obj"));
			++SingleFileCounter;

			VCCompileAction Action = new VCCompileAction(BaseAction);
			Action.ObjectFile = ObjectFile;
			Action.SourceFile = SourceFile;
			FileItem TempFile = FileItem.GetItemByPath(System.IO.Path.GetTempFileName());
			Action.ResponseFile = ResponseFile;
			List<string> Arguments = Action.GetCompilerArguments(Logger);
			System.IO.File.WriteAllLines(ResponseFile.FullName, Arguments);
			return Action;
		}
	}

	class VcSpecificFileActionSerializer : ActionSerializerBase<VcSpecificFileAction>
	{
		/// <inheritdoc/>
		public override VcSpecificFileAction Read(BinaryArchiveReader Reader)
		{
			return new VcSpecificFileAction(Reader);
		}

		/// <inheritdoc/>
		public override void Write(BinaryArchiveWriter Writer, VcSpecificFileAction Action)
		{
			Action.Write(Writer);
		}
	}

}