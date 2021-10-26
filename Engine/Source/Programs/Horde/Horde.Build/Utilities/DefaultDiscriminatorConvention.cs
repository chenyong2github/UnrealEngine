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
	/// Implements a discriminator convention, which defaults to a specific concrete type if no discriminator is specified.
	/// 
	/// This class should also be registered as the discriminator for the concrete type as well as the interface, because the default behavior
	/// of the serializer is to recognize that the concrete type is discriminated and fall back to trying to read the discriminator element,
	/// failing, and falling back to serializing it as the interface recursively until we get a stack overflow.
	/// 
	/// Registering this as the discriminator for the concrete type prevents this, because we can discriminate the concrete type correctly.
	/// </summary>
	class DefaultDiscriminatorConvention : IDiscriminatorConvention
	{
		/// <summary>
		/// The normal discriminator to use
		/// </summary>
		static IDiscriminatorConvention Inner { get; } = StandardDiscriminatorConvention.Hierarchical;

		/// <summary>
		/// The nominal type
		/// </summary>
		Type BaseType;

		/// <summary>
		/// Default type to use if the inner discriminator returns the nominal type
		/// </summary>
		Type DefaultType;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="BaseType">The base class</param>
		/// <param name="DefaultType">Default type to use if the inner discriminator returns an interface</param>
		public DefaultDiscriminatorConvention(Type BaseType, Type DefaultType)
		{
			this.BaseType = BaseType;
			this.DefaultType = DefaultType;
		}

		/// <inheritdoc/>
		public string ElementName => Inner.ElementName;

		/// <inheritdoc/>
		public Type GetActualType(IBsonReader BsonReader, Type NominalType)
		{
			Type ActualType = Inner.GetActualType(BsonReader, NominalType);
			if (ActualType == BaseType)
			{
				ActualType = DefaultType;
			}
			return ActualType;
		}

		/// <inheritdoc/>
		public BsonValue GetDiscriminator(Type NominalType, Type ActualType)
		{
			return Inner.GetDiscriminator(NominalType, ActualType);
		}
	}

	/// <summary>
	/// Generic version of <see cref="DefaultDiscriminatorConvention"/>
	/// </summary>
	class DefaultDiscriminatorConvention<TBase, TDerived> : DefaultDiscriminatorConvention
	{
		/// <summary>
		/// Constructor
		/// </summary>
		public DefaultDiscriminatorConvention()
			: base(typeof(TBase), typeof(TDerived))
		{
		}
	}
}
