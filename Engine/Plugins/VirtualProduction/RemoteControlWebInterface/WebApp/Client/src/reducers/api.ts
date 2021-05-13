import { Dispatch } from 'redux';
import { createAction, createReducer } from 'redux-act';
import dotProp from 'dot-prop-immutable';
import io from 'socket.io-client';
import { IAsset, IPayload, IPayloads, IPreset, IView, PropertyValue, AssetAction } from '../shared';
import _ from 'lodash';


export type ApiState = {
  presets: { [id: string]: IPreset };
  preset?: string;
  payload: IPayload;
  payloads: IPayloads;
  view: IView;
  status: {
    connected: boolean;
  },
};


let _dispatch: Dispatch;
let _getState: () => { api: ApiState };
let _socket: SocketIOClient.Socket;
const _host = (process.env.NODE_ENV === 'development' ? `http://${window.location.hostname}:7001` : '');

function _initialize(dispatch: Dispatch, getState: () => { api: ApiState }) {
  _dispatch = dispatch;
  _getState = getState;

  _socket = io(`${_host}/`, { path: '/api/io' });

  _socket
    .on('disconnect', () => dispatch(API.STATUS({ connected: false })))
    .on('presets', (presets: IPreset[]) => dispatch(API.PRESETS(presets)))
    .on('payloads', (payloads: IPayloads) => {
      dispatch(API.PAYLOADS(payloads));
      const preset = _internal.getPreset();
      if (!preset || !payloads[preset])
        return;

      dispatch(API.PAYLOAD(payloads[preset]));
    })
    .on('value', (preset: string, property: string, value: PropertyValue) => {
      dispatch(API.PAYLOADS_VALUE({ [preset]: { [property]: value }}));
      
      if (_internal.getPreset() === preset)
        dispatch(API.PAYLOAD({ [property]: value }));
    })
    .on('view', (preset: string, view: IView) => {
      if (_internal.getPreset() !== preset)
        return;

      dispatch(API.VIEW(view));
    })
    .on('connected', async (connected: boolean) => {
      dispatch(API.STATUS({ connected }));

      if (connected) {
        await Promise.all([
          _api.presets.get(),
          _api.payload.all(),
        ]);
      }
    });
}

type IRequestCallback = Function | string | undefined;

interface IRequestOptions {
  blob?: boolean;
}

async function _request(method: string, url: string, body: string | object | undefined, callback: IRequestCallback): Promise<any> {
  const request: RequestInit = { method, mode: 'cors', redirect: 'follow', headers: {} };
  if (body instanceof FormData || typeof(body) === 'string') {
    request.body = body;
  } else if (typeof(body) === 'object') {
    request.body = JSON.stringify(body);
    request.headers['Content-Type'] = 'application/json';
  }

  const res = await fetch(_host + url, request);

  let answer: any = await res.text();
  if (answer.length > 0)
    answer = JSON.parse(answer);

  if (!res.ok)
    throw answer;

  if (typeof (callback) === 'function')
    _dispatch(callback(answer));

  return answer;
}

function _get(url: string, callback?: IRequestCallback)        { return _request('GET', url, undefined, callback) }
function _put(url: string, body: any)                          { return _request('PUT', url, body, undefined) }

const API = {
  STATUS: createAction<any>('API_STATUS'),
  PRESETS: createAction<IPreset[]>('API_PRESETS'),
  PRESET_SELECT: createAction<string>('API_PRESET_SELECT'),
  VIEW: createAction<IView>('API_VIEW'),
  PAYLOAD: createAction<IPayload>('API_PAYLOAD'),
  PAYLOADS: createAction<IPayloads>('API_PAYLOADS'),
  PAYLOADS_VALUE: createAction<IPayloads>('API_PAYLOADS_VALUE'),
};

const _internal = {
  getPreset: () => {
    return _getState().api.preset;
  }
};

export const _api = {
  initialize: () => _initialize.bind(null),

  presets: {
    get: (): Promise<IPreset[]> => _get('/api/presets', API.PRESETS),
    select: (preset?: IPreset) => _dispatch(API.PRESET_SELECT(preset?.Name)),
  },
  views: {
    get: (preset: string): Promise<IView> => _get(`/api/presets/view?preset=${preset}`, API.VIEW),
    set: (view: IView) => {
      const preset = _internal.getPreset();

      _socket.emit('view', preset, view);
    },
  },
  payload: {
    get: (preset: string): Promise<IPayload> => _get(`/api/presets/payload?preset=${preset}`, API.PAYLOAD),
    all: (): Promise<IPayloads> => _get('/api/payloads', API.PAYLOADS),
    set: (property: string, value: PropertyValue) => {
      const preset = _internal.getPreset();
      _socket.emit('value', preset, property, value);
    },
    reset: (property: string) => {
      const preset = _internal.getPreset();
      _socket.emit('reset', preset, property);
    },
    execute: (func: string) => {
      const preset = _internal.getPreset();
      _socket.emit('execute', null, preset, func);
    },
    asset: (asset: string, action: AssetAction, meta?: any) => {
      _socket.emit('asset', asset, action, meta);
    },
    rename: (type: 'property' | 'function', property: string, label: string, callback: (label: string) => void) => {
      const preset = _internal.getPreset();
      _socket.emit('rename', preset, type, property, label, callback);
    },
    metadata: (property: string, meta: string, value: string, callback: () => void) => {
      const preset = _internal.getPreset();
      _socket.emit('metadata', preset, property, meta, value, callback);
    }
  },
  actor: {
    set: (actor: string, property: string, value: PropertyValue) => {
      const preset = _internal.getPreset();
      _socket.emit('actor', preset, actor, property, value);
    },
    execute: (actor: string, func: string) => {
      const preset = _internal.getPreset();
      _socket.emit('execute', preset, actor, func);
    },
  },
  assets: {
    search: (q: string, types: string[], prefix: string, count: number = 50): Promise<IAsset[]> => {
      const args = {
        q,
        prefix,
        count,
        types: types.join(','),
      };
      
      let url = '/api/assets/search?';
      for (const arg in args)
        url += `${arg}=${encodeURIComponent(args[arg])}&`;
      
      return _get(url);
    },
    thumbnailUrl: (asset: string) => `${_host}/api/thumbnail?asset=${asset}`,
  },
  proxy: {
    get: (url: string) => _put('/api/proxy', { method: 'GET', url }),
    put: (url: string, body: any) => _put('/api/proxy', { method: 'PUT', url, body }),
    function: (objectPath: string, functionName: string, parameters: Record<string, any> = {}): Promise<any> => {
      const body = { objectPath, functionName, parameters };
      return _api.proxy.put('/remote/object/call', body);
    }
  }
};

const initialState: ApiState = {
  presets: {},
  payload: {},
  payloads: {},
  view: { tabs: null },
  status: {
    connected: false,
  },
};

const reducer = createReducer<ApiState>({}, initialState);

reducer
  .on(API.STATUS, (state, status) => dotProp.merge(state, 'status', status))
  .on(API.PRESETS, (state, presets) => {
    state = dotProp.set(state, 'presets', _.keyBy(presets, 'Name'));

    const preset = presets?.[0]?.Name;
    if (!state.preset && preset) {
      _api.views.get(preset);
      _api.payload.get(preset);
      return { ...state, preset, view: { tabs: [] }, payload: {} };
    } else if (state.preset && !presets.length) {
      return { ...state, preset: undefined, view: { tabs: null }, payload: {} };
    }

    return state;
  })
  .on(API.VIEW, (state, view) => dotProp.merge(state, 'view', view))
  .on(API.PAYLOADS, (state, payloads) => ({ ...state, payloads }))
  .on(API.PAYLOADS_VALUE, (state, payloads) => {
    for (const preset in payloads) {
      const payload = payloads[preset];
      for (const property in payload) {
        state = dotProp.set(state, ['payloads', preset, property], payload[property]);
      }
    }
    
    return state;
  })
  .on(API.PAYLOAD, (state, payload) => {
    for (const property in payload)
      state = dotProp.set(state, ['payload', property], payload[property]);
    
    return state;
  })
  .on(API.PRESET_SELECT, (state, preset) => {
    _api.views.get(preset);
    _api.payload.get(preset);
    return { ...state, preset, view: { tabs: [] }, payload: {} };
  });

export default reducer;