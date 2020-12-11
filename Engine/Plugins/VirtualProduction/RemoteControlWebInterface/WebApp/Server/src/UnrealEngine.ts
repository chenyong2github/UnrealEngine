import { IPayload, IPayloads, IPreset, IView, PropertyType, PropertyValue, IAsset, AssetAction } from '../../Client/src/shared';
import _ from 'lodash';
import WebSocket from 'ws';
import { Notify, Program } from './';


namespace UnrealApi {
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

  export type PropertyValue = {
    ObjectPath: string;
    PropertyValue: PropertyValue;
  };

  export type PropertyValues = {
    PropertyValues: PropertyValue[];
  };

  export type Assets = {
    Assets: IAsset[];
  };
}

export namespace UnrealEngine {
  let connection: WebSocket;
  let notification: WebSocket;
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

    const address = `ws://localhost:${Program.ue}`;

    connection = new WebSocket(address);
    connection
      .on('open', onConnected)
      .on('message', onMessage)
      .on('error', onError)
      .on('close', onClose);

    notification = new WebSocket(address);
    notification
      .on('open', onConnected)
      .on('message', onMessage)
      .on('error', () => {});
  }

  function onConnected() {
    if (connection.readyState !== WebSocket.OPEN || notification.readyState !== WebSocket.OPEN)
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
        case 'PresetFieldsRenamed': {
          const view = views[message.PresetName];
          if (!view)
            return;

          const panels = _.flatMap(view.tabs, tab => tab.panels);
          const allWidgets = _.flatMap(panels, panel => panel.widgets);
          for (const Rename of message.RenamedFields) {
            const widgets = _.filter(allWidgets, w => w.property === Rename.OldFieldLabel);
            for (const widget of widgets)
              widget.property = Rename.NewFieldLabel;
          }
          
          Notify.onViewChange(message.PresetName, view);
          refresh();
          break;
        }

        case 'PresetFieldsChanged': {
          const preset = _.find(presets, p => p.Name === message.PresetName);
          if (!preset)
            return;

          for (const field of message.ChangedFields) {
            const group = _.find(preset.Groups, g => !!g.ExposedProperties.find(p => p.DisplayName === field.PropertyLabel));
            if (group)
              Notify.emitValueChange(message.PresetName, group.Name, field.PropertyLabel, field.PropertyValue);
          }
          break;
        }

        case 'PresetFieldsAdded':
        case 'PresetFieldsRemoved':
          refresh();
          break;
      }

    } catch (error) {
      console.log('Failed to parse answer', error?.message, JSON.stringify(json));
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
    if (connection?.readyState !== WebSocket.OPEN || notification.readyState != WebSocket.OPEN)
      throw new Error('Websocket is not connected');
  }

  function send(message: string, parameters: any, socket?: WebSocket) {
    verifyConnection();
    const Id = wsRequest++;
    socket = socket ?? connection;
    socket.send(JSON.stringify({ MessageName: message, Id, Parameters: parameters }));
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
    send('preset.register', { PresetName }, notification);
  }

  function unregisterPreset(PresetName: string) {
    send('preset.unregister', { PresetName }, notification);
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

          try {
            const view = await get<UnrealApi.View>(`/remote/preset/${p.Name}/metadata/view`);
            const presetView = JSON.parse(view?.Value);
            views[p.Name] = presetView;
          } catch {
          }
        }

        const { Preset } = await get<UnrealApi.Preset>(`/remote/preset/${p.Name}`);
        all.push(Preset);
      }

      const compact =  _.compact(all);
      if (!equal(presets, compact)) {
        presets = compact;
        Notify.emit('presets', presets);
      }
    } catch {
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
    const promises = [], paths = [];
    for (const Preset of presets) {
      for (const group of Preset.Groups) {
        for (const property of group.ExposedProperties) {
          promises.push(get<UnrealApi.PropertyValues>(`/remote/preset/${Preset.Name}/property/${property.DisplayName}`));
          paths.push(`${Preset.Name}.${group.Name}.${property.DisplayName}`);
        }
      }
    }

    const updatedPayloads = {};

    const values = await Promise.all(promises);
    for (let i = 0; i < paths.length; i++)
      _.set(updatedPayloads, paths[i], values[i].PropertyValues?.[0]?.PropertyValue);

    if (!equal(payloads, updatedPayloads)) {
      payloads = updatedPayloads;
      Notify.emit('payloads', payloads);
    }
  }

  export async function getPayload(preset: string): Promise<IPayload> {
    return payloads[preset];
  }

  export async function setPayload(preset: string, view: IPayload): Promise<void> {
    payloads[preset] = view;
  }

  export async function setPayloadValue(preset: string, group: string, property: string, value: PropertyValue): Promise<void> {
    _.set(payloads, `${preset}.${group}.${property}`, value);

    try {
      await put(`/remote/preset/${preset}/property/${property}`, { PropertyValue: value, GenerateTransaction: true });
    } catch (err) {
      console.log('Failed to set preset data:', err.message);
    }
  }

  export async function resetPayloadValue(preset: string, property: string): Promise<void> {
    try {
      await put(`/remote/preset/${preset}/property/${property}`, { ResetToDefault: true, GenerateTransaction: true });
    } catch (err) {
      console.log('Failed to reset preset property', err.message);
    }
  }

  export async function executeFunction(preset: string, group: string, func: string): Promise<void> {
    try {
      await put(`/remote/preset/${preset}/function/${func}`, { Parameters: {}, GenerateTransaction: true });
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
    try {
      const objectPath = '/Script/LevelSequenceEditor.Default__LevelSequenceEditorBlueprintLibrary';
      await put('/remote/object/call', { objectPath, functionName: 'OpenLevelSequence', parameters: { 'LevelSequence': asset }  });
      await put('/remote/object/call', { objectPath, functionName: 'Pause' });
      await put('/remote/object/call', { objectPath, functionName: 'SetCurrentTime', parameters: { NewFrame: 0 } });
      await put('/remote/object/call', { objectPath, functionName: 'Play' });
    } catch (err) {
      console.log('Failed to play sequence');
    }
  }

  export async function getView(preset: string): Promise<IView> {
    return views[preset];
  }

  export async function setView(preset: string, view: IView): Promise<void> {
    views[preset] = view;
    const Value = JSON.stringify(view);
    await put(`/remote/preset/${preset}/metadata/view`, { Value });
  }

  export async function search(query: string): Promise<IAsset[]> {
    const ret = await put<UnrealApi.Assets>('/remote/search/assets', { 
      Query: query,
      Limit: 50,
      Filter: {
        ClassNames: ['LevelSequence'],
        PackagePaths: ['/Game'],
        RecursivePaths: true
      }
    });

    return ret.Assets;
  }
}