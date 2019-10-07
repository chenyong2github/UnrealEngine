// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace BuildAgent.Workspace.Common
{
	class FileContentId : IBinarySerializable
	{
		const int DigestLength = 16;

		public byte[] Digest;
		public string Type;

		public FileContentId(byte[] Digest, string Type)
		{
			this.Digest = Digest;
			this.Type = Type;
		}

		public FileContentId(BinaryReader Reader)
		{
			this.Digest = Reader.ReadBytes(DigestLength);
			this.Type = Reader.ReadString();
		}

		public void Write(BinaryWriter Writer)
		{
			Writer.Write(Digest);
			Writer.Write(Type);
		}

		public override bool Equals(object Other)
		{
			FileContentId OtherContentId = Other as FileContentId;
			if(OtherContentId == null)
			{
				return false;
			}

			for(int Idx = 0; Idx < DigestLength; Idx++)
			{
				if(OtherContentId.Digest[Idx] != Digest[Idx])
				{
					return false;
				}
			}

			if(OtherContentId.Type != Type)
			{
				return false;
			}

			return true;
		}

		public override int GetHashCode()
		{
			int HashCode = Type.GetHashCode();
			for(int Idx = 0; Idx < DigestLength; Idx++)
			{
				HashCode = (HashCode * 31) + Digest[Idx];
			}
			return HashCode;
		}

		public override string ToString()
		{
			return String.Format("{0} ({1})", StringUtils.FormatHexString(Digest), Type);
		}
	}
}
