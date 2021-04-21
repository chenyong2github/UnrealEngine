import { IPayload, IPayloads, IPreset, IPanel, IView, ICustomStackWidget, ICustomStackTabs, PropertyValue, IAsset, AssetAction, WidgetTypes, PropertyType } from '../../Client/src/shared';
import _ from 'lodash';
import WebSocket from 'ws';
import { Notify, Program } from './';
import request from 'superagent';
import crypto from 'crypto';


namespace UnrealApi {
  export enum PresetEvent {
    FieldsRenamed     = 'PresetFieldsRenamed',
    FieldsChanged     = 'PresetFieldsChanged',
    FieldsAdded       = 'PresetFieldsAdded',
    FieldsRemoved     = 'PresetFieldsRemoved',
    MetadataModified  = 'PresetMetadataModified',
    ActorModified     = 'PresetActorModified',
  }

  export type Presets = { 
    Presets: Partial<IPreset>[];
  };

  export type Preset = {
    Preset: IPreset;
  };

  export type View = {
    Value: string;
  };

  export type Request = {
    RequestId: number;
    URL: string;
    Verb: 'GET' | 'PUT' | 'POST' | 'DELETE';
  };

  export type Response = {
    RequestId: number;
    ResponseCode: number;
    ResponseBody: any;
  };

  export type BatchResponses = {
    Responses: Response[];
  };

  export type GetPropertyValue = {
    PropertyValue: PropertyValue;
  };

  export type PropertyValueSet = {
    ObjectPath: string;
    PropertyValue: PropertyValue;
  };

  export type PropertyValues = {
    PropertyValues: PropertyValueSet[];
  };

  export type Assets = {
    Assets: IAsset[];
  };
}

export namespace UnrealEngine {
  let connection: WebSocket;
  let quitTimout: NodeJS.Timeout;

  let pendings: { [id: string]: (reply: any) => void } = {};
  let httpRequest: number = 1;
  let wsRequest: number = 1;

  let presets: IPreset[] = [];
  let payloads: IPayloads = {};
  let views: { [preset: string]: IView } = {};

  export async function initialize() {
    connect();
    setInterval(() => pullPresets(), 1000);
  }

  export function isConnected(): boolean {
    return (connection?.readyState === WebSocket.OPEN);
  }

  function connect() {
    if (connection?.readyState === WebSocket.OPEN || connection?.readyState === WebSocket.CONNECTING)
      return;

    const address = `ws://localhost:${Program.ueWebSocketPort}`;

    connection = new WebSocket(address);
    connection
      .on('open', onConnected)
      .on('message', onMessage)
      .on('error', onError)
      .on('close', onClose);
  }

  function onConnected() {
    if (connection.readyState !== WebSocket.OPEN)
      return;

    if (quitTimout) {
      clearTimeout(quitTimout);
      quitTimout = undefined;
    }
    
    console.log('Connected to UE Remote WebSocket');
    Notify.emit('connected', true);
    refresh();
  }

  async function refresh() {
    try {
      await pullPresets();
      await pullValues();
    } catch (error) {
    }
  }

  function onMessage(data: WebSocket.Data) {
    const json = data.toString();

    try {
      const message = JSON.parse(json);
      if (message.RequestId) {
        const promise = pendings[message.RequestId];
        if (!promise)
          return;

        delete pendings[message.RequestId];
        promise?.(message.ResponseBody);
        return;
      }

      switch (message.Type) {
        case UnrealApi.PresetEvent.FieldsChanged: {
          const preset = _.find(presets, p => p.Name === message.PresetName);
          if (!preset)
            break;

          for (const field of message.ChangedFields) {
            setPayloadValueInternal(payloads, [message.PresetName, field.PropertyLabel], field.PropertyValue);
            Notify.emitValueChange(message.PresetName, field.PropertyLabel, field.PropertyValue);
          }
          break;
        }

        case UnrealApi.PresetEvent.FieldsAdded:
        case UnrealApi.PresetEvent.FieldsRemoved:
          refresh();
          break;

        case UnrealApi.PresetEvent.MetadataModified:
          if (!message.Metadata.view)
            break;

          _.remove(presets, p => p.Name == message.PresetName);
          refresh()
            .then(() => refreshView(message.PresetName, message.Metadata.view));
          break;
      }

    } catch (error) {
      console.log('Failed to parse answer', error?.message, json);
    }
  }

  function onError(error: Error) {
    console.log('UE Remote WebSocket:', error.message);
  }

  function onClose() {
    presets = [];
    payloads = {};
    views = {};

    Notify.emit('connected', false);
    setTimeout(connect, 1000);

    if (Program.monitor && !quitTimout)
      quitTimout = setTimeout(quit, 15 * 1000);
  }

  function quit() {
    process.exit(1);
  }

  function verifyConnection() {
    if (connection?.readyState !== WebSocket.OPEN)
      throw new Error('Websocket is not connected');
  }

  function send(message: string, parameters: any) {
    verifyConnection();
    const Id = wsRequest++;
    connection.send(JSON.stringify({ MessageName: message, Id, Parameters: parameters }));
  }

  function http<T>(Verb: string, URL: string, Body?: object, wantAnswer?: boolean): Promise<T> {
    const RequestId = httpRequest++;
    send('http', { RequestId, Verb, URL, Body });
    if (!wantAnswer)
      return;

    return new Promise(resolve => {
      pendings[RequestId] = resolve;
    });
  }

  function get<T>(url: string): Promise<T> {
    return http<T>('GET', url, undefined, true);
  }

  function put<T>(url: string, body: object): Promise<T> {
    return http<T>('PUT', url, body, true);
  }

  function registerToPreset(PresetName: string): void {
    send('preset.register', { PresetName, IgnoreRemoteChanges: true });
  }

  function unregisterPreset(PresetName: string) {
    send('preset.unregister', { PresetName });
  }    

  export async function getPresets(): Promise<IPreset[]> {
    return presets;
  }

  async function pullPresets(): Promise<void> {
    try {
      const all = [];
      const { Presets } = await get<UnrealApi.Presets>('/remote/presets');
      
      for (const p of Presets ?? []) {
        if (!_.find(presets, preset => preset.Name === p.Name)) {
          registerToPreset(p.Name);

          const res = await get<UnrealApi.View>(`/remote/preset/${p.Name}/metadata/view`);
          refreshView(p.Name, res?.Value);
        }

        const { Preset } = await get<UnrealApi.Preset>(`/remote/preset/${p.Name}`);
        if (!Preset)
          continue;

        Preset.ExposedProperties = [];
        Preset.ExposedFunctions = [];
        Preset.Exposed = {};

        for (const Group of Preset.Groups) {
          for (const Function of Group.ExposedFunctions)
            Preset.Exposed[Function.Id] = Function;

          for (const Property of Group.ExposedProperties) {
            Preset.Exposed[Property.Id] = Property;

            switch (Property.UnderlyingProperty.Type) {
              case PropertyType.Boolean:
              case PropertyType.Uint8:
              case PropertyType.Int8:
                Property.Widget = WidgetTypes.Toggle;
                break;

              case PropertyType.Int16:
              case PropertyType.Int32:
              case PropertyType.Int64:
              case PropertyType.Uint16:
              case PropertyType.Uint32:
              case PropertyType.Uint64:
              case PropertyType.Float:
              case PropertyType.Double:
                Property.Widget = WidgetTypes.Slider;
                break;

              case PropertyType.LinearColor:
              case PropertyType.Color:
              case PropertyType.Vector4:
                Property.Widget = WidgetTypes.ColorPicker;
                break;

              case PropertyType.Vector:
              case PropertyType.Vector2D:
              case PropertyType.Rotator:
                Property.Widget = WidgetTypes.Vector;
                break;

              case PropertyType.String:
              case PropertyType.Text:
                Property.Widget = WidgetTypes.Text;
                break;
            }
          }

          Preset.ExposedProperties.push(...Group.ExposedProperties);
          Preset.ExposedFunctions.push(...Group.ExposedFunctions);
        }
        
        all.push(Preset);
      }

      const compact =  _.compact(all);
      if (!equal(presets, compact)) {
        presets = compact;
        Notify.emit('presets', presets);
      }
    } catch (error) {
      console.log('Failed to pull presets data');
    }
  }

  async function refreshView(preset: string, viewJson: string) {
    try {
      if (!viewJson)
        return;

      const view = JSON.parse(viewJson) as IView;
      if (equal(view, views[preset]))
        return; 

      for (const tab of view.tabs) {
        if (!tab.panels)
          continue;

        for (const panel of tab.panels)
          setPanelIds(panel);
      }

      views[preset] = view;
      Notify.onViewChange(preset, view);
    } catch (error) {
      console.log('Failed to parse View of Preset', preset);
    }    
  }

  function setPanelIds(panel: IPanel) {
    if (!panel.id)
      panel.id = crypto.randomBytes(16).toString('hex');

    if (panel.widgets)
      return setWidgetsId(panel.widgets);

    if (panel.items)
      for (const item of panel.items) {
        if (!item.id)
          item.id = crypto.randomBytes(16).toString('hex');

        for (const panel of item.panels)
          setPanelIds(panel);
      }
  }

  function setWidgetsId(widgets?: ICustomStackWidget[]) {
    if (!widgets)
      return;

    for (const widget of widgets) {
      if (!widget.id)
        widget.id = crypto.randomBytes(16).toString('hex');

      if (widget.widget === 'Tabs') {
        const tabWidget = widget as ICustomStackTabs;
        for (const tab of tabWidget?.tabs ?? []) {
          if (!tab.id)
            tab.id = crypto.randomBytes(16).toString('hex');

          setWidgetsId(tab.widgets);
        }
      }
    }
  }

  function equal(a: any, b: any): boolean {
    if (a === b)
      return true;
    
    if (!!a !== !!b || typeof(a) !== typeof(b))
      return false;

    if (Array.isArray(a) && Array.isArray(b)) {
      if (a.length !== b.length)
        return false;

      for (let i = 0; i < a.length; i++) {
        if (!equal(a[i], b[i]))
          return false;
      }

      return true;
    }

    if (typeof(a) === 'object' && typeof(b) === 'object') {
      if (!equal(Object.keys(a), Object.keys(b)))
        return false;

      for (const key in a) {
        if (!equal(a[key], b[key]))
          return false;
      }

      return true;
    }

    return false;
  }

  async function pullValues(): Promise<void> {
    const updatedPayloads = {};
    for (const Preset of presets) {
      for (const group of Preset.Groups) {
        for (const property of group.ExposedProperties) {
           const value = await get<UnrealApi.PropertyValues>(`/remote/preset/${Preset.Name}/property/${property.Id}`);
           setPayloadValueInternal(updatedPayloads, [Preset.Name, property.Id], value?.PropertyValues?.[0]?.PropertyValue);
        }
      }
    }

    if (!equal(payloads, updatedPayloads)) {
      payloads = updatedPayloads;
      Notify.emit('payloads', payloads );
    }
  }

  export async function getPayload(preset: string): Promise<IPayload> {
    return payloads[preset];
  }

  export async function getPayloads(): Promise<IPayloads> {
    return { ...payloads };
  }

  export async function setPayload(preset: string, payload: IPayload): Promise<void> {
    payloads[preset] = payload;
  }

  export async function setPayloadValue(preset: string, property: string, value: PropertyValue): Promise<void> {
    try {
      const body: any = { GenerateTransaction: true };
      if (value !== null) {
        setPayloadValueInternal(payloads, [preset, property], value);
        Notify.emitValueChange(preset, property, value);
        body.PropertyValue = value;
      } else {
        body.ResetToDefault = true; 
      }

      await put(`/remote/preset/${preset}/property/${property}`, body);

      if (value === null) {
        const ret = await get<UnrealApi.PropertyValues>(`/remote/preset/${preset}/property/${property}`);
        value = ret.PropertyValues?.[0]?.PropertyValue;
        if (value !== undefined) {
          setPayloadValueInternal(payloads, [preset, property], value);
          Notify.emitValueChange(preset, property, value);
        }
      }
    } catch (err) {
      console.log('Failed to set preset data:', err.message);
    }
  }

  export async function setActorValue(preset: string, actor: string, property: string, value: PropertyValue): Promise<void> {
    const payload = payloads[preset];
    if (!payload)
      return;

    try {
      const body: any = { GenerateTransaction: true };

      if (value !== null) {
        _.set(payload[actor] as IPayload, property, value);
        Notify.emitValueChange(preset, actor, payload[actor]);

        body.PropertyValue = value;
      } else {
        body.ResetToDefault = true;
      }

      await put(`/remote/preset/${preset}/actor/${actor}/property/${property}`, body);

      if (value === null) {
        const ret = await get<UnrealApi.GetPropertyValue>(`/remote/preset/${preset}/actor/${actor}/property/${property}`);
        if (ret) {
          _.set(payload[actor] as IPayload, property, ret.PropertyValue);
          Notify.emitValueChange(preset, actor, payload[actor]);
        }
      }
    } catch (err) {
      console.log('Failed to set actor property value:', err.message);
    }
  }

  function setPayloadValueInternal(data: IPayloads, path: string[], value: PropertyValue): void {
    if (!data || !path.length)
      return;

    let element: any = data;
    for (let i = 0; i < path.length - 1; i++) {
      const property = path[i];
      if (!element[property])
        element[property] = {};
      
      element = element[property];
    }

    element[ _.last(path) ] = value;
  }

  export async function resetPayloadValue(preset: string, property: string): Promise<void> {
    try {
      await put(`/remote/preset/${preset}/property/${property}`, { ResetToDefault: true, GenerateTransaction: true });
    } catch (err) {
      console.log('Failed to reset preset property', err.message);
    }
  }

  export async function executeFunction(preset: string, actor: string, func: string): Promise<void> {
    try {
      let url = `/remote/preset/${preset}`;
      if (actor)
        url += `/actor/${actor}`;
      
      url += `/function/${func}`;

      await put(url, { Parameters: {}, GenerateTransaction: true });
    } catch (err) {
      console.log('Failed to set execute function call:', err.message);
    }
  }

  export async function executeAssetAction(asset: string, action: AssetAction, meta: any): Promise<void> {
    if (!asset)
      return;

    switch (action) {
      case AssetAction.SequencePlay:
        await playSequence(asset);
        break;
    }
  }

  async function playSequence(asset: string) {
    const sequencer = '/Script/LevelSequenceEditor.Default__LevelSequenceEditorBlueprintLibrary';
    const editor = '/Script/EditorScriptingUtilities.Default__EditorAssetLibrary';
    try {
      await put('/remote/object/call', { objectPath: editor, functionName: 'LoadAsset', parameters: { 'AssetPath': asset }  });
      await put('/remote/object/call', { objectPath: sequencer, functionName: 'OpenLevelSequence', parameters: { 'LevelSequence': asset }  });
      await put('/remote/object/call', { objectPath: sequencer, functionName: 'Pause' });
      await put('/remote/object/call', { objectPath: sequencer, functionName: 'SetCurrentTime', parameters: { NewFrame: 0 } });
      await put('/remote/object/call', { objectPath: sequencer, functionName: 'Play' });
    } catch (err) {
      console.log('Failed to play sequence');
    }
  }

  export async function getView(preset: string): Promise<IView> {
    return views[preset];
  }

  export async function setView(preset: string, view: IView): Promise<void> {
    for (const tab of view.tabs) {
      if (!tab.panels)
        continue;

      for (const panel of tab.panels)
        setPanelIds(panel);
    }

    views[preset] = view;
    const Value = JSON.stringify(view);
    await put(`/remote/preset/${preset}/metadata/view`, { Value });
  }

  export async function search(query: string, types: string[], prefix: string, count: number): Promise<IAsset[]> {
    const ret = await put<UnrealApi.Assets>('/remote/search/assets', { 
      Query: query,
      Limit: count,
      Filter: {
        ClassNames: types,
        PackagePaths: [prefix],
        RecursivePaths: true
      }
    });

    return ret.Assets;
  }

  export function proxy(method: 'GET' | 'PUT', url: string, body?: any): Promise<any> {
    if (!method || !url)
      return Promise.resolve({});

    switch (method) {
      case 'GET':
        return get(url);

      case 'PUT':
        if (body)
          return put(url, body);
    }

    return Promise.resolve({});
  }

  export function thumbnail(asset: string): Promise<any> {
    return request.put(`http://localhost:${Program.ueHttpPort}/remote/object/thumbnail`)
                  .send({ ObjectPath: asset })
                  .then(res => res.body);
  }
}