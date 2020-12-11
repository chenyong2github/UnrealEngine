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
      const preset = _internal.getPreset();
      if (!preset || !payloads[preset])
        return;

      dispatch(API.PAYLOAD(payloads[preset]));
    })
    .on('value', (preset: string, object: string, property: string, value: PropertyValue) => {
      if (_internal.getPreset() !== preset)
        return;

      dispatch(API.PAYLOAD({ [object]: { [property]: value } }));
    })
    .on('view', (preset: string, view: IView) => {
      if (_internal.getPreset() !== preset)
        return;

      dispatch(API.VIEW(view));
    })
    .on('connected', async (connected: boolean) => {
      dispatch(API.STATUS({ connected }));

      if (connected)
        await _api.presets.get();
    });
}

type IRequestCallback = Function | string | undefined;

interface IRequestOptions {
  blob?: boolean;
}

async function _request(method: string, url: string, body: string | object | undefined, callback: IRequestCallback, options?: IRequestOptions): Promise<any> {
  const request: RequestInit = { method, mode: 'cors', redirect: 'follow', headers: {} };
  if (body instanceof FormData || typeof(body) === 'string') {
    request.body = body;
  } else if (typeof(body) === 'object') {
    request.body = JSON.stringify(body);
    request.headers['Content-Type'] = 'application/json';
  }

  const res = await fetch(_host + url, request);
  if (res.ok && options?.blob)
    return await res.blob();

  let answer: any = await res.text();
  if (answer.length > 0)
    answer = JSON.parse(answer);

  if (!res.ok)
    throw answer;

  if (typeof (callback) === 'function')
    _dispatch(callback(answer));

  return answer;
}

function _get(url: string, callback?: IRequestCallback, options?: IRequestOptions)                { return _request('GET', url, undefined, callback, options) }

const API = {
  STATUS: createAction<any>('API_STATUS'),
  PRESETS: createAction<IPreset[]>('API_PRESETS'),
  PRESET_SELECT: createAction<string>('API_PRESET_SELECT'),
  VIEW: createAction<IView>('API_VIEW'),
  PAYLOAD: createAction<IPayload>('API_PAYLOAD'),
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
    set: (group: string, property: string, value: PropertyValue) => {
      const preset = _internal.getPreset();
      _socket.emit('value', preset, group, property, value);
    },
    reset: (property: string) => {
      const preset = _internal.getPreset();
      _socket.emit('reset', preset, property);
    },
    execute: (group: string, func: string) => {
      const preset = _internal.getPreset();
      _socket.emit('execute', preset, group, func);
    },
    asset: (asset: string, action: AssetAction, meta?: any) => {
      _socket.emit('asset', asset, action, meta);
    }
  },
  assets: {
    search: (query: string): Promise<IAsset[]> => _get(`/api/assets/search?q=${query}`),
  }
};

const initialState: ApiState = {
  presets: {},
  payload: {},
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
  .on(API.PAYLOAD, (state, payload) => {
    for (const object in payload) {
      const values = payload[object];
      for (const property in values)
        state = dotProp.set(state, `payload.${object}.${property}`, values[property]);
    }
    
    return state;
  })
  .on(API.PRESET_SELECT, (state, preset) => {
    _api.views.get(preset);
    _api.payload.get(preset);
    return { ...state, preset, view: { tabs: [] }, payload: {} };
  });

export default reducer;