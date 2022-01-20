// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Text;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Identifier for a storage bucket
	/// </summary>
	public struct BucketId : IEquatable<BucketId>
	{
		/// <summary>
		/// The text representing this id
		/// </summary>
		readonly StringId Inner;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Input">Unique id for the string</param>
		public BucketId(string Input)
		{
			Inner = new StringId(Input);
		}

		/// <inheritdoc/>
		public override bool Equals(object? Obj) => Obj is BucketId Id && Inner.Equals(Id.Inner);

		/// <inheritdoc/>
		public override int GetHashCode() => Inner.GetHashCode();

		/// <inheritdoc/>
		public bool Equals(BucketId Other) => Inner.Equals(Other.Inner);

		/// <inheritdoc/>
		public override string ToString() => Inner.ToString();

		/// <inheritdoc cref="StringId.op_Equality"/>
		public static bool operator ==(BucketId Left, BucketId Right) => Left.Inner == Right.Inner;

		/// <inheritdoc cref="StringId.op_Inequality"/>
		public static bool operator !=(BucketId Left, BucketId Right) => Left.Inner != Right.Inner;
	}
}
