add_executable(VulkanDemo)
set_target_properties(VulkanDemo PROPERTIES
	CXX_STANDARD 23
	CXX_STANDARD_REQUIRED YES
	CXX_EXTENSIONS NO

	CXX_SCAN_FOR_MODULES OFF
)

target_sources(VulkanDemo PRIVATE "Main.cpp")

find_package(Vulkan REQUIRED)
target_link_libraries(VulkanDemo PRIVATE Vulkan::Vulkan)

find_package(glfw3 CONFIG REQUIRED)
target_link_libraries(VulkanDemo PRIVATE glfw)