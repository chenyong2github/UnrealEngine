// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;
using System.Runtime.InteropServices;

namespace SolidworksDatasmith
{
	[ComVisible(false)]
	public class SwEventArgs : EventArgs
	{
		private class ItemBase
		{
			public string Name;
		}

		private class Item<T> : ItemBase
		{
			private T _data;

			public T Data { get { return _data; } }

			public Item(string id, T value)
			{
				Name = id;
				_data = value;
			}
		}

		private List<ItemBase> _items = new List<ItemBase>();

		public SwEventArgs()
		{
		}

		public SwEventArgs AddParameter<T>(string id, T value)
		{
			_items.Add(new Item<T>(id, value));
			return this;
		}

		public T GetParameter<T>(string name)
		{
			ItemBase item = null;
			try
			{
				item = _items.Find(x => (name == x.Name));
			}
			catch
			{
			}
			if (item != null)
			{
				var titem = item as Item<T>;
				if (titem != null)
					return titem.Data;
			}
			throw new System.ArgumentException("Parameter not found", "SwEventArgs::GetParameter " + typeof(T).Name + " " + name);
		}
	}

	[ComVisible(false)]
	public class swEvent<T> where T : SwEventArgs
	{
		public event EventHandler<T> Source = delegate { };

		public void Fire(object sender, T arg = default(T))
		{
			EventHandler<T> handler = Source;
			handler?.Invoke(sender, arg);
		}

		public void Fire(T arg = default(T))
		{
			EventHandler<T> handler = Source;
			handler?.Invoke(this, arg);
		}

		public void Register(EventHandler<T> handler)
		{
			Source += handler;
		}

		public void Unregister(EventHandler<T> handler)
		{
			Source -= handler;
		}

		public static swEvent<T> operator +(swEvent<T> e, EventHandler<T> handler)
		{
			e.Register(handler);
			return e;
		}
	}

	[ComVisible(false)]
	public class SwNotifications
	{
		public swEvent<SwEventArgs> ComponentAddItemEvent = new swEvent<SwEventArgs>();
		public swEvent<SwEventArgs> ComponentRemovedEvent = new swEvent<SwEventArgs>();
		public swEvent<SwEventArgs> ComponentRenamedEvent = new swEvent<SwEventArgs>();

		public swEvent<SwEventArgs> PartAddItemEvent = new swEvent<SwEventArgs>();
	}
}
