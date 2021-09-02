// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using Google.Protobuf;
using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Horde.Common
{
	/// <summary>
	/// Methods for CbBinaryAttachmentWrapper
	/// </summary>
	public partial class CbBinaryAttachmentWrapper
	{
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Hash"></param>
		public CbBinaryAttachmentWrapper(IoHash Hash)
		{
			this.Hash = ByteString.CopyFrom(Hash.Memory.ToArray());
		}

		/// <summary>
		/// Convert from an IoHashWrapper to an IoHash
		/// </summary>
		/// <param name="Hash"></param>
		public static implicit operator CbBinaryAttachment(CbBinaryAttachmentWrapper Attachment)
		{
			return new CbBinaryAttachment(new IoHash(Attachment.Hash.ToByteArray()));
		}

		/// <summary>
		/// Convert from an IoHash to an IoHashWrapper
		/// </summary>
		/// <param name="Hash"></param>
		public static implicit operator CbBinaryAttachmentWrapper(CbBinaryAttachment Attachment)
		{
			return new CbBinaryAttachmentWrapper(Attachment.Hash);
		}
	}
}
