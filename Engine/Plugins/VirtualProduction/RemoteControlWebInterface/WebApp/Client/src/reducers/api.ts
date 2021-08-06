import { Dispatch } from 'redux';
import { createAction, createReducer } from 'redux-act';
import dotProp from 'dot-prop-immutable';
import io from 'socket.io-client';
import { IAsset, IPayload, IPayloads, IPreset, IView, PropertyValue, TabLayout } from '../shared';
import _ from 'lodash';


export type ApiState = {
  presets: { [id: string]: IPreset };
  preset?: string;
  payload: IPayload;
  payloads: IPayloads;
  view: IView;
  status: {
    connected?: boolean;
    loading?: boolean;
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
    .on('connected', (connected: boolean) => {
      dispatch(API.STATUS({ connected, loading: false }));

      if (connected) {
          _api.presets.get();
          _api.payload.all();
      }
    })
    .on('loading', (loading: boolean) => {
      dispatch(API.STATUS({ loading }));
    });
}

type IRequestCallback = Function | string | undefined;

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
    load: (id: string): Promise<IPreset> => _get(`/api/presets/${id}/load`),
    select: (preset?: IPreset) => {
      _api.presets.load(preset?.ID);
      _dispatch(API.PRESET_SELECT(preset?.ID));
    },
  },
  views: {
    get: async(preset: string): Promise<IView> => {
      let view = await _get(`/api/presets/view?preset=${preset}`);
      if (typeof(view) !== 'object' || !view?.tabs?.length) {
        view = {
          tabs: [{ name: 'Tab 1', layout: TabLayout.Stack, panels: [] }]
        };
      }

      _dispatch(API.VIEW(view));

      return view;
    },
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
    execute: (func: string, args?: Record<string, any>) => {
      const preset = _internal.getPreset();
      _socket.emit('execute', preset, func, args ?? {});
    },
    metadata: (property: string, meta: string, value: string) => {
      const preset = _internal.getPreset();
      _socket.emit('metadata', preset, property, meta, value);
    }
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
    },
    property: {
      get: (objectPath: string, propertyName: string) => {
        const body = { 
          objectPath,
          propertyName,
          access: 'READ_ACCESS',
        };
        return _api.proxy.put('/remote/object/property', body);        
      },
      set: (objectPath: string, propertyName: string, propertyValue: any) => {
        const body = { 
          objectPath,
          propertyName,
          propertyValue: { [propertyName]: propertyValue },
          access: 'WRITE_ACCESS',
        };
        return _api.proxy.put('/remote/object/property', body);        
      }
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
  .on(API.STATUS, (state, status) => {
    return dotProp.merge(state, 'status', status);
  })
  .on(API.PRESETS, (state, presets) => {
    const presetsMap = _.keyBy(presets, 'ID');
    state = dotProp.set(state, 'presets', presetsMap);

    let { preset } = state;

    // Is loaded preset still available?
    if (preset && !presetsMap[preset])
      preset = undefined;

    // If there isn't a loaded preset
    if (!preset) {
      // 1. Load the preset name specified in the url
      const params = new URLSearchParams(window.location.search);
      preset = params.get('preset');

      // 2. Load last used preset
      if (!preset || !presetsMap[preset])
        preset = localStorage.getItem('preset');

      // 3. Load first preset
      if (!preset || !presetsMap[preset])
        preset = presets[0]?.ID;

      // No available preset
      if (!preset)
        return { ...state, preset: undefined, view: { tabs: null }, payload: {} };

      _api.presets.load(preset)
          .then(() => Promise.all([
              _api.views.get(preset),
              _api.payload.get(preset),
          ]));

      return { ...state, preset, view: { tabs: [] }, payload: {} };
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
    localStorage.setItem('preset', preset);
    _api.views.get(preset);
    _api.payload.get(preset);
    return { ...state, preset, view: { tabs: [] }, payload: {} };
  });

export default reducer;