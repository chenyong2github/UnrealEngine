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
		public Digest<Md5> Digest
		{
			get;
		}

		public ReadOnlyUtf8String Type
		{
			get;
		}

		public FileContentId(Digest<Md5> Digest, ReadOnlyUtf8String Type)
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
			Digest<Md5> Digest = Reader.ReadDigest<Md5>();
			ReadOnlyUtf8String Type = Reader.ReadString();
			return new FileContentId(Digest, Type);
		}

		public static void WriteFileContentId(this MemoryWriter Writer, FileContentId FileContentId)
		{
			Writer.WriteDigest<Md5>(FileContentId.Digest);
			Writer.WriteString(FileContentId.Type);
		}

		public static int GetSerializedSize(this FileContentId FileContentId)
		{
			return Digest<Md5>.Length + FileContentId.Type.GetSerializedSize();
		}
	}
}
