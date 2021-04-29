// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Perforce;

namespace EpicGames.Perforce.Managed
{
	/// <summary>
	/// Represents a file within a stream
	/// </summary>
	class StreamFileInfo
	{
		/// <summary>
		/// Name of this file
		/// </summary>
		public readonly ReadOnlyUtf8String Name;

		/// <summary>
		/// Length of the file, as reported by the server (actual size on disk may be different due to workspace options).
		/// </summary>
		public readonly long Length;

		/// <summary>
		/// Content id for this file
		/// </summary>
		public readonly FileContentId ContentId;

		/// <summary>
		/// The parent directory
		/// </summary>
		public readonly StreamDirectoryInfo Directory;

		/// <summary>
		/// The depot file and revision that need to be synced for this file
		/// </summary>
		public readonly ReadOnlyUtf8String DepotFileAndRevision;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name">Name of this file</param>
		/// <param name="Length">Length of the file on the server</param>
		/// <param name="ContentId">Content id for this file</param>
		/// <param name="Directory">The parent directory</param>
		/// <param name="DepotFileAndRevision">The depot file and revision that need to be synced for this file</param>
		public StreamFileInfo(ReadOnlyUtf8String Name, long Length, FileContentId ContentId, StreamDirectoryInfo Directory, ReadOnlyUtf8String DepotFileAndRevision)
		{
			this.Name = Name;
			this.Length = Length;
			this.ContentId = ContentId;
			this.Directory = Directory;
			this.DepotFileAndRevision = DepotFileAndRevision;
		}

		/// <summary>
		/// Get the path to this file relative to the root of the stream
		/// </summary>
		/// <returns>Relative path to the file</returns>
		public string GetRelativePath()
		{
			StringBuilder Builder = new StringBuilder();
			Directory.AppendPath(Builder);
			Builder.Append(Name);
			return Builder.ToString();
		}

		/// <summary>
		/// Format the path to the file for the debugger
		/// </summary>
		/// <returns>Path to the file</returns>
		public override string ToString()
		{
			return GetRelativePath();
		}
	}

	/// <summary>
	/// Extension methods for serializing StreamFileInfo objects
	/// </summary>
	static class StreamFileInfoExtensions
	{
		/// <summary>
		/// Constructor for reading a file info from disk
		/// </summary>
		/// <param name="Reader">Binary reader to read data from</param>
		/// <param name="Directory">Parent directory</param>
		public static StreamFileInfo ReadStreamFileInfo(this MemoryReader Reader, StreamDirectoryInfo Directory)
		{
			ReadOnlyUtf8String Name = Reader.ReadString();
			long Length = Reader.ReadInt64();
			FileContentId ContentId = Reader.ReadFileContentId();
			ReadOnlyUtf8String DepotFileAndRevision = Reader.ReadString();
			return new StreamFileInfo(Name, Length, ContentId, Directory, DepotFileAndRevision);
		}

		/// <summary>
		/// Save the file info to disk
		/// </summary>
		/// <param name="Writer">Writer to output to</param>
		/// <param name="FileInfo">The file info to serialize</param>
		public static void WriteStreamFileInfo(this MemoryWriter Writer, StreamFileInfo FileInfo)
		{
			Writer.WriteString(FileInfo.Name);
			Writer.WriteInt64(FileInfo.Length);
			Writer.WriteFileContentId(FileInfo.ContentId);
			Writer.WriteString(FileInfo.DepotFileAndRevision);
		}

		/// <summary>
		/// Gets the serialized length of this file info
		/// </summary>
		public static int GetSerializedSize(this StreamFileInfo FileInfo)
		{
			return FileInfo.Name.GetSerializedSize() + sizeof(long) + FileInfo.ContentId.GetSerializedSize() + FileInfo.DepotFileAndRevision.GetSerializedSize();
		}
	}
}
