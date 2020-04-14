# Copyright Epic Games, Inc. All Rights Reserved.

from .utils import dict_from_json
import gym.spaces
from collections import OrderedDict


def gym_space_from_ue4ml(data):
    if type(data) == str or type(data) == bytes:
        data = dict_from_json(data)
    return gym_space_from_list(data)

    
def gym_space_from_list(data):
    if type(data) == OrderedDict:
        data = list(data.values())

    if type(data) != list:
        #raise TypeError('Only dicts supported while data is {}'.format(type(data)))
        return data
    if len(data) == 0:
        return None    
    elif len(data) > 1:
        spaces = []
        for d in data:
            if type(d) == OrderedDict:
                spaces.append(gym_space_from_dict(d))
            else:
                spaces.append(gym_space_from_ue4ml(d))
        spaces = tuple(spaces)
        return gym.spaces.Tuple(spaces)
    else:
        key, val = list(data[0].items())[0]
        if key == 'Discrete':
            return gym.spaces.Discrete(val)
        elif key == 'MultiDiscrete':
            return gym.spaces.MultiDiscrete(val)
        elif key == 'Box':
            low = val[0]
            high = val[1]
            shape = val[2:]
            return gym.spaces.Box(low, high, shape=(shape))
        elif key == 'Tuple':
            return gym_space_from_ue4ml(val)
        else:
            return gym.spaces.Dict({key:gym_space_from_ue4ml(val)})


def gym_space_from_dict(data):
    data_type = type(data)
    if data_type != OrderedDict and data_type != list:
        #raise TypeError('Only dicts supported while data is {}'.format(data_type))
        return data
    if len(data) == 0:
        return None    
    elif len(data) > 1:
        if data_type == OrderedDict:
            d = {}
            for key in data:
                d[key] = gym_space_from_ue4ml(data[key]) 
            return gym.spaces.Dict(d)
            #spaces = (gym_space_from_ue4ml(data[key]) for key in data)
        else: # list
            #spaces = (gym_space_from_ue4ml(d) for d in data)
            spaces = []
            for d in data:
                if type(d) == OrderedDict:
                    spaces.append(gym_space_from_dict(d))
                else:
                    spaces.append(gym_space_from_ue4ml(d))
            spaces = tuple(spaces)
            return gym.spaces.Tuple(spaces)
    else:
        key = list(data.keys())[0]
        if key == 'Discrete':
            return gym.spaces.Discrete(data[key])
        elif key == 'MultiDiscrete':
            return gym.spaces.MultiDiscrete(data[key])
        elif key == 'Box':
            low = data[key][0]
            high = data[key][1]
            shape = data[key][2:]
            return gym.spaces.Box(low, high, shape=(shape))
        elif key == 'Tuple':
            return gym_space_from_ue4ml(data[key])
        else:
            return gym.spaces.Dict({key:gym_space_from_ue4ml(data[key])})

