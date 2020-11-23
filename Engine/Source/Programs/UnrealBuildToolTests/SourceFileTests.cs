// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using Microsoft.VisualStudio.TestTools.UnitTesting;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;
using UnrealBuildTool;

namespace UnrealBuildToolTests
{
	/// <summary>
	/// Tests for reading source file markup
	/// </summary>
	[TestClass]
	public class SourceFileTests
	{
		[TestMethod]
		public void Run()
		{
			List<DirectoryReference> BaseDirectories = new List<DirectoryReference>();
			BaseDirectories.Add(DirectoryReference.Combine(UnrealBuildTool.UnrealBuildTool.EngineSourceDirectory, "Runtime"));
			BaseDirectories.Add(DirectoryReference.Combine(UnrealBuildTool.UnrealBuildTool.EngineSourceDirectory, "Developer"));
			BaseDirectories.Add(DirectoryReference.Combine(UnrealBuildTool.UnrealBuildTool.EngineSourceDirectory, "Editor"));

			foreach(FileReference PluginFile in Plugins.EnumeratePlugins((FileReference)null))
			{
				DirectoryReference PluginSourceDir = DirectoryReference.Combine(PluginFile.Directory, "Source");
				if(DirectoryReference.Exists(PluginSourceDir))
				{
					BaseDirectories.Add(PluginSourceDir);
				}
			}

			ConcurrentBag<SourceFile> SourceFiles = new ConcurrentBag<SourceFile>();
			using(Timeline.ScopeEvent("Scanning source files"))
			{
				using(ThreadPoolWorkQueue Queue = new ThreadPoolWorkQueue())
				{
					foreach(DirectoryReference BaseDirectory in BaseDirectories)
					{
						Queue.Enqueue(() => ParseSourceFiles(DirectoryItem.GetItemByDirectoryReference(BaseDirectory), SourceFiles, Queue));
					}
				}
			}

			Log.TraceLog("Read {0} source files", SourceFiles.Count);

			FileReference TempDataFile = FileReference.Combine(UnrealBuildTool.UnrealBuildTool.EngineDirectory, "Intermediate", "Temp", "SourceFileTests.bin");
			DirectoryReference.CreateDirectory(TempDataFile.Directory);

			using(Timeline.ScopeEvent("Writing source file data"))
			{
				using(BinaryArchiveWriter Writer = new BinaryArchiveWriter(TempDataFile))
				{
					Writer.WriteList(SourceFiles.ToList(), x => x.Write(Writer));
				}
			}

			List<SourceFile> ReadSourceFiles = new List<SourceFile>();
			using(Timeline.ScopeEvent("Reading source file data"))
			{
				using(BinaryArchiveReader Reader = new BinaryArchiveReader(TempDataFile))
				{
					ReadSourceFiles = Reader.ReadList(() => new SourceFile(Reader));
				}
			}
		}

		static void ParseSourceFiles(DirectoryItem Directory, ConcurrentBag<SourceFile> SourceFiles, ThreadPoolWorkQueue Queue)
		{
			foreach(DirectoryItem SubDirectory in Directory.EnumerateDirectories())
			{
				Queue.Enqueue(() => ParseSourceFiles(SubDirectory, SourceFiles, Queue));
			}

			foreach(FileItem File in Directory.EnumerateFiles())
			{
				if(File.HasExtension(".h") || File.HasExtension(".cpp"))
				{
					Queue.Enqueue(() => SourceFiles.Add(new SourceFile(File)));
				}
			}
		}
	}
}
