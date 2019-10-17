// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace BuildAgent.Workspace.Common
{
	/// <summary>
	/// Stores information about where a particular FileContentId has been staged, or its location in the cache
	/// </summary>
	[DebuggerDisplay("{ContentId}")]
	class TrackedFileInfo : IBinarySerializable
	{
		public readonly DirectoryReference CacheDir;
		public readonly FileContentId ContentId;
		public readonly ulong CacheId;
		public readonly long Length;
		public readonly long LastModifiedTicks;
		public readonly bool bReadOnly;
		public readonly uint SequenceNumber;

		public TrackedFileInfo(DirectoryReference CacheDir, FileContentId ContentId, ulong CacheId, long Length, long LastModifiedTicks, bool bReadOnly, uint SequenceNumber)
		{
			this.CacheDir = CacheDir;
			this.ContentId = ContentId;
			this.CacheId = CacheId;
			this.Length = Length;
			this.LastModifiedTicks = LastModifiedTicks;
			this.bReadOnly = bReadOnly;
			this.SequenceNumber = SequenceNumber;
		}

		public TrackedFileInfo(DirectoryReference CacheDir, BinaryReader Reader)
		{
			this.CacheDir = CacheDir;
			this.ContentId = Reader.ReadObject<FileContentId>();
			this.CacheId = Reader.ReadUInt64();
			this.Length = Reader.ReadInt64();
			this.LastModifiedTicks = Reader.ReadInt64();
			this.bReadOnly = Reader.ReadBoolean();
			this.SequenceNumber = Reader.ReadUInt32();
		}

		public void Write(BinaryWriter Writer)
		{
			Writer.Write(ContentId);
			Writer.Write(CacheId);
			Writer.Write(Length);
			Writer.Write(LastModifiedTicks);
			Writer.Write(bReadOnly);
			Writer.Write(SequenceNumber);
		}

		public bool CheckIntegrity()
		{
			FileReference Location = GetLocation();

			FileInfo Info = new FileInfo(Location.FullName);
			if(!Info.Exists)
			{
				Log.TraceWarning("warning: {0} was missing from cache.", Location);
				return false;
			}
			if(Info.Length != Length)
			{
				Log.TraceWarning("warning: {0} was {1:n} bytes; expected {2:n} bytes", Location, Info.Length, Length);
				return false;
			}
			if(Info.LastWriteTimeUtc.Ticks != LastModifiedTicks)
			{
				Log.TraceWarning("warning: {0} was last modified at {1}; expected {2}", Location, Info.LastWriteTimeUtc, new DateTime(LastModifiedTicks, DateTimeKind.Utc));
				return false;
			}
			if(Info.Attributes.HasFlag(FileAttributes.ReadOnly) != bReadOnly)
			{
				Log.TraceWarning("warning: {0} readonly flag is {1}; expected {2}", Info.Attributes.HasFlag(FileAttributes.ReadOnly), bReadOnly);
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
}
