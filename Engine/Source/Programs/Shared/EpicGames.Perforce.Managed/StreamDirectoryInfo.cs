// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Perforce.Managed
{
	/// <summary>
	/// Information about a directory within a stream
	/// </summary>
	class StreamDirectoryInfo
	{
		/// <summary>
		/// The current signature for saved directory objects
		/// </summary>
		static readonly byte[] CurrentSignature = { (byte)'W', (byte)'S', (byte)'D', 2 };

		/// <summary>
		/// The directory name
		/// </summary>
		public readonly ReadOnlyUtf8String Name;

		/// <summary>
		/// The parent directory
		/// </summary>
		public readonly StreamDirectoryInfo? ParentDirectory;

		/// <summary>
		/// Map of name to file within the directory
		/// </summary>
		public Dictionary<ReadOnlyUtf8String, StreamFileInfo> NameToFile = new Dictionary<ReadOnlyUtf8String, StreamFileInfo>();

		/// <summary>
		/// Map of name to subdirectory
		/// </summary>
		public Dictionary<ReadOnlyUtf8String, StreamDirectoryInfo> NameToSubDirectory = new Dictionary<ReadOnlyUtf8String, StreamDirectoryInfo>(FileUtils.PlatformPathComparerUtf8);

		/// <summary>
		/// Constructor
		/// </summary>
		public StreamDirectoryInfo()
			: this("", null)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name">Name of the directory</param>
		/// <param name="ParentDirectory">Parent directory</param>
		public StreamDirectoryInfo(ReadOnlyUtf8String Name, StreamDirectoryInfo? ParentDirectory)
		{
			this.Name = Name;
			this.ParentDirectory = ParentDirectory;
		}

		/// <summary>
		/// Load a stream directory from a file on disk
		/// </summary>
		/// <param name="InputFile">File to read from</param>
		/// <param name="CancellationToken">Cancellation token</param>
		/// <returns>New StreamDirectoryInfo object</returns>
		public static async Task<StreamDirectoryInfo> LoadAsync(FileReference InputFile, CancellationToken CancellationToken)
		{
			byte[] Data = await FileReference.ReadAllBytesAsync(InputFile);
			if (Data.Length < CurrentSignature.Length)
			{
				throw new InvalidDataException(String.Format("Unable to read signature bytes from {0}", InputFile));
			}
			if (!Enumerable.SequenceEqual(Data.Take(CurrentSignature.Length), CurrentSignature))
			{
				throw new InvalidDataException(String.Format("Cached stream contents at {0} has incorrect signature", InputFile));
			}

			MemoryReader Reader = new MemoryReader(Data.AsMemory(CurrentSignature.Length));
			return Reader.ReadStreamDirectoryInfo(null);
		}

		/// <summary>
		/// Saves the contents of this object to disk
		/// </summary>
		/// <param name="OutputFile">The output file to write to</param>
		public async Task Save(FileReference OutputFile)
		{
			using (FileStream OutputStream = FileReference.Open(OutputFile, FileMode.Create, FileAccess.Write, FileShare.Read))
			{
				await OutputStream.WriteAsync(CurrentSignature, 0, CurrentSignature.Length);

				byte[] Data = new byte[this.GetSerializedSize()];
				MemoryWriter Writer = new MemoryWriter(Data.AsMemory());
				Writer.WriteStreamDirectoryInfo(this);
				await OutputStream.WriteAsync(Data, 0, Data.Length);
			}
		}

		/// <summary>
		/// Get all the files in this directory
		/// </summary>
		/// <returns>List of files</returns>
		public List<StreamFileInfo> GetFiles()
		{
			List<StreamFileInfo> Files = new List<StreamFileInfo>();
			AppendFiles(Files);
			return Files;
		}

		/// <summary>
		/// Append the contents of this directory and subdirectories to a list
		/// </summary>
		/// <param name="Files">List to append to</param>
		public void AppendFiles(List<StreamFileInfo> Files)
		{
			foreach (StreamDirectoryInfo SubDirectory in NameToSubDirectory.Values)
			{
				SubDirectory.AppendFiles(Files);
			}
			Files.AddRange(NameToFile.Values);
		}

		/// <summary>
		/// Gets the relative path to this directory, with a trailing slash
		/// </summary>
		/// <returns>Relative path to this directory</returns>
		public string GetRelativePath()
		{
			StringBuilder Builder = new StringBuilder();
			AppendPath(Builder);
			return Builder.ToString();
		}

		/// <summary>
		/// Append the relative path to this directory to the given string builder
		/// </summary>
		/// <param name="Builder">String builder to append to</param>
		public void AppendPath(StringBuilder Builder)
		{
			if (ParentDirectory != null)
			{
				ParentDirectory.AppendPath(Builder);
				Builder.Append(Name);
			}
			Builder.Append('/');
		}

		/// <summary>
		/// Format the path to the directory for debugging
		/// </summary>
		/// <returns>Path to the directory</returns>
		public override string ToString()
		{
			return GetRelativePath();
		}
	}

	/// <summary>
	/// Extension methods for serializing StreamDirectoryInfo objects
	/// </summary>
	static class StreamDirectoryInfoExtensions
	{
		/// <summary>
		/// Constructor for reading from disk
		/// </summary>
		/// <param name="Reader">The reader to serialize from</param>
		/// <param name="ParentDirectory">The parent directory to initialize this directory with</param>
		public static StreamDirectoryInfo ReadStreamDirectoryInfo(this MemoryReader Reader, StreamDirectoryInfo? ParentDirectory)
		{
			ReadOnlyUtf8String Name = Reader.ReadString();
			StreamDirectoryInfo DirectoryInfo = new StreamDirectoryInfo(Name, ParentDirectory);

			int NumFiles = Reader.ReadInt32();
			for (int Idx = 0; Idx < NumFiles; Idx++)
			{
				StreamFileInfo FileInfo = Reader.ReadStreamFileInfo(DirectoryInfo);
				DirectoryInfo.NameToFile[FileInfo.Name] = FileInfo;
			}

			int NumSubDirectories = Reader.ReadInt32();
			for (int Idx = 0; Idx < NumSubDirectories; Idx++)
			{
				StreamDirectoryInfo SubDirectoryInfo = Reader.ReadStreamDirectoryInfo(ParentDirectory);
				DirectoryInfo.NameToSubDirectory[SubDirectoryInfo.Name] = SubDirectoryInfo;
			}

			return DirectoryInfo;
		}

		/// <summary>
		/// Writes the contents of this stream to disk
		/// </summary>
		/// <param name="Writer">Writer to serialize to</param>
		/// <param name="DirectoryInfo">The directory info to serialize</param>
		public static void WriteStreamDirectoryInfo(this MemoryWriter Writer, StreamDirectoryInfo DirectoryInfo)
		{
			Writer.WriteString(DirectoryInfo.Name);

			Writer.WriteInt32(DirectoryInfo.NameToFile.Count);
			foreach (StreamFileInfo File in DirectoryInfo.NameToFile.Values)
			{
				Writer.WriteStreamFileInfo(File);
			}

			Writer.WriteInt32(DirectoryInfo.NameToSubDirectory.Count);
			foreach (StreamDirectoryInfo SubDirectory in DirectoryInfo.NameToSubDirectory.Values)
			{
				Writer.WriteStreamDirectoryInfo(SubDirectory);
			}
		}

		/// <summary>
		/// Gets the total size of this object when serialized to disk
		/// </summary>
		/// <returns>The serialized size of this object</returns>
		public static int GetSerializedSize(this StreamDirectoryInfo DirectoryInfo)
		{
			return DirectoryInfo.Name.GetSerializedSize() + sizeof(int) + DirectoryInfo.NameToFile.Values.Sum(x => x.GetSerializedSize()) + sizeof(int) + DirectoryInfo.NameToSubDirectory.Values.Sum(x => x.GetSerializedSize());
		}
	}
}
