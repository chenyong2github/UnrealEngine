// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;

namespace EpicGames.Perforce.Managed
{
	class FileContentId
	{
		public const int DigestLength = 16;

		public ReadOnlyMemory<byte> Digest
		{
			get;
		}

		public ReadOnlyUtf8String Type
		{
			get;
		}

		public FileContentId(ReadOnlyMemory<byte> Digest, ReadOnlyUtf8String Type)
		{
			this.Digest = Digest;
			this.Type = Type;
		}

		public override bool Equals(object? Other)
		{
			FileContentId? OtherContentId = Other as FileContentId;
			if (OtherContentId == null)
			{
				return false;
			}
			else
			{
				return Digest.Span.SequenceEqual(OtherContentId.Digest.Span) && Type == OtherContentId.Type;
			}
		}

		public override int GetHashCode()
		{
			HashCode HashCode = new HashCode();
			for (int Idx = 0; Idx < Digest.Length; Idx++)
			{
				HashCode.Add(Digest.Span[Idx]);
			}
			return HashCode.ToHashCode();
		}

		public override string ToString()
		{
			return String.Format("{0} ({1})", StringUtils.FormatHexString(Digest.ToArray()), Type);
		}
	}

	static class FileContentIdExtensions
	{
		public static FileContentId ReadFileContentId(this MemoryReader Reader)
		{
			ReadOnlyMemory<byte> Digest = Reader.ReadFixedLengthBytes(FileContentId.DigestLength);
			ReadOnlyUtf8String Type = Reader.ReadString();
			return new FileContentId(Digest, Type);
		}

		public static void WriteFileContentId(this MemoryWriter Writer, FileContentId FileContentId)
		{
			Writer.WriteFixedLengthBytes(FileContentId.Digest.Span);
			Writer.WriteString(FileContentId.Type);
		}

		public static int GetSerializedSize(this FileContentId FileContentId)
		{
			return FileContentId.Digest.Length + FileContentId.Type.GetSerializedSize();
		}
	}
}
