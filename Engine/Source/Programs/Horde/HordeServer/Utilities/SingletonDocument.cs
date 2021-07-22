// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using HordeServer.Services;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Attribute specifying the unique id for a singleton document
	/// </summary>
	[AttributeUsage(AttributeTargets.Class)]
	public class SingletonDocumentAttribute : Attribute
	{
		/// <summary>
		/// Unique id for the singleton document
		/// </summary>
		public string Id { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Id">Unique id for the singleton document</param>
		public SingletonDocumentAttribute(string Id)
		{
			this.Id = Id;
		}
	}

	/// <summary>
	/// Interface for the getting and setting the singleton
	/// </summary>
	/// <typeparam name="T">Type of document</typeparam>
	public interface ISingletonDocument<T>
	{
		/// <summary>
		/// Gets the current document
		/// </summary>
		/// <returns>The current document</returns>
		Task<T> GetAsync();

		/// <summary>
		/// Attempts to update the document
		/// </summary>
		/// <param name="Value">New state of the document</param>
		/// <returns>True if the document was updated, false otherwise</returns>
		Task<bool> TryUpdateAsync(T Value);
	}

	/// <summary>
	/// Concrete implementation of <see cref="ISingletonDocument{T}"/>
	/// </summary>
	/// <typeparam name="T">The document type</typeparam>
	public class SingletonDocument<T> : ISingletonDocument<T> where T : SingletonBase, new()
	{
		/// <summary>
		/// The database service instance
		/// </summary>
		DatabaseService DatabaseService;

		/// <summary>
		/// Unique id for the singleton document
		/// </summary>
		ObjectId ObjectId;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service instance</param>
		public SingletonDocument(DatabaseService DatabaseService)
		{
			this.DatabaseService = DatabaseService;

			SingletonDocumentAttribute? Attribute = typeof(T).GetCustomAttribute<SingletonDocumentAttribute>();
			if (Attribute == null)
			{
				throw new Exception($"Type {typeof(T).Name} is missing a {nameof(SingletonDocumentAttribute)} annotation");
			}

			ObjectId = new ObjectId(Attribute.Id);
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service instance</param>
		/// <param name="ObjectId">The singleton document object id</param>
		public SingletonDocument(DatabaseService DatabaseService, ObjectId ObjectId)
		{
			this.DatabaseService = DatabaseService;
			this.ObjectId = ObjectId;
		}

		/// <inheritdoc/>
		public async Task<T> GetAsync()
		{
			T Value = await DatabaseService.GetSingletonAsync<T>(ObjectId);
			Value.Id = ObjectId;
			return Value;
		}

		/// <inheritdoc/>
		public Task<bool> TryUpdateAsync(T Value)
		{
			return DatabaseService.TryUpdateSingletonAsync<T>(Value);
		}
	}

	/// <summary>
	/// Extension methods for singletons
	/// </summary>
	public static class SingletonDocumentExtensions
	{
		/// <summary>
		/// Update a singleton
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="Singleton"></param>
		/// <param name="UpdateAction"></param>
		/// <returns></returns>
		public static async Task<T> UpdateAsync<T>(this ISingletonDocument<T> Singleton, Action<T> UpdateAction) where T : SingletonBase, new()
		{
			for (; ; )
			{
				T Value = await Singleton.GetAsync();
				UpdateAction(Value);

				if (await Singleton.TryUpdateAsync(Value))
				{
					return Value;
				}
			}
		}
	}
}
