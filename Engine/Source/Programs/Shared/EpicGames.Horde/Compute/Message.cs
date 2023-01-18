// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Serialization;
using System;

#pragma warning disable CS1591
#pragma warning disable CA1819

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Terminate the current connection
	/// </summary>
	[Message("close"), CbObject]
	public class CloseMessage : MessageBase
	{
	}

	/// <summary>
	/// XOR a block of data with a value
	/// </summary>
	[Message("xor-req")]
	public class XorRequestMessage : MessageBase
	{
		[CbField]
		public byte Value { get; set; }

		[CbField]
		public byte[] Payload { get; set; } = Array.Empty<byte>();
	}

	/// <summary>
	/// Response from an XOR message
	/// </summary>
	[Message("xor-rsp")]
	public class XorResponseMessage : MessageBase
	{
		[CbField]
		public byte[] Payload { get; set; } = Array.Empty<byte>();
	}
}
