// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// Reads the contents of C++ dependency files, and caches them for future iterations.
	/// </summary>
	class CppDependencyCache
	{
		/// <summary>
		/// Contents of a single dependency file
		/// </summary>
		class DependencyInfo
		{
			public long LastWriteTimeUtc;
			public List<FileItem> Files;

			public DependencyInfo(long LastWriteTimeUtc, List<FileItem> Files)
			{
				this.LastWriteTimeUtc = LastWriteTimeUtc;
				this.Files = Files;
			}

			public static DependencyInfo Read(BinaryArchiveReader Reader)
			{
				long LastWriteTimeUtc = Reader.ReadLong();
				List<FileItem> Files = Reader.ReadList(() => Reader.ReadCompactFileItem());

				return new DependencyInfo(LastWriteTimeUtc, Files);
			}

			public void Write(BinaryArchiveWriter Writer)
			{
				Writer.WriteLong(LastWriteTimeUtc);
				Writer.WriteList<FileItem>(Files, File => Writer.WriteCompactFileItem(File));
			}
		}

		/// <summary>
		/// The current file version
		/// </summary>
		public const int CurrentVersion = 2;

		/// <summary>
		/// Location of this dependency cache
		/// </summary>
		FileReference Location;

		/// <summary>
		/// Directory for files to cache dependencies for.
		/// </summary>
		DirectoryReference BaseDir;

		/// <summary>
		/// The parent cache.
		/// </summary>
		CppDependencyCache Parent;

		/// <summary>
		/// Map from file item to dependency info
		/// </summary>
		ConcurrentDictionary<FileItem, DependencyInfo> DependencyFileToInfo = new ConcurrentDictionary<FileItem, DependencyInfo>();

		/// <summary>
		/// Whether the cache has been modified and needs to be saved
		/// </summary>
		bool bModified;

		/// <summary>
		/// Static cache of all constructed dependency caches
		/// </summary>
		static Dictionary<FileReference, CppDependencyCache> Caches = new Dictionary<FileReference, CppDependencyCache>();

		/// <summary>
		/// Constructs a dependency cache. This method is private; call CppDependencyCache.Create() to create a cache hierarchy for a given project.
		/// </summary>
		/// <param name="Location">File to store the cache</param>
		/// <param name="BaseDir">Base directory for files that this cache should store data for</param>
		/// <param name="Parent">The parent cache to use</param>
		private CppDependencyCache(FileReference Location, DirectoryReference BaseDir, CppDependencyCache Parent)
		{
			this.Location = Location;
			this.BaseDir = BaseDir;
			this.Parent = Parent;

			if(FileReference.Exists(Location))
			{
				Read();
			}
		}

		/// <summary>
		/// Attempts to read the dependencies from the given input file
		/// </summary>
		/// <param name="InputFile">File to be read</param>
		/// <param name="OutDependencyItems">Receives a list of output items</param>
		/// <returns>True if the input file exists and the dependencies were read</returns>
		public bool TryGetDependencies(FileItem InputFile, out List<FileItem> OutDependencyItems)
		{
			if(!InputFile.Exists)
			{
				OutDependencyItems = null;
				return false;
			}

			try
			{
				return TryGetDependenciesInternal(InputFile, out OutDependencyItems);
			}
			catch(Exception Ex)
			{
				Log.TraceLog("Unable to read {0}:\n{1}", InputFile, ExceptionUtils.FormatExceptionDetails(Ex));
				OutDependencyItems = null;
				return false;
			}
		}

		/// <summary>
		/// Attempts to read dependencies from the given file.
		/// </summary>
		/// <param name="InputFile">File to be read</param>
		/// <param name="OutDependencyItems">Receives a list of output items</param>
		/// <returns>True if the input file exists and the dependencies were read</returns>
		private bool TryGetDependenciesInternal(FileItem InputFile, out List<FileItem> OutDependencyItems)
		{
			if(Parent != null && !InputFile.Location.IsUnderDirectory(BaseDir))
			{
				return Parent.TryGetDependencies(InputFile, out OutDependencyItems);
			}
			else
			{
				DependencyInfo Info;
				if(DependencyFileToInfo.TryGetValue(InputFile, out Info) && InputFile.LastWriteTimeUtc.Ticks <= Info.LastWriteTimeUtc)
				{
					OutDependencyItems = Info.Files;
					return true;
				}

				List<FileItem> DependencyItems = ReadDependenciesFile(InputFile.Location);
				DependencyFileToInfo.TryAdd(InputFile, new DependencyInfo(InputFile.LastWriteTimeUtc.Ticks, DependencyItems));
				bModified = true;

				OutDependencyItems = DependencyItems;
				return true;
			}
		}

		/// <summary>
		/// Creates a cache hierarchy for a particular target
		/// </summary>
		/// <param name="ProjectFile">Project file for the target being built</param>
		/// <param name="TargetName">Name of the target</param>
		/// <param name="Platform">Platform being built</param>
		/// <param name="Configuration">Configuration being built</param>
		/// <param name="TargetType">The target type</param>
		/// <param name="Architecture">The target architecture</param>
		/// <returns>Dependency cache hierarchy for the given project</returns>
		public static CppDependencyCache CreateHierarchy(FileReference ProjectFile, string TargetName, UnrealTargetPlatform Platform, UnrealTargetConfiguration Configuration, TargetType TargetType, string Architecture)
		{
			CppDependencyCache Cache = null;

			if(ProjectFile == null || !UnrealBuildTool.IsEngineInstalled())
			{
				string AppName;
				if(TargetType == TargetType.Program)
				{
					AppName = TargetName;
				}
				else
				{
					AppName = UEBuildTarget.GetAppNameForTargetType(TargetType);
				}

				FileReference EngineCacheLocation = FileReference.Combine(UnrealBuildTool.EngineDirectory, UEBuildTarget.GetPlatformIntermediateFolder(Platform, Architecture), AppName, Configuration.ToString(), "DependencyCache.bin");
				Cache = FindOrAddCache(EngineCacheLocation, UnrealBuildTool.EngineDirectory, Cache);
			}

			if(ProjectFile != null)
			{
				FileReference ProjectCacheLocation = FileReference.Combine(ProjectFile.Directory, UEBuildTarget.GetPlatformIntermediateFolder(Platform, Architecture), TargetName, Configuration.ToString(), "DependencyCache.bin");
				Cache = FindOrAddCache(ProjectCacheLocation, ProjectFile.Directory, Cache);
			}

			return Cache;
		}

		/// <summary>
		/// Reads a cache from the given location, or creates it with the given settings
		/// </summary>
		/// <param name="Location">File to store the cache</param>
		/// <param name="BaseDir">Base directory for files that this cache should store data for</param>
		/// <param name="Parent">The parent cache to use</param>
		/// <returns>Reference to a dependency cache with the given settings</returns>
		static CppDependencyCache FindOrAddCache(FileReference Location, DirectoryReference BaseDir, CppDependencyCache Parent)
		{
			lock(Caches)
			{
				CppDependencyCache Cache;
				if(Caches.TryGetValue(Location, out Cache))
				{
					Debug.Assert(Cache.BaseDir == BaseDir);
					Debug.Assert(Cache.Parent == Parent);
				}
				else
				{
					Cache = new CppDependencyCache(Location, BaseDir, Parent);
					Caches.Add(Location, Cache);
				}
				return Cache;
			}
		}

		/// <summary>
		/// Save all the caches that have been modified
		/// </summary>
		public static void SaveAll()
		{
			Parallel.ForEach(Caches.Values, Cache => { if(Cache.bModified){ Cache.Write(); } });
		}

		/// <summary>
		/// Reads data for this dependency cache from disk
		/// </summary>
		private void Read()
		{
			try
			{
				using(BinaryArchiveReader Reader = new BinaryArchiveReader(Location))
				{
					int Version = Reader.ReadInt();
					if(Version != CurrentVersion)
					{
						Log.TraceLog("Unable to read dependency cache from {0}; version {1} vs current {2}", Location, Version, CurrentVersion);
						return;
					}

					int Count = Reader.ReadInt();
					for(int Idx = 0; Idx < Count; Idx++)
					{
						FileItem File = Reader.ReadFileItem();
						DependencyFileToInfo[File] = DependencyInfo.Read(Reader);
					}
				}
			}
			catch(Exception Ex)
			{
				Log.TraceWarning("Unable to read {0}. See log for additional information.", Location);
				Log.TraceLog("{0}", ExceptionUtils.FormatExceptionDetails(Ex));
			}
		}

		/// <summary>
		/// Writes data for this dependency cache to disk
		/// </summary>
		private void Write()
		{
			DirectoryReference.CreateDirectory(Location.Directory);
			using(FileStream Stream = File.Open(Location.FullName, FileMode.Create, FileAccess.Write, FileShare.Read))
			{
				using(BinaryArchiveWriter Writer = new BinaryArchiveWriter(Stream))
				{
					Writer.WriteInt(CurrentVersion);

					Writer.WriteInt(DependencyFileToInfo.Count);
					foreach(KeyValuePair<FileItem, DependencyInfo> Pair in DependencyFileToInfo)
					{
						Writer.WriteFileItem(Pair.Key);
						Pair.Value.Write(Writer);
					}
				}
			}
			bModified = false;
		}

		/// <summary>
		/// Reads dependencies from the given file.
		/// </summary>
		/// <param name="InputFile">The file to read from</param>
		/// <returns>List of included dependencies</returns>
		static List<FileItem> ReadDependenciesFile(FileReference InputFile)
		{
			if(InputFile.HasExtension(".txt"))
			{
				string[] Lines = FileReference.ReadAllLines(InputFile);

				HashSet<FileItem> DependencyItems = new HashSet<FileItem>();
				foreach(string Line in Lines)
				{
					if(Line.Length > 0)
					{
						// Ignore *.tlh and *.tli files generated by the compiler from COM DLLs
						if(!Line.EndsWith(".tlh", StringComparison.OrdinalIgnoreCase) && !Line.EndsWith(".tli", StringComparison.OrdinalIgnoreCase))
						{
							DependencyItems.Add(FileItem.GetItemByPath(Line));
						}
					}
				}
				return DependencyItems.ToList();
			}
			else if(InputFile.HasExtension(".d"))
			{
				string Text = FileReference.ReadAllText(InputFile);

				List<FileItem> NewDependencyFiles = new List<FileItem>();

				// Currently expect one file per line
				int Index = Text.IndexOf(':');
				if(Index != -1)
				{
					Index++;
					for (; ; )
					{
						int EndIndex = Text.IndexOf('\n', Index);
						if(EndIndex == -1)
						{
							break;
						}

						int FileEndIndex = EndIndex;
						if(FileEndIndex > Index && Text[FileEndIndex - 1] == '\r')
						{
							FileEndIndex--;
						}
						if(FileEndIndex > Index && Text[FileEndIndex - 1] == '\\')
						{
							FileEndIndex--;
						}

						string File = Text.Substring(Index, FileEndIndex - Index).Trim();
						if (File.Length > 0)
						{
							NewDependencyFiles.Add(FileItem.GetItemByPath(File));
						}

						Index = EndIndex + 1;
					}

				}

				return NewDependencyFiles;
			}
			else
			{
				throw new BuildException("Unknown dependency list file type: {0}", InputFile);
			}
		}
	}
}
