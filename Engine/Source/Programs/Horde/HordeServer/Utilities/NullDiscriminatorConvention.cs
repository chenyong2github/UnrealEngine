// Copyright Epic Games, Inc. All Rights Reserved.

using MongoDB.Bson;
using MongoDB.Bson.IO;
using MongoDB.Bson.Serialization.Conventions;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Prevent a discriminator being serialized for a type
	/// </summary>
	public class NullDiscriminatorConvention : IDiscriminatorConvention
	{
		/// <summary>
		/// Instance of the convention
		/// </summary>
		public static NullDiscriminatorConvention Instance { get; } = new NullDiscriminatorConvention();

		/// <inheritdoc/>
		public Type GetActualType(IBsonReader BsonReader, Type NominalType)
		{
			return NominalType;
		}

		/// <inheritdoc/>
		public BsonValue? GetDiscriminator(Type nominalType, Type actualType)
		{
			return null;
		}

		/// <inheritdoc/>
		public string? ElementName => null;
	}
}
