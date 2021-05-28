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
	/// Metadata for a Perforce file
	/// </summary>
	[DebuggerDisplay("{Name}")]
	class StreamFileInfo
	{
		/// <summary>
		/// Name of this file
		/// </summary>
		public ReadOnlyUtf8String Name { get; }

		/// <summary>
		/// Length of the file, as reported by the server (actual size on disk may be different due to workspace options).
		/// </summary>
		public long Length { get; }

		/// <summary>
		/// Unique identifier for the file content
		/// </summary>
		public FileContentId ContentId { get; }

		/// <summary>
		/// Depot path for this file
		/// </summary>
		public ReadOnlyUtf8String DepotFileAndRevision { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public StreamFileInfo(ReadOnlyUtf8String Name, long Length, FileContentId ContentId, ReadOnlyUtf8String DepotPathAndRevision)
		{
			this.Name = Name;
			this.Length = Length;
			this.ContentId = ContentId;
			this.DepotFileAndRevision = DepotPathAndRevision;
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
		public static StreamFileInfo ReadStreamFileInfo(this MemoryReader Reader)
		{
			ReadOnlyUtf8String Name = Reader.ReadString();
			long Length = Reader.ReadInt64();
			FileContentId ContentId = Reader.ReadFileContentId();
			ReadOnlyUtf8String DepotPathAndRevision = Reader.ReadString();
			return new StreamFileInfo(Name, Length, ContentId, DepotPathAndRevision);
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
