// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;

namespace EpicGames.Perforce.Managed
{
	[DebuggerDisplay("{Digest} ({Type})")]
	public class FileContentId
	{
		public Md5Hash Digest
		{
			get;
		}

		public Utf8String Type
		{
			get;
		}

		public FileContentId(Md5Hash Digest, Utf8String Type)
		{
			this.Digest = Digest;
			this.Type = Type;
		}

		public override bool Equals(object? Other)
		{
			return (Other is FileContentId OtherFile) && Digest == OtherFile.Digest && Type == OtherFile.Type;
		}

		public override int GetHashCode()
		{
			return HashCode.Combine(Digest.GetHashCode(), Type.GetHashCode());
		}
	}

	static class FileContentIdExtensions
	{
		public static FileContentId ReadFileContentId(this MemoryReader Reader)
		{
			Md5Hash Digest = Reader.ReadMd5Hash();
			Utf8String Type = Reader.ReadString();
			return new FileContentId(Digest, Type);
		}

		public static void WriteFileContentId(this MemoryWriter Writer, FileContentId FileContentId)
		{
			Writer.WriteMd5Hash(FileContentId.Digest);
			Writer.WriteString(FileContentId.Type);
		}

		public static int GetSerializedSize(this FileContentId FileContentId)
		{
			return Digest<Md5>.Length + FileContentId.Type.GetSerializedSize();
		}
	}
}
