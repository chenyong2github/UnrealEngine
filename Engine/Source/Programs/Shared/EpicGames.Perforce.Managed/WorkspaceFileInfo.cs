// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
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
	/// Stores information about a file that has been staged into a workspace
	/// </summary>
	class WorkspaceFileInfo
	{
		public readonly WorkspaceDirectoryInfo Directory;
		public readonly Utf8String Name;
		public long Length;
		public long LastModifiedTicks;
		public bool bReadOnly;
		public readonly FileContentId ContentId;

		public WorkspaceFileInfo(WorkspaceDirectoryInfo Directory, Utf8String Name, FileContentId ContentId)
		{
			this.Directory = Directory;
			this.Name = Name;
			this.ContentId = ContentId;
		}

		public WorkspaceFileInfo(WorkspaceDirectoryInfo Directory, Utf8String Name, FileInfo Info, FileContentId ContentId)
			: this(Directory, Name, Info.Length, Info.LastWriteTimeUtc.Ticks, Info.Attributes.HasFlag(FileAttributes.ReadOnly), ContentId)
		{
		}

		public WorkspaceFileInfo(WorkspaceDirectoryInfo Directory, Utf8String Name, long Length, long LastModifiedTicks, bool bReadOnly, FileContentId ContentId)
		{
			this.Directory = Directory;
			this.Name = Name;
			this.Length = Length;
			this.LastModifiedTicks = LastModifiedTicks;
			this.bReadOnly = bReadOnly;
			this.ContentId = ContentId;
		}

		public void SetMetadata(long Length, long LastModifiedTicks, bool bReadOnly)
		{
			this.Length = Length;
			this.LastModifiedTicks = LastModifiedTicks;
			this.bReadOnly = bReadOnly;
		}

		public void UpdateMetadata()
		{
			FileInfo Info = new FileInfo(GetFullName());
			if(Info.Exists)
			{
				Length = Info.Length;
				LastModifiedTicks = Info.LastWriteTimeUtc.Ticks;
				bReadOnly = Info.Attributes.HasFlag(FileAttributes.ReadOnly);
			}
		}

		public bool MatchesAttributes(FileInfo Info)
		{
			return Length == Info.Length && LastModifiedTicks == Info.LastWriteTimeUtc.Ticks && (Info.Attributes.HasFlag(FileAttributes.ReadOnly) == bReadOnly);
		}

		public string GetClientPath()
		{
			StringBuilder Builder = new StringBuilder();
			Directory.AppendClientPath(Builder);
			Builder.Append(Name);
			return Builder.ToString();
		}

		public string GetFullName()
		{
			StringBuilder Builder = new StringBuilder();
			Directory.AppendFullPath(Builder);
			Builder.Append(Path.DirectorySeparatorChar);
			Builder.Append(Name);
			return Builder.ToString();
		}

		public FileReference GetLocation()
		{
			return new FileReference(GetFullName());
		}

		public override string ToString()
		{
			return GetFullName();
		}
	}

	static class WorkspaceFileInfoExtensions
	{
		public static WorkspaceFileInfo ReadWorkspaceFileInfo(this MemoryReader Reader, WorkspaceDirectoryInfo Directory)
		{
			Utf8String Name = Reader.ReadString();
			long Length = Reader.ReadInt64();
			long LastModifiedTicks = Reader.ReadInt64();
			bool bReadOnly = Reader.ReadBoolean();
			FileContentId ContentId = Reader.ReadFileContentId();
			return new WorkspaceFileInfo(Directory, Name, Length, LastModifiedTicks, bReadOnly, ContentId);
		}

		public static void WriteWorkspaceFileInfo(this MemoryWriter Writer, WorkspaceFileInfo FileInfo)
		{
			Writer.WriteString(FileInfo.Name);
			Writer.WriteInt64(FileInfo.Length);
			Writer.WriteInt64(FileInfo.LastModifiedTicks);
			Writer.WriteBoolean(FileInfo.bReadOnly);
			Writer.WriteFileContentId(FileInfo.ContentId);
		}

		public static int GetSerializedSize(this WorkspaceFileInfo FileInfo)
		{
			return FileInfo.Name.GetSerializedSize() + sizeof(long) + sizeof(long) + sizeof(byte) + FileInfo.ContentId.GetSerializedSize();
		}
	}
}
