#include <ranges>
#include <iostream>
#include <stdexcept>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>

class Application
{
	using this_t = Application;

public:
	void Run()
	{
		Init();
		MainLoop();
		Terminate();
	}

private:
	void Init()
	{
		InitGLFW();
		InitVulkan();
	}

	void InitGLFW()
	{
		if (glfwInit() != GLFW_TRUE)
		{
			char const* msg;
			glfwGetError(&msg);
			throw std::runtime_error{msg};
		}

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, false);

		static constexpr int Width = 800, Height = 600;
		static constexpr auto Title = "Vulkan";
		m_wnd = glfwCreateWindow(Width, Height, Title, nullptr, nullptr);
		if (!m_wnd)
		{
			char const* msg;
			glfwGetError(&msg);
			throw std::runtime_error{msg};
		}
	}

	void InitVulkan()
	{
		CreateVulkanInstance();
		SetupDebugMessenger();
		CreateSurface();
		PickPhysicalDevice();
		CreateLogicalDevice();
		CreateSwapChain();
		CreateImageViews();
	}

	void CreateSurface()
	{
		VkSurfaceKHR surface;
		if (glfwCreateWindowSurface(*m_instance, m_wnd, nullptr, &surface) != VK_SUCCESS)
			throw std::runtime_error{"failed to create window surface"};

		m_surface = vk::raii::SurfaceKHR{m_instance, std::move(surface)};
	}

	vk::SurfaceFormatKHR ChooseSwapChainSurfaceFormat(std::vector<vk::SurfaceFormatKHR> const& availFormats)
	{
		auto const formatIt = std::ranges::find_if(availFormats, [](vk::SurfaceFormatKHR const& format)
		{
			return format.format == vk::Format::eB8G8R8A8Srgb &&
				format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
		});

		return formatIt != availFormats.end() ? *formatIt : availFormats.front();
	}

	vk::PresentModeKHR ChooseSwapChainPresentMode(std::vector<vk::PresentModeKHR> const& availPresentModes)
	{
		return std::ranges::contains(availPresentModes, vk::PresentModeKHR::eMailbox)
			? vk::PresentModeKHR::eMailbox
			: vk::PresentModeKHR::eFifo;
	}

	vk::Extent2D ChooseSwapChainExtent(vk::SurfaceCapabilitiesKHR const& surfCaps)
	{
		if (surfCaps.currentExtent.width != std::numeric_limits<uint32_t>::max())
			return surfCaps.currentExtent;

		int widthPx, heightPx;
		glfwGetFramebufferSize(m_wnd, &widthPx, &heightPx);

		return vk::Extent2D
		{
			.width = std::clamp<uint32_t>(widthPx, surfCaps.minImageExtent.width, surfCaps.maxImageExtent.width),
			.height = std::clamp<uint32_t>(heightPx, surfCaps.minImageExtent.height, surfCaps.maxImageExtent.height),
		};
	}

	uint32_t ChooseSwapChainMinImageCount(vk::SurfaceCapabilitiesKHR const& surfCaps)
	{
		static constexpr uint32_t DesiredImagesCount = 3u;
		if (surfCaps.maxImageCount == 0u)
			return std::max(surfCaps.minImageCount, DesiredImagesCount);

		return std::clamp(DesiredImagesCount, surfCaps.minImageCount, surfCaps.maxImageCount);
	}

	void CreateSwapChain()
	{
		vk::SurfaceCapabilitiesKHR const surfCaps = m_physicalDevice.getSurfaceCapabilitiesKHR(*m_surface);
		
		m_swapChainExtent = ChooseSwapChainExtent(surfCaps);
		m_swapChainSurfaceFormat = ChooseSwapChainSurfaceFormat(m_physicalDevice.getSurfaceFormatsKHR(*m_surface));

		m_swapChain = m_logicalDevice.createSwapchainKHR(vk::SwapchainCreateInfoKHR
		{
			.surface = m_surface,
			.minImageCount = ChooseSwapChainMinImageCount(surfCaps),
			.imageFormat = m_swapChainSurfaceFormat.format,
			.imageColorSpace = m_swapChainSurfaceFormat.colorSpace,
			.imageExtent = m_swapChainExtent,
			.imageArrayLayers = 1,
			.imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
			.imageSharingMode = vk::SharingMode::eExclusive,
			.preTransform = surfCaps.currentTransform,
			.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
			.presentMode = ChooseSwapChainPresentMode(m_physicalDevice.getSurfacePresentModesKHR(*m_surface)),
			.clipped = true,
			.oldSwapchain = nullptr,
		});

		m_swapChainImages = m_swapChain.getImages();
	}

	void CreateImageViews()
	{
		for (vk::Image const& image : m_swapChainImages)
		{
			m_swapChainImageViews.push_back(m_logicalDevice.createImageView(vk::ImageViewCreateInfo
			{
				.image = image,
				.viewType = vk::ImageViewType::e2DArray,
				.format = m_swapChainSurfaceFormat.format,
				.subresourceRange = vk::ImageSubresourceRange
				{
					.aspectMask = vk::ImageAspectFlagBits::eColor,
					.levelCount = 1,
					.layerCount = 1,
				},
			}));
		}
	}

	void CreateLogicalDevice()
	{
		uint32_t queueFamilyIndex = std::numeric_limits<uint32_t>::max();

		std::vector<vk::QueueFamilyProperties> const& queueFamilyProps = m_physicalDevice.getQueueFamilyProperties();
		for (uint32_t qfpIndex = 0; qfpIndex < queueFamilyProps.size(); ++qfpIndex)
		{
			vk::QueueFamilyProperties const& props = queueFamilyProps[qfpIndex];
			if ((props.queueFlags & vk::QueueFlagBits::eGraphics) &&
				m_physicalDevice.getSurfaceSupportKHR(qfpIndex, *m_surface))
			{
				queueFamilyIndex = qfpIndex;
				break;
			}
		}

		if (queueFamilyIndex == std::numeric_limits<uint32_t>::max())
			throw std::runtime_error{"Could not find a queue for graphics and present -> terminating"};

		static constexpr float QueuePriority = 1.0f;
		vk::DeviceQueueCreateInfo deviceQueueCreateInfo
		{
			.queueFamilyIndex = queueFamilyIndex,
			.queueCount = 1,
			.pQueuePriorities = &QueuePriority
		};

		vk::StructureChain featureChain =
		{
			vk::PhysicalDeviceFeatures2{},

			// Enable shader draw parameters from Vulkan 1.1
			vk::PhysicalDeviceVulkan11Features{ .shaderDrawParameters = true },

			// Enable dynamic rendering from Vulkan 1.3
			vk::PhysicalDeviceVulkan13Features{ .dynamicRendering = true },

			// Enable extended dynamic state from the extension
			vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT{ .extendedDynamicState = true }
		};

		std::vector<char const*> requiredDeviceExtension = { vk::KHRSwapchainExtensionName };

		m_logicalDevice = m_physicalDevice.createDevice(vk::DeviceCreateInfo
		{
			.pNext = &featureChain.get(),
			.queueCreateInfoCount = 1,
			.pQueueCreateInfos = &deviceQueueCreateInfo,
			.enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtension.size()),
			.ppEnabledExtensionNames = requiredDeviceExtension.data()
		});

		m_graphicsQueue = m_logicalDevice.getQueue(queueFamilyIndex, 0);
	}

	void PickPhysicalDevice()
	{
		auto isDeviceSuitable = [this](vk::raii::PhysicalDevice const & device)
		{
			vk::PhysicalDeviceProperties const& deviceProps = device.getProperties();
			if (deviceProps.apiVersion < vk::ApiVersion13)
				return false;

			std::vector<vk::QueueFamilyProperties> const& queueFamiliesProps = device.getQueueFamilyProperties();
			bool const isSupportGraphics = std::ranges::any_of(queueFamiliesProps, [](vk::QueueFamilyProperties const& props)
			{
				return !!(props.queueFlags & vk::QueueFlagBits::eGraphics);
			});

			if (!isSupportGraphics)
				return false;

			bool const isSupportSurface = [&]
			{
				for (size_t const qfpIndex : std::views::iota(0u, queueFamiliesProps.size()))
				{
					if (device.getSurfaceSupportKHR(qfpIndex, *m_surface))
						return true;
				}

				return false;
			}();

			if (!isSupportSurface)
				return false;

			std::vector<char const*> requiredDeviceExtensions = { vk::KHRSwapchainExtensionName };
			std::vector<vk::ExtensionProperties> const& deviceExtProps = device.enumerateDeviceExtensionProperties();
			bool const isSupportsAllRequiredExtensions = std::ranges::all_of(requiredDeviceExtensions, [&](char const* extName)
			{
				return std::ranges::any_of(deviceExtProps, [&](vk::ExtensionProperties const& props)
				{
					return std::strcmp(props.extensionName, extName) == 0;
				});
			});
			if (!isSupportsAllRequiredExtensions)
				return false;

			auto features = device.getFeatures2<
				vk::PhysicalDeviceFeatures2,
				vk::PhysicalDeviceVulkan11Features,
				vk::PhysicalDeviceVulkan13Features,
				vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
			>();

			bool isSupportsAllRequiredFeatures =
				features.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
				features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
				features.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;

			if (!isSupportsAllRequiredFeatures)
				return false;

			return true;
		};

		std::vector<vk::raii::PhysicalDevice> const physicalDevices = m_instance.enumeratePhysicalDevices();
		if (physicalDevices.empty())
			throw std::runtime_error{"failed to find GPUs with Vulkan support!"};


		std::cout << "List of available devices:" << std::endl;
		for (vk::raii::PhysicalDevice const & device : physicalDevices)
		{
			vk::PhysicalDeviceProperties const & props = device.getProperties();
			bool const isDiscrete = props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu;

			std::cout << "\t- Device name: " << props.deviceName << std::endl;
			std::cout << "\t- Vulkan version: "
				<< VK_API_VERSION_MAJOR(props.apiVersion) << '.'
				<< VK_API_VERSION_MINOR(props.apiVersion) << '.'
				<< VK_API_VERSION_PATCH(props.apiVersion) << std::endl;

			std::cout << "\t- Is discrete: " << std::boolalpha << isDiscrete << std::endl;
			std::cout << "\t- Is suitable: " << std::boolalpha << isDeviceSuitable(device) << std::endl;
			std::cout << std::endl;
		}
		std::cout << std::endl;

		auto suitableDeviceIt = physicalDevices.end();
		for (auto it = physicalDevices.begin(); it != physicalDevices.end(); ++it)
		{
			if (vk::raii::PhysicalDevice const& device = *it; isDeviceSuitable(device))
			{
				suitableDeviceIt = it;
				break;
			}
		}

		if (suitableDeviceIt == physicalDevices.end())
			throw std::runtime_error{"failed to find a suitable GPU"};

		m_physicalDevice = *suitableDeviceIt;
	}

	static std::vector<char const*> GetRequiredVkInstanceExtensions()
	{
		uint32_t extensionsCount = 0;
		char const** extensionNamesArray = glfwGetRequiredInstanceExtensions(&extensionsCount);

		std::vector result(extensionNamesArray, extensionNamesArray + extensionsCount);
		result.push_back(vk::EXTDebugUtilsExtensionName);

		return result;
	}

	static VkBool32 VKAPI_CALL DebugMessengerCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT const messageSeverity,
		vk::DebugUtilsMessageTypeFlagsEXT const messageType, vk::DebugUtilsMessengerCallbackDataEXT const* callbackData,
		void* /*userData*/)
	{
		auto severityToStr = [](vk::DebugUtilsMessageSeverityFlagBitsEXT const severity)
		{
			switch (severity)
			{
				case vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose: return "VERBOSE";
				case vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo: return "INFO";
				case vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning: return "WARNING";
				case vk::DebugUtilsMessageSeverityFlagBitsEXT::eError: return "ERROR";
				default: return "UNKNOWN";
			}
		};

		auto typeToStr = [](vk::DebugUtilsMessageTypeFlagsEXT const type) -> std::string
		{
			if (!type) return "none";

			std::string result;
			auto append = [&result](const char* s)
			{
				if (!result.empty()) result += " | ";
				result += s;
			};

			if (type & vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral)     append("general");
			if (type & vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation)  append("validation");
			if (type & vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance) append("performance");

			return result.empty() ? "unknown" : result;
		};

		std::cout << '[' << severityToStr(messageSeverity) << "] validation layer :: type: "
			<< typeToStr(messageType) << " / msg: " << callbackData->pMessage << std::endl;
		return vk::False;
	}

	void CreateVulkanInstance()
	{
		static constexpr vk::ApplicationInfo AppInfo =
		{
			.pApplicationName = "sandbox",
			.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
			.pEngineName = "No Engine",
			.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
			.apiVersion = vk::ApiVersion14
		};

		std::vector<char const*> const requiredExtensions = GetRequiredVkInstanceExtensions();

		std::vector<vk::ExtensionProperties> const extProps = m_context.enumerateInstanceExtensionProperties();
		for (char const* extName : requiredExtensions)
		{
			auto is_required_ext = [extName](vk::ExtensionProperties const & ext)
			{
				return std::strcmp(ext.extensionName, extName) == 0;
			};

			if (std::ranges::none_of(extProps, is_required_ext))
				throw std::runtime_error{"Required GLFW extension not supported: " + std::string{extName}};
		}

		std::cout << "Vulkan instance extensions required by application:" << std::endl;
		for (char const* extName : requiredExtensions)
			std::cout << "\t- " << extName << std::endl;
		std::cout << std::endl;

		std::cout << "Available vulkan instance extensions:" << std::endl;
		for (auto const& [extensionName, _] : extProps)
			std::cout << "\t- " << extensionName << std::endl;
		std::cout << std::endl;

		std::array requestedValidationLayers = { "VK_LAYER_KHRONOS_validation" };
		std::ranges::sort(requestedValidationLayers);

		std::vector<vk::LayerProperties> const & availableValidationLayers = m_context.enumerateInstanceLayerProperties();

		std::cout << "Requested validation layers:" << std::endl;
		for (std::string_view const layerName : requestedValidationLayers)
			std::cout << "\t- " << layerName << std::endl;
		std::cout << std::endl;

		std::cout << "Available validation layers:" << std::endl;
		for (vk::LayerProperties const & layer : availableValidationLayers)
			std::cout << "\t- " << layer.layerName << " (" << layer.description << ")" << std::endl;
		std::cout << std::endl;

		auto const unsupportedLayerIt = std::ranges::find_if(requestedValidationLayers, [&](char const * requiredLayer)
		{
			auto is_equal = [&](vk::LayerProperties const& layer)
			{ return std::strcmp(layer.layerName, requiredLayer) == 0; };

			return std::ranges::none_of(availableValidationLayers, is_equal);
		});

		if (unsupportedLayerIt != requestedValidationLayers.end())
			throw std::runtime_error{"Required layer not supported: " + std::string{*unsupportedLayerIt}};

		m_instance = m_context.createInstance(vk::InstanceCreateInfo
		{
			.pApplicationInfo = &AppInfo,
			.enabledLayerCount = static_cast<uint32_t>(requestedValidationLayers.size()),
			.ppEnabledLayerNames = requestedValidationLayers.data(),
			.enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size()),
			.ppEnabledExtensionNames = requiredExtensions.data(),
		});
	}

	void SetupDebugMessenger()
	{
		m_debugMessenger = m_instance.createDebugUtilsMessengerEXT(vk::DebugUtilsMessengerCreateInfoEXT
		{
			.messageSeverity =
				// vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
				vk::DebugUtilsMessageSeverityFlagBitsEXT::eError |
				// vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
				vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning,

			.messageType =
				vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
				vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
				vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,

			.pfnUserCallback = &this_t::DebugMessengerCallback
		});
	}

	void Terminate()
	{
		TerminateGLFW();
	}

	// ReSharper disable once CppMemberFunctionMayBeConst
	void TerminateGLFW()
	{
		glfwDestroyWindow(m_wnd);
		glfwTerminate();
	}

	void MainLoop() const
	{
		while (!glfwWindowShouldClose(m_wnd))
			glfwPollEvents();
	}

private:
	GLFWwindow* m_wnd = nullptr;

	vk::raii::Context m_context;
	vk::raii::Instance m_instance = nullptr;
	vk::raii::DebugUtilsMessengerEXT m_debugMessenger = nullptr;
	vk::raii::PhysicalDevice m_physicalDevice = nullptr;
	vk::raii::Device m_logicalDevice = nullptr;
	vk::raii::Queue m_graphicsQueue = nullptr;
	vk::raii::SurfaceKHR m_surface = nullptr;
	vk::raii::SwapchainKHR m_swapChain = nullptr;
	std::vector<vk::Image> m_swapChainImages;
	std::vector<vk::raii::ImageView> m_swapChainImageViews;
	vk::SurfaceFormatKHR m_swapChainSurfaceFormat;
	vk::Extent2D m_swapChainExtent;
};

int main()
{
	try
	{
		Application app;
		app.Run();
	}
	catch (std::exception const & ex)
	{
		std::cerr << ex.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
