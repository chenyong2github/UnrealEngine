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
	class FileContentId
	{
		public static int SerializedSize => Digest<Md5>.Length + FileType.SerializedSize;

		public Digest<Md5> Digest
		{
			get;
		}

		public FileType Type
		{
			get;
		}

		public FileContentId(Digest<Md5> Digest, FileType Type)
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
			FileType Type = Reader.ReadFileType();
			return new FileContentId(Digest, Type);
		}

		public static void WriteFileContentId(this MemoryWriter Writer, FileContentId FileContentId)
		{
			Writer.WriteDigest<Md5>(FileContentId.Digest);
			Writer.WriteFileType(FileContentId.Type);
		}
	}
}
