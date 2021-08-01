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
	/// Utility methods for CbObjectWrapper
	/// </summary>
	public partial class CbObjectWrapper
	{
		/// <summary>
		/// Parse the buffer as a CbObject
		/// </summary>
		public CbObject Object => new CbObject(Data.ToByteArray());

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="CbObject"></param>
		public CbObjectWrapper(CbObject CbObject)
		{
			this.Data = ByteString.CopyFrom(CbObject.GetView().ToArray());
		}

		/// <summary>
		/// Convert a wrapper to regular CbObject
		/// </summary>
		/// <param name="ObjectWrapper"></param>
		public static implicit operator CbObject(CbObjectWrapper ObjectWrapper)
		{
			return new CbObject(ObjectWrapper.Data.ToByteArray());
		}

		/// <summary>
		/// Convert a CbObject to a wrapper type
		/// </summary>
		/// <param name="Object"></param>
		public static implicit operator CbObjectWrapper(CbObject Object)
		{
			return new CbObjectWrapper(Object);
		}
	}
}
