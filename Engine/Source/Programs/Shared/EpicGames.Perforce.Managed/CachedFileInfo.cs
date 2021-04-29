// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;

namespace EpicGames.Perforce.Managed
{
	/// <summary>
	/// Stores information about where a particular FileContentId has been staged, or its location in the cache
	/// </summary>
	[DebuggerDisplay("{ContentId}")]
	class CachedFileInfo
	{
		public readonly DirectoryReference CacheDir;
		public readonly FileContentId ContentId;
		public readonly ulong CacheId;
		public readonly long Length;
		public readonly long LastModifiedTicks;
		public readonly bool bReadOnly;
		public readonly uint SequenceNumber;

		public CachedFileInfo(DirectoryReference CacheDir, FileContentId ContentId, ulong CacheId, long Length, long LastModifiedTicks, bool bReadOnly, uint SequenceNumber)
		{
			this.CacheDir = CacheDir;
			this.ContentId = ContentId;
			this.CacheId = CacheId;
			this.Length = Length;
			this.LastModifiedTicks = LastModifiedTicks;
			this.bReadOnly = bReadOnly;
			this.SequenceNumber = SequenceNumber;
		}

		public bool CheckIntegrity(ILogger Logger)
		{
			FileReference Location = GetLocation();

			FileInfo Info = new FileInfo(Location.FullName);
			if (!Info.Exists)
			{
				Logger.LogWarning("warning: {0} was missing from cache.", Location);
				return false;
			}
			if (Info.Length != Length)
			{
				Logger.LogWarning("warning: {0} was {1:n} bytes; expected {2:n} bytes", Location, Info.Length, Length);
				return false;
			}
			if (Info.LastWriteTimeUtc.Ticks != LastModifiedTicks)
			{
				Logger.LogWarning("warning: {0} was last modified at {1}; expected {2}", Location, Info.LastWriteTimeUtc, new DateTime(LastModifiedTicks, DateTimeKind.Utc));
				return false;
			}
			if (Info.Attributes.HasFlag(FileAttributes.ReadOnly) != bReadOnly)
			{
				Logger.LogWarning("warning: {0} readonly flag is {1}; expected {2}", Info.Attributes.HasFlag(FileAttributes.ReadOnly), bReadOnly);
				return false;
			}

			return true;
		}

		public FileReference GetLocation()
		{
			StringBuilder FullName = new StringBuilder(CacheDir.FullName);
			FullName.Append(Path.DirectorySeparatorChar);
			FullName.AppendFormat("{0:X}", (CacheId >> 60) & 0xf);
			FullName.Append(Path.DirectorySeparatorChar);
			FullName.AppendFormat("{0:X}", (CacheId >> 56) & 0xf);
			FullName.Append(Path.DirectorySeparatorChar);
			FullName.AppendFormat("{0:X}", (CacheId >> 62) & 0xf);
			FullName.Append(Path.DirectorySeparatorChar);
			FullName.AppendFormat("{0:X}", CacheId);
			return new FileReference(FullName.ToString());
		}
	}

	static class CachedFileInfoExtensions
	{
		public static CachedFileInfo ReadCachedFileInfo(this MemoryReader Reader, DirectoryReference CacheDir)
		{
			FileContentId ContentId = Reader.ReadFileContentId();
			ulong CacheId = Reader.ReadUInt64();
			long Length = Reader.ReadInt64();
			long LastModifiedTicks = Reader.ReadInt64();
			bool bReadOnly = Reader.ReadBoolean();
			uint SequenceNumber = Reader.ReadUInt32();
			return new CachedFileInfo(CacheDir, ContentId, CacheId, Length, LastModifiedTicks, bReadOnly, SequenceNumber);
		}

		public static void WriteCachedFileInfo(this MemoryWriter Writer, CachedFileInfo FileInfo)
		{
			Writer.WriteFileContentId(FileInfo.ContentId);
			Writer.WriteUInt64(FileInfo.CacheId);
			Writer.WriteInt64(FileInfo.Length);
			Writer.WriteInt64(FileInfo.LastModifiedTicks);
			Writer.WriteBoolean(FileInfo.bReadOnly);
			Writer.WriteUInt32(FileInfo.SequenceNumber);
		}

		public static int GetSerializedSize(this CachedFileInfo FileInfo)
		{
			return FileInfo.ContentId.GetSerializedSize() + sizeof(ulong) + sizeof(long) + sizeof(long) + sizeof(byte) + sizeof(uint);
		}
	}
}

