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
	/// Methods for CbObjectAttachmentWrapper
	/// </summary>
	public partial class CbObjectAttachmentWrapper
	{
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Hash"></param>
		public CbObjectAttachmentWrapper(IoHash Hash)
		{
			this.Hash = ByteString.CopyFrom(Hash.Memory.ToArray());
		}

		/// <summary>
		/// Convert from an IoHashWrapper to an IoHash
		/// </summary>
		/// <param name="Hash"></param>
		public static implicit operator CbObjectAttachment(CbObjectAttachmentWrapper Attachment)
		{
			return new CbObjectAttachment(new IoHash(Attachment.Hash.ToByteArray()));
		}

		/// <summary>
		/// Convert from an IoHash to an IoHashWrapper
		/// </summary>
		/// <param name="Hash"></param>
		public static implicit operator CbObjectAttachmentWrapper(CbObjectAttachment Attachment)
		{
			return new CbObjectAttachmentWrapper(Attachment.Hash);
		}
	}
}
