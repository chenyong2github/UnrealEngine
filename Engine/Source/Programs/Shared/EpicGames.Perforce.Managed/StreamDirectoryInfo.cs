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
	public class StreamDirectoryInfo
	{
		/// <summary>
		/// Map of name to file within the directory
		/// </summary>
		public Dictionary<ReadOnlyUtf8String, StreamFileInfo> NameToFile = new Dictionary<ReadOnlyUtf8String, StreamFileInfo>();

		/// <summary>
		/// Map of name to subdirectory
		/// </summary>
		public Dictionary<ReadOnlyUtf8String, IoHash> NameToSubDirectory = new Dictionary<ReadOnlyUtf8String, IoHash>(FileUtils.PlatformPathComparerUtf8);
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
		public static StreamDirectoryInfo ReadStreamDirectoryInfo(this MemoryReader Reader)
		{
			StreamDirectoryInfo DirectoryInfo = new StreamDirectoryInfo();

			int NumFiles = Reader.ReadInt32();
			for (int Idx = 0; Idx < NumFiles; Idx++)
			{
				StreamFileInfo FileInfo = Reader.ReadStreamFileInfo();
				DirectoryInfo.NameToFile[FileInfo.Name] = FileInfo;
			}

			int NumSubDirectories = Reader.ReadInt32();
			for (int Idx = 0; Idx < NumSubDirectories; Idx++)
			{
				ReadOnlyUtf8String Name = Reader.ReadString();
				DirectoryInfo.NameToSubDirectory[Name] = Reader.ReadIoHash();
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
			Writer.WriteInt32(DirectoryInfo.NameToFile.Count);
			foreach (StreamFileInfo File in DirectoryInfo.NameToFile.Values)
			{
				Writer.WriteStreamFileInfo(File);
			}

			Writer.WriteInt32(DirectoryInfo.NameToSubDirectory.Count);
			foreach ((ReadOnlyUtf8String Name, IoHash Hash) in DirectoryInfo.NameToSubDirectory)
			{
				Writer.WriteString(Name);
				Writer.WriteIoHash(Hash);
			}
		}

		/// <summary>
		/// Gets the total size of this object when serialized to disk
		/// </summary>
		/// <returns>The serialized size of this object</returns>
		public static int GetSerializedSize(this StreamDirectoryInfo DirectoryInfo)
		{
			return sizeof(int) + DirectoryInfo.NameToFile.Values.Sum(x => x.GetSerializedSize()) + sizeof(int) + DirectoryInfo.NameToSubDirectory.Sum(x => x.Key.GetSerializedSize() + IoHash.NumBytes);
		}
	}
}
