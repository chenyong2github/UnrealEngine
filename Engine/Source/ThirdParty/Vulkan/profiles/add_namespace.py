# Use the generated header in the "debug" folder because we want the option to print specific errors
with open(r'./debug/vulkan_profiles.hpp', 'r') as f:
    data = file.read()
    
    # Add our namespace in front of functions assumed by the generation scripts
    data = data.replace("vkGetInstanceProcAddr(","VulkanRHI::vkGetInstanceProcAddr(")
    data = data.replace("vkEnumerateInstanceExtensionProperties(","VulkanRHI::vkEnumerateInstanceExtensionProperties(")
    data = data.replace("vkGetInstanceProcAddr(","VulkanRHI::vkGetInstanceProcAddr(")
    data = data.replace("vkEnumerateDeviceExtensionProperties(","VulkanRHI::vkEnumerateDeviceExtensionProperties(")
    data = data.replace("vkGetPhysicalDeviceProperties(","VulkanRHI::vkGetPhysicalDeviceProperties(")
    data = data.replace("vkCreateDevice(","VulkanRHI::vkCreateDevice(")

# Write final header with text namespaces added
with open(r'./include/vulkan_profiles_ue.h', 'w') as f:
    f.write(data)
