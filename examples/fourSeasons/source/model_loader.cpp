#include "imgui.h"

#include "camera_path.hpp"
#include "orbit_camera.hpp"
#include "configure_and_compose.hpp"
#include "imgui_manager.hpp"
#include "invokee.hpp"
#include "material_image_helpers.hpp"
#include "model.hpp"
#include "sequential_invoker.hpp"
#include "ui_helper.hpp"
#include "vk_convenience_functions.hpp"
#include "camera_path_recorder.hpp"
#include "math_utils.hpp"
#include <Windows.h>

#include <random>

#include "auto_vk_toolkit.hpp"

constexpr float CAM_NEAR = 0.3f;
constexpr float CAM_FAR = 1000.0f;

namespace global {
	constexpr size_t kernelSize = 64;
	constexpr size_t noiseSize = 16;
	constexpr size_t numPointLights = 10;
}


struct startOptions
{
	int width;
	int height;
	std::string sceneFile;
};
static startOptions mStartOptions;

class model_loader_app : public avk::invokee
{
	
	
	struct data_for_draw_call
	{
		std::vector<glm::vec3> mPositions;
		std::vector<glm::vec2> mTexCoords;
		std::vector<glm::vec3> mNormals;
		std::vector<uint32_t> mIndices;

		avk::buffer mPositionsBuffer;
		avk::buffer mTexCoordsBuffer;
		avk::buffer mNormalsBuffer;
		avk::buffer mIndexBuffer;

		int mMaterialIndex;
	};

	struct transformation_matrices {
		glm::mat4 mModelMatrix;
		int mMaterialIndex;
	};

	//for SSAO/GBuffer
	struct vp_matrices {
		glm::mat4 mViewMatrix;
		glm::mat4 mProjectionMatrix;
	};

	//for skybox
	struct view_projection_matrices {
		glm::mat4 mProjectionMatrix;
		glm::mat4 mModelViewMatrix;
		glm::mat4 mInverseModelViewMatrix;
		float mLodBias = 0.0f;
	};

	//for DoF
	struct DoFData {
		int mEnabled = 1;
		int mMode;
		float mFocus = 3.0f; //at what distance the focus is
		float mFocusRange = 1.5f; //how far the focus reaches (in both directions), i.e. the range of sharpness
		float mDistOutOfFocus = 3.0f; //how much from the start of the out of focus area until the image is completely out of focus
		float mNearPlane = 0.0f;
		float mFarPlane = 0.0f;
	};

	struct DoFKernelBufferStruct
	{
		// Using std::vector<glm::vec3> on the CPU is convenient, but it can cause issues when interfacing with Vulkan .
		// due to alignment and padding differences. Specifically, glm::vec3 does not include the padding required by
		// Vulkan's alignment rules (16 bytes for vec3), so you'll end up with mismatched data.
		// -> We use glm::vec4 instead, which is 16 bytes aligned.
		std::vector<glm::vec4> gaussianKernel;
		std::vector<glm::vec4> bokehKernel;
	};

	DoFKernelBufferStruct mDoFKernelBufferStruct;

	//for SSAO
	struct SSAOData {
		int mEnabled = 1;
		int mBlur = 1;
		int mIllumination = 1;
	};

	struct CameraData {
		glm::vec4 position;
	};

	struct LightingData {
		glm::vec3 sunColor;
	};

	avk::buffer mLightPositions;

	avk::buffer mSSAOKernel;
	avk::image_sampler mSSAONoiseTexture;

	const std::vector<glm::vec2> mScreenspaceQuadVertexData = {
		{-1.0f, -1.0f},
		{ 1.0f, -1.0f},
		{ 1.0f,  1.0f},
		{-1.0f,  1.0f}
	};

	const std::vector<uint16_t> mScreenspaceQuadIndexData = {
		0, 1, 2,
		2, 3, 0
	};

	

	
public: // v== avk::invokee overrides which will be invoked by the framework ==v
	model_loader_app(avk::queue& aQueue)
		: mQueue{ &aQueue }
		, mScale{1.0f, 1.0f, 1.0f}
	{}

	void init_ui()
	{
		// use helper functions to create ImGui elements
		auto surfaceCap = avk::context().physical_device().getSurfaceCapabilitiesKHR(avk::context().main_window()->surface());
		mPresentationModeCombo = model_loader_ui_generator::get_presentation_mode_imgui_element();
		mSrgbFrameBufferCheckbox = model_loader_ui_generator::get_framebuffer_mode_imgui_element();
		mNumConcurrentFramesSlider = model_loader_ui_generator::get_number_of_concurrent_frames_imgui_element();
		mNumPresentableImagesSlider = model_loader_ui_generator::get_number_of_presentable_images_imgui_element(3, surfaceCap.minImageCount, surfaceCap.maxImageCount);
		mResizableWindowCheckbox = model_loader_ui_generator::get_window_resize_imgui_element();
		//depth of field
		mDoFSliderFocus = slider_container<float>{ "Focus", 0.8f, 0.0f, 1, [this](float val) { this->mDoFFocus = val; } };
		mDoFSliderFocusRange = slider_container<float>{ "Range", 0.01f, 0.0f, 0.1, [this](float val) { this->mDoFFocusRange = val; } };
		mDoFSliderDistanceOutOfFocus = slider_container<float>{ "Dist", 0.05f, 0.0f, 0.2, [this](float val) { this->mDoFDistanceOutOfFocus = val; } };
		mDoFEnabledCheckbox = check_box_container{ "Enabled##1", true, [this](bool val) { this->mDoFEnabled = val; } };
		mDoFModeCombo = combo_box_container{ "Mode", { "blur", "near", "center", "far" }, 0, [this](std::string val) { this->mDoFMode = val; } };
		//ssao
		mSSAOEnabledCheckbox = check_box_container{ "Enabled##2", true, [&](bool val) { mSSAOEnabled = val; } };
		mSSAOBlurCheckbox = check_box_container{ "Blur", true, [&](bool val) { mSSAOBlur = val; } };
		mIlluminationCheckbox = check_box_container{ "Illumination", true, [&](bool val) { mIllumination = val; } };

		mDayCheckbox = check_box_container{ "Day", true, [&](bool val) { mDay = val; } };
	}

	void init_skybox()
	{
		avk::image cubemapImage;
		// Load cube map from file or from cache file:
		const std::string cacheFilePath("assets/cubemap.cache");
		auto serializer = avk::serializer(cacheFilePath);

		avk::command::action_type_command loadImageCommand;

		// Load the textures for all cubemap faces from one file (.ktx or .dds format), or from six individual files
		std::tie(cubemapImage, loadImageCommand) = avk::create_cubemap_from_file(
			"assets/SkyDawn.dds",
			true, // <-- load in HDR if possible 
			true, // <-- load in sRGB if applicable
			false // <-- flip along the y-axis
		);
		avk::context().record_and_submit_with_fence({ std::move(loadImageCommand) }, *mQueue)->wait_until_signalled();
		auto cubemapSampler = avk::context().create_sampler(avk::filter_mode::trilinear, avk::border_handling_mode::clamp_to_edge, static_cast<float>(cubemapImage->create_info().mipLevels));
		auto cubemapImageView = avk::context().create_image_view(cubemapImage, {}, avk::image_usage::general_cube_map_texture);
		mImageSamplerCubemap = avk::context().create_image_sampler(cubemapImageView, cubemapSampler);

		// Load a cube as the skybox from file
		// Since the cubemap uses a left-handed coordinate system, we declare the cube to be defined in the same coordinate system as well.
		// This simplifies coordinate transformations later on. To transform the cube vertices back to right-handed world coordinates for display,
		// we adjust its model matrix accordingly. Note that this also changes the winding order of faces, i.e. front faces
		// of the cube that have CCW order when viewed from the outside now have CCW order when viewed from inside the cube.
		
		auto cube = avk::model_t::load_from_file("assets/cube.obj", aiProcess_Triangulate | aiProcess_PreTransformVertices);

		auto& newElement = mDrawCallsSkybox.emplace_back();

		// 2. Build all the buffers for the GPU
		std::vector<avk::mesh_index_t> indices = { 0 };

		auto modelMeshSelection = avk::make_model_references_and_mesh_indices_selection(cube, indices);

		auto [mPositionsBuffer, mIndexBuffer, geometryCommands] = avk::create_vertex_and_index_buffers({ modelMeshSelection });
		avk::context().record_and_submit_with_fence({ std::move(geometryCommands) }, *mQueue)->wait_until_signalled();

		newElement.mPositionsBuffer = std::move(mPositionsBuffer);
		newElement.mIndexBuffer = std::move(mIndexBuffer);
		
		//Create Buffer for skybox
		mViewProjBufferSkybox = avk::context().create_buffer(
			avk::memory_usage::host_coherent, {},
			avk::uniform_buffer_meta::create_from_data(view_projection_matrices())
		);
	}

	void init_scene()
	{
		// Load a model from file:
		auto sponza = avk::model_t::load_from_file("assets/" + mStartOptions.sceneFile, aiProcess_Triangulate | aiProcess_PreTransformVertices);
		// Get all the different materials of the model:
		auto distinctMaterials = sponza->distinct_material_configs();

		// The following might be a bit tedious still, but maybe it's not. For what it's worth, it is expressive.
		// The following loop gathers all the vertex and index data PER MATERIAL and constructs the buffers and materials.
		// Later, we'll use ONE draw call PER MATERIAL to draw the whole scene.
		std::vector<avk::material_config> allMatConfigs;
		for (const auto& pair : distinctMaterials) {
			auto& newElement = mDrawCalls.emplace_back();
			allMatConfigs.push_back(pair.first);
			newElement.mMaterialIndex = static_cast<int>(allMatConfigs.size() - 1);

			// 1. Gather all the vertex and index data from the sub meshes:
			for (auto index : pair.second) {
				avk::append_indices_and_vertex_data(
					avk::additional_index_data(	newElement.mIndices,	[&]() { return sponza->indices_for_mesh<uint32_t>(index);					} ),
					avk::additional_vertex_data(newElement.mPositions,	[&]() { return sponza->positions_for_mesh(index);							} ),
					avk::additional_vertex_data(newElement.mTexCoords,	[&]() { return sponza->texture_coordinates_for_mesh<glm::vec2>(index, 0);	} ),
					avk::additional_vertex_data(newElement.mNormals,	[&]() { return sponza->normals_for_mesh(index);								} )
				);
			}

			// 2. Build all the buffers for the GPU
			// 2.1 Positions:
			newElement.mPositionsBuffer = avk::context().create_buffer(
				avk::memory_usage::device, {},
				avk::vertex_buffer_meta::create_from_data(newElement.mPositions)
			);
			auto posFillCmd = newElement.mPositionsBuffer->fill(newElement.mPositions.data(), 0);

			// 2.2 Texture Coordinates:
			newElement.mTexCoordsBuffer = avk::context().create_buffer(
				avk::memory_usage::device, {},
				avk::vertex_buffer_meta::create_from_data(newElement.mTexCoords)
			);
			auto tcoFillCmd = newElement.mTexCoordsBuffer->fill(newElement.mTexCoords.data(), 0);

			// 2.3 Normals:
			newElement.mNormalsBuffer = avk::context().create_buffer(
				avk::memory_usage::device, {},
				avk::vertex_buffer_meta::create_from_data(newElement.mNormals)
			);
			auto nrmFillCmd = newElement.mNormalsBuffer->fill(newElement.mNormals.data(), 0);

			// 2.4 Indices:
			newElement.mIndexBuffer = avk::context().create_buffer(
				avk::memory_usage::device, {},
				avk::index_buffer_meta::create_from_data(newElement.mIndices)
			);
			auto idxFillCmd = newElement.mIndexBuffer->fill(newElement.mIndices.data(), 0);

			// Submit all the fill commands to the queue:
			auto fence = avk::context().record_and_submit_with_fence({
				std::move(posFillCmd),
				std::move(tcoFillCmd),
				std::move(nrmFillCmd),
				std::move(idxFillCmd)
				// ^ No need for any synchronization in-between, because the commands do not depend on each other.
			}, *mQueue);
			// Wait on the host until the device is done:
			fence->wait_until_signalled();
		}

		// For all the different materials, transfer them in structs which are well
		// suited for GPU-usage (proper alignment, and containing only the relevant data),
		// also load all the referenced images from file and provide access to them
		// via samplers; It all happens in `ak::convert_for_gpu_usage`:
		auto [gpuMaterials, imageSamplers, materialCommands] = avk::convert_for_gpu_usage<avk::material_gpu_data>(
			allMatConfigs, false, true,
			avk::image_usage::general_texture,
			avk::filter_mode::trilinear
		);

		mImageSamplers = std::move(imageSamplers);

		// A buffer to hold all the material data:
		mMaterialBuffer = avk::context().create_buffer(
			avk::memory_usage::device, {},
			avk::storage_buffer_meta::create_from_data(gpuMaterials)
		);

		// Submit the commands material commands and the materials buffer fill to the device:
		auto matFence = avk::context().record_and_submit_with_fence({
			std::move(materialCommands),
			mMaterialBuffer->fill(gpuMaterials.data(), 0)
		}, *mQueue);
		matFence->wait_until_signalled();

		mViewProjBuffer = avk::context().create_buffer(
			avk::memory_usage::host_coherent, {},
			avk::uniform_buffer_meta::create_from_data(vp_matrices())
		);

		// Create a buffer for the transformation matrices in a host coherent memory region (one for each frame in flight):
		for (int i = 0; i < 10; ++i) { // Up to 10 concurrent frames can be configured through the UI.
			mViewProjBuffers[i] = avk::context().create_buffer(
				avk::memory_usage::host_coherent, {},
				avk::uniform_buffer_meta::create_from_data(glm::mat4())
			);
		}
	}


	void init_ssao_data()
	{
		std::vector<glm::vec4> kernel;
		std::vector<glm::vec4> noise;
		kernel.reserve(global::kernelSize);
		noise.reserve(global::noiseSize);
		std::uniform_real_distribution<float> range(0.0, 1.0);
		std::mt19937 randomEngine;
		// Generate samples in a hemisphere
		for (size_t i = 0; i < global::kernelSize; i++) {
			glm::vec3 sample(
				range(randomEngine) * 2.0 - 1.0,
				range(randomEngine) * 2.0 - 1.0,
				range(randomEngine)
			);
			sample = glm::normalize(sample);
			sample *= range(randomEngine);

			// Scale so samples are distributed closer to the origin of the hemisphere
			float scale = (float)i / global::kernelSize;
			// lerp
			scale = 0.1 + scale*scale * (1.0 - 0.1);

			kernel.push_back(glm::vec4(sample * scale, 0.0f));
		}

		mSSAOKernel = avk::context().create_buffer(
			avk::memory_usage::host_coherent, {},
			avk::uniform_buffer_meta::create_from_data(kernel)
		);
		mSSAOKernel->fill(kernel.data(), 0);

		// Create a rotation vectors for the noise texture
		for (size_t i = 0; i < global::noiseSize; i++) {
			noise.push_back(glm::vec4(
				range(randomEngine) * 2.0 - 1.0,
				range(randomEngine) * 2.0 - 1.0,
				// We want to rotate around the z axis (up)
				0.0f,
				// Filler for vec4
				0.0f
			));
		}

		auto noiseImage = avk::context().create_image(
			4, 4, vk::Format::eR32G32B32A32Sfloat,
			1, avk::memory_usage::device, avk::image_usage::general_color_attachment
		);
		auto noiseBuffer = avk::context().create_buffer(
			avk::memory_usage::host_coherent, vk::BufferUsageFlagBits::eTransferSrc,
			avk::generic_buffer_meta::create_from_data(noise)
		);
		avk::context().record_and_submit_with_fence(
			{ noiseBuffer->fill(noise.data(), 0) },
			*mQueue
		)->wait_until_signalled();

		avk::context().record_and_submit_with_fence({
		avk::sync::image_memory_barrier(noiseImage.as_reference(), avk::stage::none >> avk::stage::copy,
			avk::access::none >> avk::access::transfer_read | avk::access::transfer_write).with_layout_transition(
				avk::layout::undefined >> avk::layout::transfer_dst),
		avk::copy_buffer_to_image(noiseBuffer, noiseImage.as_reference(), avk::layout::transfer_dst),
		avk::sync::image_memory_barrier(noiseImage.as_reference(), avk::stage::copy >> avk::stage::transfer,
			avk::access::transfer_write >> avk::access::none).with_layout_transition(
				avk::layout::transfer_dst >> avk::layout::shader_read_only_optimal)
		}, *mQueue)->wait_until_signalled();

		mSSAONoiseTexture = avk::context().create_image_sampler(
			avk::context().create_image_view(
				noiseImage
			),
			avk::context().create_sampler(avk::filter_mode::nearest_neighbor, avk::border_handling_mode::repeat)
		);
	}

	std::vector<glm::vec4> init_bokeh_kernel()
	{
		// Circular Kernel from GPU Zen 'Practical Gather-based Bokeh Depth of Field' by Wojciech Sterna
		 auto kernel = {
		 	glm::vec2(1.000000f, 0.000000f) * 2.0f,
			glm::vec2(0.707107f, 0.707107f) * 2.0f,
			glm::vec2(-0.000000f, 1.000000f) * 2.0f,
			glm::vec2(-0.707107f, 0.707107f) * 2.0f,
			glm::vec2(-1.000000f, -0.000000f) * 2.0f,
			glm::vec2(-0.707106f, -0.707107f) * 2.0f,
			glm::vec2(0.000000f, -1.000000f) * 2.0f,
			glm::vec2(0.707107f, -0.707107f) * 2.0f,

			glm::vec2(1.000000f, 0.000000f) * 4.0f,
			glm::vec2(0.923880f, 0.382683f) * 4.0f,
			glm::vec2(0.707107f, 0.707107f) * 4.0f,
			glm::vec2(0.382683f, 0.923880f) * 4.0f,
			glm::vec2(-0.000000f, 1.000000f) * 4.0f,
			glm::vec2(-0.382684f, 0.923879f) * 4.0f,
			glm::vec2(-0.707107f, 0.707107f) * 4.0f,
			glm::vec2(-0.923880f, 0.382683f) * 4.0f,
			glm::vec2(-1.000000f, -0.000000f) * 4.0f,
			glm::vec2(-0.923879f, -0.382684f) * 4.0f,
			glm::vec2(-0.707106f, -0.707107f) * 4.0f,
			glm::vec2(-0.382683f, -0.923880f) * 4.0f,
			glm::vec2(0.000000f, -1.000000f) * 4.0f,
			glm::vec2(0.382684f, -0.923879f) * 4.0f,
			glm::vec2(0.707107f, -0.707107f) * 4.0f,
			glm::vec2(0.923880f, -0.382683f) * 4.0f,

			glm::vec2(1.000000f, 0.000000f) * 6.0f,
			glm::vec2(0.965926f, 0.258819f) * 6.0f,
			glm::vec2(0.866025f, 0.500000f) * 6.0f,
			glm::vec2(0.707107f, 0.707107f) * 6.0f,
			glm::vec2(0.500000f, 0.866026f) * 6.0f,
			glm::vec2(0.258819f, 0.965926f) * 6.0f,
			glm::vec2(-0.000000f, 1.000000f) * 6.0f,
			glm::vec2(-0.258819f, 0.965926f) * 6.0f,
			glm::vec2(-0.500000f, 0.866025f) * 6.0f,
			glm::vec2(-0.707107f, 0.707107f) * 6.0f,
			glm::vec2(-0.866026f, 0.500000f) * 6.0f,
			glm::vec2(-0.965926f, 0.258819f) * 6.0f,
			glm::vec2(-1.000000f, -0.000000f) * 6.0f,
			glm::vec2(-0.965926f, -0.258820f) * 6.0f,
			glm::vec2(-0.866025f, -0.500000f) * 6.0f,
			glm::vec2(-0.707106f, -0.707107f) * 6.0f,
			glm::vec2(-0.499999f, -0.866026f) * 6.0f,
			glm::vec2(-0.258819f, -0.965926f) * 6.0f,
			glm::vec2(0.000000f, -1.000000f) * 6.0f,
			glm::vec2(0.258819f, -0.965926f) * 6.0f,
			glm::vec2(0.500000f, -0.866025f) * 6.0f,
			glm::vec2(0.707107f, -0.707107f) * 6.0f,
			glm::vec2(0.866026f, -0.499999f) * 6.0f,
			glm::vec2(0.965926f, -0.258818f) * 6.0f
		};
		//convert each vec2 to an vec4 with 1 and 1 as z and w
		std::vector<glm::vec4> kernel2;
		for (auto& k : kernel) {
			kernel2.emplace_back(k.x, k.y, 1, 1);
		}
		return kernel2;
	}

	std::vector<glm::vec4> init_gaussian_kernel(int aSize)
	{
		//x and y are the coordinates of the kernel, z is the weight
		std::vector<glm::vec4> kernel;
		// Generate a 2D Gaussian kernel
		float sigma = 1.0f;
		float twoSigma2 = 2.0f * sigma * sigma;
		float weightSum = 0.0f;
		for (int x = -aSize; x <= aSize; x++) {
			for (int y = -aSize; y <= aSize; y++) {
				float r = sqrt(x * x + y * y);
				float weight = (glm::exp(-(r * r) / twoSigma2)) / (glm::pi<float>() * twoSigma2);
				weightSum += weight;
				kernel.emplace_back(x, y, weight, 1);
			}
		}
		// Normalize the kernel
		for (auto& k : kernel) {
			k.z /= weightSum;
		}
		return kernel;
	}
	

	void initialize() override
	{
		init_ui();
		
		mInitTime = std::chrono::high_resolution_clock::now();

		// Create a descriptor cache that helps us to conveniently create descriptor sets:
		mDescriptorCache = avk::context().create_descriptor_cache();
		
		init_skybox();
		init_scene();

		//Create a Framebuffer for the screenspace effects (Main scene renders into this framebuffer, then this
		const auto r = avk::context().main_window()->resolution();

		// mImageViewScreenspaceColor = avk::context().create_image_view(avk::context().create_image(r.x, r.y, vk::Format::eR8G8B8A8Unorm, 1, avk::memory_usage::device, avk::image_usage::general_color_attachment));
		// mImageViewScreenspaceDepth =  avk::context().create_depth_image_view(avk::context().create_depth_image(r.x, r.y, vk::Format::eD32Sfloat, 1, avk::memory_usage::device, avk::image_usage::general_depth_stencil_attachment));

		//framebuffer is used to render a quad with the screenspace effect).
		auto colorAttachmentRaster = avk::context().create_image_view(avk::context().create_image(r.x, r.y, vk::Format::eR8G8B8A8Unorm, 1, avk::memory_usage::device, avk::image_usage::general_color_attachment));
		auto colorAttachmentDescriptionRaster = avk::attachment::declare_for(colorAttachmentRaster.as_reference(), avk::on_load::load.from_previous_layout(avk::layout::color_attachment_optimal), avk::usage::color(0), avk::on_store::store);
		auto depthAttachmentRaster = avk::context().create_depth_image_view(avk::context().create_depth_image(r.x, r.y, vk::Format::eD32Sfloat, 1, avk::memory_usage::device, avk::image_usage::general_depth_stencil_attachment));
		auto depthAttachmentDescription = avk::attachment::declare_for(depthAttachmentRaster.as_reference(), avk::on_load::clear.from_previous_layout(avk::layout::depth_stencil_attachment_optimal), avk::usage::depth_stencil, avk::on_store::store);

		//For G-Buffer we also need some more data
		auto positionAttachmentRaster = avk::context().create_image_view(avk::context().create_image(r.x, r.y, vk::Format::eR32G32B32A32Sfloat, 1, avk::memory_usage::device, avk::image_usage::general_color_attachment));
		auto positionAttachmentDescription = avk::attachment::declare_for(positionAttachmentRaster.as_reference(), avk::on_load::clear.from_previous_layout(avk::layout::color_attachment_optimal), avk::usage::color(1), avk::on_store::store);
		auto normalsAttachmentRaster = avk::context().create_image_view(avk::context().create_image(r.x, r.y, vk::Format::eR32G32B32A32Sfloat, 1, avk::memory_usage::device, avk::image_usage::general_color_attachment));
		auto normalsAttachmentDescription = avk::attachment::declare_for(normalsAttachmentRaster.as_reference(), avk::on_load::clear.from_previous_layout(avk::layout::color_attachment_optimal), avk::usage::color(2), avk::on_store::store);
		auto positionWSAttachmentRaster = avk::context().create_image_view(avk::context().create_image(r.x, r.y, vk::Format::eR32G32B32A32Sfloat, 1, avk::memory_usage::device, avk::image_usage::general_color_attachment));
		auto positionWSAttachmentDescription = avk::attachment::declare_for(positionWSAttachmentRaster.as_reference(), avk::on_load::clear.from_previous_layout(avk::layout::color_attachment_optimal), avk::usage::color(3), avk::on_store::store);
		auto normalsWSAttachmentRaster = avk::context().create_image_view(avk::context().create_image(r.x, r.y, vk::Format::eR32G32B32A32Sfloat, 1, avk::memory_usage::device, avk::image_usage::general_color_attachment));
		auto normalsWSAttachmentDescription = avk::attachment::declare_for(normalsWSAttachmentRaster.as_reference(), avk::on_load::clear.from_previous_layout(avk::layout::color_attachment_optimal), avk::usage::color(4), avk::on_store::store);

		//SSAO
		auto colorAttachmentSSAO = avk::context().create_image_view(avk::context().create_image(r.x, r.y, vk::Format::eR8G8B8A8Unorm, 1, avk::memory_usage::device, avk::image_usage::general_color_attachment));
		auto colorAttachmentDescriptionSSAO = avk::attachment::declare_for(colorAttachmentSSAO.as_reference(), avk::on_load::load.from_previous_layout(avk::layout::color_attachment_optimal), avk::usage::color(0), avk::on_store::store);

		auto colorAttachmentSSAOBlur = avk::context().create_image_view(avk::context().create_image(r.x, r.y, vk::Format::eR8G8B8A8Unorm, 1, avk::memory_usage::device, avk::image_usage::general_color_attachment));
		auto colorAttachmentDescriptionSSAOBlur = avk::attachment::declare_for(colorAttachmentSSAOBlur.as_reference(), avk::on_load::load.from_previous_layout(avk::layout::color_attachment_optimal), avk::usage::color(0), avk::on_store::store);

		auto colorAttachmentIllumination = avk::context().create_image_view(avk::context().create_image(r.x, r.y, vk::Format::eR8G8B8A8Unorm, 1, avk::memory_usage::device, avk::image_usage::general_color_attachment));
		auto colorAttachmentDescriptionIllumination = avk::attachment::declare_for(colorAttachmentIllumination.as_reference(), avk::on_load::load.from_previous_layout(avk::layout::color_attachment_optimal), avk::usage::color(0), avk::on_store::store);

		//DOF
		auto colorAttachmentNearDof = avk::context().create_image_view(avk::context().create_image(r.x, r.y, vk::Format::eR8G8B8A8Unorm, 1, avk::memory_usage::device, avk::image_usage::general_color_attachment));
		auto colorAttachmentDescriptionNearDof = avk::attachment::declare_for(colorAttachmentRaster.as_reference(), avk::on_load::load.from_previous_layout(avk::layout::color_attachment_optimal), avk::usage::color(0), avk::on_store::store);

		auto colorAttachmentNearBleedDof = avk::context().create_image_view(avk::context().create_image(r.x, r.y, vk::Format::eR8G8B8A8Unorm, 1, avk::memory_usage::device, avk::image_usage::general_color_attachment));
		auto colorAttachmentDescriptionNearBleedDof = avk::attachment::declare_for(colorAttachmentRaster.as_reference(), avk::on_load::load.from_previous_layout(avk::layout::color_attachment_optimal), avk::usage::color(0), avk::on_store::store);
		
		auto colorAttachmentCenterDof = avk::context().create_image_view(avk::context().create_image(r.x, r.y, vk::Format::eR8G8B8A8Unorm, 1, avk::memory_usage::device, avk::image_usage::general_color_attachment));
		auto colorAttachmentDescriptionCenterDof = avk::attachment::declare_for(colorAttachmentRaster.as_reference(), avk::on_load::load.from_previous_layout(avk::layout::color_attachment_optimal), avk::usage::color(0), avk::on_store::store);
		
		auto colorAttachmentFarDof = avk::context().create_image_view(avk::context().create_image(r.x, r.y, vk::Format::eR8G8B8A8Unorm, 1, avk::memory_usage::device, avk::image_usage::general_color_attachment));
		auto colorAttachmentDescriptionFarDof = avk::attachment::declare_for(colorAttachmentRaster.as_reference(), avk::on_load::load.from_previous_layout(avk::layout::color_attachment_optimal), avk::usage::color(0), avk::on_store::store);
		


		mRasterizerFramebuffer = avk::context().create_framebuffer(
			{ colorAttachmentDescriptionRaster, positionAttachmentDescription, normalsAttachmentDescription, positionWSAttachmentDescription, normalsWSAttachmentDescription, depthAttachmentDescription },
			avk::make_vector( colorAttachmentRaster, positionAttachmentRaster, normalsAttachmentRaster, positionWSAttachmentRaster, normalsWSAttachmentRaster, depthAttachmentRaster )
		);
		auto samplerLin = avk::context().create_sampler(avk::filter_mode::trilinear, avk::border_handling_mode::clamp_to_edge, 0);
		auto samplerNea = avk::context().create_sampler(avk::filter_mode::nearest_neighbor, avk::border_handling_mode::clamp_to_edge, 0);
		mImageSamplerRasterFBColor = avk::context().create_image_sampler(mRasterizerFramebuffer->image_view_at(0), samplerNea);
		mImageSamplerRasterFBPosition = avk::context().create_image_sampler(mRasterizerFramebuffer->image_view_at(1), samplerNea);
		mImageSamplerRasterFBNormals = avk::context().create_image_sampler(mRasterizerFramebuffer->image_view_at(2), samplerNea);
		mImageSamplerRasterFBPositionWS = avk::context().create_image_sampler(mRasterizerFramebuffer->image_view_at(3), samplerNea);
		mImageSamplerRasterFBNormalsWS = avk::context().create_image_sampler(mRasterizerFramebuffer->image_view_at(4), samplerNea);
		mImageSamplerRasterFBDepth = avk::context().create_image_sampler(mRasterizerFramebuffer->image_view_at(5), samplerNea);

		mSSAOFramebuffer = avk::context().create_framebuffer(
			{ colorAttachmentDescriptionSSAO }, 
			avk::make_vector(colorAttachmentSSAO)
		);
		mImageSamplerSSAOFBColor = avk::context().create_image_sampler(mSSAOFramebuffer->image_view_at(0), samplerNea);

		mSSAOBlurFramebuffer = avk::context().create_framebuffer(
			{ colorAttachmentDescriptionSSAOBlur },
			avk::make_vector(colorAttachmentSSAOBlur)
		);
		mImageSamplerSSAOBlurFBColor = avk::context().create_image_sampler(mSSAOBlurFramebuffer->image_view_at(0), samplerLin);

		mIlluminationFramebuffer = avk::context().create_framebuffer(
			{ colorAttachmentDescriptionIllumination },
			avk::make_vector(colorAttachmentIllumination)
		);
		mImageSamplerIlluminationFBColor = avk::context().create_image_sampler(mIlluminationFramebuffer->image_view_at(0), samplerLin);

		mDofNearFieldFB = avk::context().create_framebuffer(
			{ colorAttachmentDescriptionNearDof },
			avk::make_vector(colorAttachmentNearDof)
		);
		mImageSamplerDofNearColor = avk::context().create_image_sampler(mDofNearFieldFB->image_view_at(0), samplerLin);

		mDofNearFieldBleedFB = avk::context().create_framebuffer(
			{ colorAttachmentDescriptionNearBleedDof },
			avk::make_vector(colorAttachmentNearBleedDof)
		);
		mImageSamplerDofNearBleedColor = avk::context().create_image_sampler(mDofNearFieldBleedFB->image_view_at(0), samplerLin);
		
		mDofCenterFieldFB = avk::context().create_framebuffer(
			{ colorAttachmentDescriptionCenterDof },
			avk::make_vector(colorAttachmentCenterDof)
		);
		mImageSamplerDofCenterColor = avk::context().create_image_sampler(mDofCenterFieldFB->image_view_at(0), samplerLin);

		mDofFarFieldFB = avk::context().create_framebuffer(
			{ colorAttachmentDescriptionFarDof },
			avk::make_vector(colorAttachmentFarDof)
		);
		mImageSamplerDofFarColor = avk::context().create_image_sampler(mDofFarFieldFB->image_view_at(0), samplerLin);
		
		
		//Create Buffer and Data for Screenspace effects
		mDoFBuffer = avk::context().create_buffer(
			avk::memory_usage::host_coherent, {},
			avk::uniform_buffer_meta::create_from_data(DoFData())
		);

		mSSAOBuffer = avk::context().create_buffer(
			avk::memory_usage::host_coherent, {},
			avk::uniform_buffer_meta::create_from_data(SSAOData())
		);

		mCameraData = avk::context().create_buffer(
			avk::memory_usage::host_coherent, {},
			avk::uniform_buffer_meta::create_from_data(CameraData())
		);

		mLightingData = avk::context().create_buffer(
			avk::memory_usage::host_coherent, {},
			avk::uniform_buffer_meta::create_from_data(LightingData())
		);

		init_ssao_data();

		std::uniform_real_distribution<double> rangeXZ(0, 10.0);
		std::uniform_real_distribution<double> rangeY(2.0, 10.0);
		std::mt19937 randomEngine;
		std::vector<glm::vec3> lightPositions;
		lightPositions.reserve(global::numPointLights);
		for (size_t i = 0; i < global::numPointLights; i++) {
			lightPositions.push_back(glm::vec3(rangeXZ(randomEngine), 10.0, rangeXZ(randomEngine)));
		}

		mLightPositions = avk::context().create_buffer(
			avk::memory_usage::host_coherent, {},
			avk::uniform_buffer_meta::create_from_data(lightPositions)
		);
		avk::context().record_and_submit_with_fence({
			mLightPositions->fill(lightPositions.data(), 0)
		}, *mQueue)->wait_until_signalled();


		// A buffer to hold all the material data:
		// mMaterialBuffer = avk::context().create_buffer(
		// 	avk::memory_usage::device, {},
		// 	avk::storage_buffer_meta::create_from_data(gpuMaterials)
		// );
		//
		// // Submit the commands material commands and the materials buffer fill to the device:
		// auto matFence = avk::context().record_and_submit_with_fence({
		// 	std::move(materialCommands),
		// 	mMaterialBuffer->fill(gpuMaterials.data(), 0)
		// }, *mQueue);
		// matFence->wait_until_signalled();
		
		mDoFKernelBufferStruct = { .gaussianKernel= init_gaussian_kernel(3), .bokehKernel= init_bokeh_kernel() };
		//try zero initialization isntead
		// mDoFKernelBufferStruct = {.gaussianKernel = std::vector<glm::vec4>(49, glm::vec4(0)), .bokehKernel = std::vector<glm::vec4>(48, glm::vec4(0))};
		mDoFKernelBufferGaussian = avk::context().create_buffer(
			avk::memory_usage::device, {},
			avk::storage_buffer_meta::create_from_data(mDoFKernelBufferStruct.gaussianKernel)
		);
		// Submit the Vertex Buffer fill command to the device:
		auto fence = avk::context().record_and_submit_with_fence({
			mDoFKernelBufferGaussian->fill(mDoFKernelBufferStruct.gaussianKernel.data(), 0)
		}, *mQueue);
		// Wait on the host until the device is done:
		fence->wait_until_signalled();

		mDoFKernelBufferBokeh = avk::context().create_buffer(
			avk::memory_usage::device, {},
			avk::storage_buffer_meta::create_from_data(mDoFKernelBufferStruct.bokehKernel)
		);
		// Submit the Vertex Buffer fill command to the device:
		auto fence2 = avk::context().record_and_submit_with_fence({
			mDoFKernelBufferBokeh->fill(mDoFKernelBufferStruct.bokehKernel.data(), 0)
		}, *mQueue);
		// Wait on the host until the device is done:
		fence2->wait_until_signalled();
		
		
		//Create Vertex Buffer for Screenspace Quad
		{
			mVertexBufferScreenspace = avk::context().create_buffer(
				avk::memory_usage::device, {},
				// Create the buffer on the device, i.e. in GPU memory, (no additional usage flags).
				// because the screenspace quad is static, we can use device memory
				avk::vertex_buffer_meta::create_from_data(mScreenspaceQuadVertexData)
			);
			// Submit the Vertex Buffer fill command to the device:
			auto fence3 = avk::context().record_and_submit_with_fence({
				mVertexBufferScreenspace->fill(mScreenspaceQuadVertexData.data(), 0)
			}, *mQueue);
			// Wait on the host until the device is done:
			fence3->wait_until_signalled();

			mIndexBufferScreenspace = avk::context().create_buffer(
				avk::memory_usage::device, {},
				avk::index_buffer_meta::create_from_data(mScreenspaceQuadIndexData)
			);
			auto fence2 = avk::context().record_and_submit_with_fence({
				mIndexBufferScreenspace->fill(mScreenspaceQuadIndexData.data(), 0)
			}, *mQueue);
			fence2->wait_until_signalled();
			
		}

		mPipelineSkybox = avk::context().create_graphics_pipeline_for(
			// Specify which shaders the pipeline consists of:
			avk::vertex_shader("shaders/skybox.vert"),
			avk::fragment_shader("shaders/skybox.frag"),
			// The next line defines the format and location of the vertex shader inputs:
			// (The dummy values (like glm::vec3) tell the pipeline the format of the respective input)
			avk::from_buffer_binding(0)->stream_per_vertex<glm::vec3>()->to_location(0), // <-- corresponds to vertex shader's inPosition
			// Some further settings:
			avk::cfg::front_face::define_front_faces_to_be_counter_clockwise(),
			avk::cfg::viewport_depth_scissors_config::from_framebuffer(mRasterizerFramebuffer.as_reference()),
			// We'll render to the framebuffer, only color no depth needed cause skybox is always at infinity
			// mColorAttachmentDescription,
			avk::context().create_renderpass(
			{
				colorAttachmentDescriptionRaster,
				positionAttachmentDescription,
				normalsAttachmentDescription,
				positionWSAttachmentDescription,
				normalsWSAttachmentDescription,
				depthAttachmentDescription
			}),
			
			// The following define additional data which we'll pass to the pipeline:
			avk::descriptor_binding(0, 0, mViewProjBufferSkybox),
			avk::descriptor_binding(0, 1, mImageSamplerCubemap->as_combined_image_sampler(avk::layout::general))
		);
		
		auto swapChainFormat = avk::context().main_window()->swap_chain_image_format();
		// Create our rasterization graphics pipeline with the required configuration:
		mRasterizePipeline = avk::context().create_graphics_pipeline_for(
			// Specify which shaders the pipeline consists of:
			avk::vertex_shader("shaders/raster.vert"),
			avk::fragment_shader("shaders/raster.frag"),
			// The next 3 lines define the format and location of the vertex shader inputs:
			// (The dummy values (like glm::vec3) tell the pipeline the format of the respective input)
			avk::from_buffer_binding(0) -> stream_per_vertex<glm::vec3>() -> to_location(0), // <-- corresponds to vertex shader's inPosition
			avk::from_buffer_binding(1) -> stream_per_vertex<glm::vec2>() -> to_location(1), // <-- corresponds to vertex shader's inTexCoord
			avk::from_buffer_binding(2) -> stream_per_vertex<glm::vec3>() -> to_location(2), // <-- corresponds to vertex shader's inNormal
			// Some further settings:
			avk::cfg::front_face::define_front_faces_to_be_counter_clockwise(),
			avk::cfg::viewport_depth_scissors_config::from_framebuffer(mRasterizerFramebuffer.as_reference()), // Align viewport with framebuffer resolution
			
			// We'll render to the framebuffer
			avk::context().create_renderpass(
			{
				colorAttachmentDescriptionRaster,
				positionAttachmentDescription,
				normalsAttachmentDescription,
				positionWSAttachmentDescription,
				normalsWSAttachmentDescription,
				depthAttachmentDescription
			}),
				
			
			// The following define additional data which we'll pass to the pipeline:
			//   We'll pass two matrices to our vertex shader via push constants:
			avk::push_constant_binding_data { avk::shader_type::vertex, 0, sizeof(transformation_matrices) },
			avk::descriptor_binding(0, 0, avk::as_combined_image_samplers(mImageSamplers, avk::layout::shader_read_only_optimal)),
			avk::descriptor_binding(0, 1, mViewProjBuffers[0]),
			avk::descriptor_binding(0, 2, mViewProjBuffer), //For view/projection matrices
			avk::descriptor_binding(1, 0, mMaterialBuffer)
		);

		//Pipeline for Screenspace Effects (DoF) 
		//Basically we just need to render a quad with the texture of the result of the previous pipeline and apply the DoF effect
		//In addition we also need to pass the depth buffer to the pipeline from the previous pipeline
		mPipelineSSAO = avk::context().create_graphics_pipeline_for(
			// Specify which shaders the pipeline consists of:
			avk::vertex_shader("shaders/ssao.vert"),
			avk::fragment_shader("shaders/ssao.frag"),
			
			avk::from_buffer_binding(0) -> stream_per_vertex<glm::vec2>() -> to_location(0), // <-- corresponds to vertex shader's inPosition

			// Some further settings:
			avk::cfg::front_face::define_front_faces_to_be_clockwise(),
			avk::cfg::viewport_depth_scissors_config::from_framebuffer(mSSAOFramebuffer.as_reference()),

			// We'll render to the framebuffer
			avk::context().create_renderpass(
				{ colorAttachmentDescriptionSSAO }
			),
			
			// we bind the image (in which we copy the result of the previous pipeline) to the fragment shader
			avk::descriptor_binding(0, 0, mImageSamplerRasterFBColor->as_combined_image_sampler(avk::layout::color_attachment_optimal)),
			avk::descriptor_binding(0, 1, mImageSamplerRasterFBDepth->as_combined_image_sampler(avk::layout::depth_stencil_read_only_optimal)),
			avk::descriptor_binding(0, 2, mImageSamplerRasterFBPosition->as_combined_image_sampler(avk::layout::color_attachment_optimal)),
			avk::descriptor_binding(0, 3, mImageSamplerRasterFBNormals->as_combined_image_sampler(avk::layout::color_attachment_optimal)),
			avk::descriptor_binding(0, 4, mSSAONoiseTexture->as_combined_image_sampler(avk::layout::shader_read_only_optimal)),
			avk::descriptor_binding(0, 5, mSSAOBuffer),
			avk::descriptor_binding(0, 6, mSSAOKernel),
			avk::descriptor_binding(0, 7, mViewProjBuffer)
		);

		mPipelineSSAOBlur = avk::context().create_graphics_pipeline_for(
			avk::vertex_shader("shaders/ssao.vert"),
			avk::fragment_shader("shaders/ssao_blur.frag"),

			avk::from_buffer_binding(0) -> stream_per_vertex<glm::vec2>() -> to_location(0),

			avk::cfg::front_face::define_front_faces_to_be_clockwise(),
			avk::cfg::viewport_depth_scissors_config::from_framebuffer(mSSAOBlurFramebuffer.as_reference()),

			avk::context().create_renderpass(
			{
				colorAttachmentDescriptionSSAOBlur
			}),

			avk::descriptor_binding(0, 0, mImageSamplerSSAOFBColor->as_combined_image_sampler(avk::layout::color_attachment_optimal)),
			avk::descriptor_binding(0, 1, mSSAOBuffer)
		);

		mPipelineIllumination = avk::context().create_graphics_pipeline_for(
			avk::vertex_shader("shaders/ssao.vert"),
			avk::fragment_shader("shaders/illum.frag"),

			avk::from_buffer_binding(0)->stream_per_vertex<glm::vec2>()->to_location(0),

			avk::cfg::front_face::define_front_faces_to_be_clockwise(),
			avk::cfg::viewport_depth_scissors_config::from_framebuffer(mIlluminationFramebuffer.as_reference()),

			avk::context().create_renderpass(
				{ colorAttachmentDescriptionIllumination }
			),

			avk::descriptor_binding(0, 0, mImageSamplerSSAOBlurFBColor->as_combined_image_sampler(avk::layout::color_attachment_optimal)),
			avk::descriptor_binding(0, 1, mImageSamplerRasterFBPositionWS->as_combined_image_sampler(avk::layout::color_attachment_optimal)),
			avk::descriptor_binding(0, 2, mImageSamplerRasterFBNormalsWS->as_combined_image_sampler(avk::layout::color_attachment_optimal)),
			avk::descriptor_binding(0, 3, mImageSamplerRasterFBColor->as_combined_image_sampler(avk::layout::color_attachment_optimal)),
			avk::descriptor_binding(0, 4, mImageSamplerRasterFBDepth->as_combined_image_sampler(avk::layout::depth_stencil_read_only_optimal)),
			avk::descriptor_binding(0, 5, mSSAOBuffer),
			avk::descriptor_binding(0, 6, mCameraData),
			avk::descriptor_binding(0, 7, mLightingData),
			avk::descriptor_binding(0, 8, mLightPositions)
		);



		mPipelineDofNear = avk::context().create_graphics_pipeline_for(
			// Specify which shaders the pipeline consists of:
			avk::vertex_shader("shaders/dof1.vert"),
			avk::fragment_shader("shaders/dof1.frag"),
					
			avk::from_buffer_binding(0) -> stream_per_vertex<glm::vec2>() -> to_location(0), // <-- corresponds to vertex shader's inPosition

			// Some further settings:
			avk::cfg::front_face::define_front_faces_to_be_clockwise(),
			avk::cfg::viewport_depth_scissors_config::from_framebuffer(mDofNearFieldFB.as_reference()),

			// We'll render to the framebuffer
			avk::context().create_renderpass(
			{
				colorAttachmentDescriptionNearDof
			}),
					
			// we bind the image (in which we copy the result of the previous pipeline) to the fragment shader
			avk::descriptor_binding(0, 0, mImageSamplerIlluminationFBColor->as_combined_image_sampler(avk::layout::color_attachment_optimal)),
			avk::descriptor_binding(0, 1, mImageSamplerRasterFBDepth->as_combined_image_sampler(avk::layout::depth_stencil_read_only_optimal)),
			avk::descriptor_binding(0, 2, mDoFBuffer)
		);

		mPipelineDofNearBleed = avk::context().create_graphics_pipeline_for(
			// Specify which shaders the pipeline consists of:
			avk::vertex_shader("shaders/dofBleedNearField.vert"),
			avk::fragment_shader("shaders/dofBleedNearField.frag"),
					
			avk::from_buffer_binding(0) -> stream_per_vertex<glm::vec2>() -> to_location(0), // <-- corresponds to vertex shader's inPosition

			// Some further settings:
			avk::cfg::front_face::define_front_faces_to_be_clockwise(),
			avk::cfg::viewport_depth_scissors_config::from_framebuffer(mDofNearFieldBleedFB.as_reference()),

			// We'll render to the framebuffer
			avk::context().create_renderpass(
			{
			colorAttachmentDescriptionNearBleedDof
			}),

			avk::descriptor_binding(0, 0, mImageSamplerDofNearColor->as_combined_image_sampler(avk::layout::color_attachment_optimal))
		);

		mPipelineDofFar = avk::context().create_graphics_pipeline_for(
			// Specify which shaders the pipeline consists of:
			avk::vertex_shader("shaders/dof2.vert"),
			avk::fragment_shader("shaders/dof2.frag"),
							
			avk::from_buffer_binding(0) -> stream_per_vertex<glm::vec2>() -> to_location(0), // <-- corresponds to vertex shader's inPosition

			// Some further settings:
			avk::cfg::front_face::define_front_faces_to_be_clockwise(),
			avk::cfg::viewport_depth_scissors_config::from_framebuffer(mDofFarFieldFB.as_reference()),

			// We'll render to the framebuffer
			avk::context().create_renderpass(
			{
				colorAttachmentDescriptionFarDof
			}),
							
			// we bind the image (in which we copy the result of the previous pipeline) to the fragment shader
			avk::descriptor_binding(0, 0, mImageSamplerIlluminationFBColor->as_combined_image_sampler(avk::layout::color_attachment_optimal)),
			avk::descriptor_binding(0, 1, mImageSamplerRasterFBDepth->as_combined_image_sampler(avk::layout::depth_stencil_read_only_optimal)),
			avk::descriptor_binding(0, 2, mDoFBuffer)
		);

		mPipelineDofCenter = avk::context().create_graphics_pipeline_for(
			// Specify which shaders the pipeline consists of:
			avk::vertex_shader("shaders/dofCenter.vert"),
			avk::fragment_shader("shaders/dofCenter.frag"),
							
			avk::from_buffer_binding(0) -> stream_per_vertex<glm::vec2>() -> to_location(0), // <-- corresponds to vertex shader's inPosition

			// Some further settings:
			avk::cfg::front_face::define_front_faces_to_be_clockwise(),
			avk::cfg::viewport_depth_scissors_config::from_framebuffer(mDofCenterFieldFB.as_reference()),

			// We'll render to the framebuffer
			avk::context().create_renderpass(
			{
				colorAttachmentDescriptionCenterDof
			}),
							
			// we bind the image (in which we copy the result of the previous pipeline) to the fragment shader
			avk::descriptor_binding(0, 0, mImageSamplerIlluminationFBColor->as_combined_image_sampler(avk::layout::color_attachment_optimal)),
			avk::descriptor_binding(0, 1, mImageSamplerRasterFBDepth->as_combined_image_sampler(avk::layout::depth_stencil_read_only_optimal)),
			avk::descriptor_binding(0, 2, mDoFBuffer)
		);
		
		
		mPipelineDofFinal = avk::context().create_graphics_pipeline_for(
			// Specify which shaders the pipeline consists of:
			avk::vertex_shader("shaders/dof3.vert"),
			avk::fragment_shader("shaders/dof3.frag"),
			
			avk::from_buffer_binding(0) -> stream_per_vertex<glm::vec2>() -> to_location(0), // <-- corresponds to vertex shader's inPosition

			// Some further settings:
			avk::cfg::front_face::define_front_faces_to_be_clockwise(),
			avk::cfg::viewport_depth_scissors_config::from_framebuffer(avk::context().main_window()->backbuffer_reference_at_index(0)),	// Align viewport with main window's resolution

			// We'll render to the back buffer, which has a color attachment always, and in our case additionally a depth
			// attachment, which has been configured when creating the window. See main() function!
			avk::context().create_renderpass({
				avk::attachment::declare(avk::format_from_window_color_buffer(avk::context().main_window()), avk::on_load::clear.from_previous_layout(avk::layout::undefined), avk::usage::color(0), avk::on_store::store),
				avk::attachment::declare(avk::format_from_window_depth_buffer(avk::context().main_window()), avk::on_load::clear.from_previous_layout(avk::layout::undefined), avk::usage::depth_stencil, avk::on_store::dont_care)
			}, avk::context().main_window()->renderpass_reference().subpass_dependencies()),
			
			// we bind the image (in which we copy the result of the previous pipeline) to the fragment shader
			avk::descriptor_binding(0, 0, mImageSamplerIlluminationFBColor->as_combined_image_sampler(avk::layout::color_attachment_optimal)),
			avk::descriptor_binding(0, 1, mImageSamplerDofNearColor->as_combined_image_sampler(avk::layout::color_attachment_optimal)),
			avk::descriptor_binding(0, 2, mImageSamplerDofCenterColor->as_combined_image_sampler(avk::layout::color_attachment_optimal)),
			avk::descriptor_binding(0, 3, mImageSamplerDofFarColor->as_combined_image_sampler(avk::layout::color_attachment_optimal)),
			avk::descriptor_binding(0, 4, mImageSamplerRasterFBDepth->as_combined_image_sampler(avk::layout::depth_stencil_read_only_optimal)),
			avk::descriptor_binding(0, 5, mDoFBuffer),
			avk::descriptor_binding(1, 0, mDoFKernelBufferGaussian),
			avk::descriptor_binding(1, 1, mDoFKernelBufferBokeh)

		);

		

		// set up updater
		// we want to use an updater, so create one:
		mUpdater.emplace();
		mPipelineSSAO.enable_shared_ownership(); // Make it usable with the updater
		mPipelineDofFar.enable_shared_ownership(); // Make it usable with the updater
		mPipelineDofNear.enable_shared_ownership(); // Make it usable with the updater
		mPipelineDofNearBleed.enable_shared_ownership(); // Make it usable with the updater
		mPipelineDofFinal.enable_shared_ownership(); // Make it usable with the updater
		mPipelineSkybox.enable_shared_ownership(); // Make it usable with the updater
		mRasterizePipeline.enable_shared_ownership(); // Make it usable with the updater
		mPipelineSSAOBlur.enable_shared_ownership(); // Make it usable with the updater
		mPipelineIllumination.enable_shared_ownership(); // Make it usable with the updater

		mUpdater->on(avk::swapchain_resized_event(avk::context().main_window())).invoke([this]() {
			this->mQuakeCam.set_aspect_ratio(avk::context().main_window()->aspect_ratio());
			this->mOrbitCam.set_aspect_ratio(avk::context().main_window()->aspect_ratio());
		});

		//first make sure render pass is updated
		mUpdater->on(avk::swapchain_format_changed_event(avk::context().main_window()),
					 avk::swapchain_additional_attachments_changed_event(avk::context().main_window())
		).invoke([this]() {
			// std::vector<avk::attachment> renderpassAttachments = {
			// 	avk::attachment::declare(avk::format_from_window_color_buffer(avk::context().main_window()), avk::on_load::clear.from_previous_layout(avk::layout::undefined), avk::usage::color(0),		avk::on_store::store),	 // But not in presentable format, because ImGui comes after
			// };
			// if (mAdditionalAttachmentsCheckbox->checked())	{
			// 	renderpassAttachments.push_back(avk::attachment::declare(avk::format_from_window_depth_buffer(avk::context().main_window()), avk::on_load::clear.from_previous_layout(avk::layout::undefined), avk::usage::depth_stencil, avk::on_store::dont_care));
			// }
			// auto renderPass = avk::context().create_renderpass(renderpassAttachments, avk::context().main_window()->renderpass_reference().subpass_dependencies());
			// avk::context().replace_render_pass_for_pipeline(mPipelineScreenspace, std::move(renderPass));
			// TODO also for mPipelineSkybox?!?!?
		}).then_on( // ... next, at this point, we are sure that the render pass is correct -> check if there are events that would update the pipeline
			avk::swapchain_changed_event(avk::context().main_window()),
			avk::shader_files_changed_event(mRasterizePipeline.as_reference()),
			avk::shader_files_changed_event(mPipelineSkybox.as_reference()),
			avk::shader_files_changed_event(mPipelineSSAO.as_reference()),
			avk::shader_files_changed_event(mPipelineSSAOBlur.as_reference()),
			avk::shader_files_changed_event(mPipelineIllumination.as_reference()),
			avk::shader_files_changed_event(mPipelineDofFar.as_reference()),
			avk::shader_files_changed_event(mPipelineDofNear.as_reference()),
			avk::shader_files_changed_event(mPipelineDofNearBleed.as_reference()),
			avk::shader_files_changed_event(mPipelineDofCenter.as_reference()),
			avk::shader_files_changed_event(mPipelineDofFinal.as_reference())
		).update(mRasterizePipeline, mPipelineSkybox, mPipelineDofFinal, mPipelineDofNear,mPipelineDofNearBleed, mPipelineDofCenter, mPipelineDofFar, mPipelineSSAO, mPipelineSSAOBlur, mPipelineIllumination);

		
		// Add the cameras to the composition (and let them handle updates)
		mOrbitCam.set_translation({ 0.0f, 0.0f, 0.0f });
		mQuakeCam.set_translation({ 0.0f, 0.0f, 0.0f });
		mOrbitCam.set_perspective_projection(glm::radians(30.0f), avk::context().main_window()->aspect_ratio(), CAM_NEAR, CAM_FAR);
		mQuakeCam.set_perspective_projection(glm::radians(30.0f), avk::context().main_window()->aspect_ratio(), CAM_NEAR, CAM_FAR);
		avk::current_composition()->add_element(mOrbitCam);
		avk::current_composition()->add_element(mQuakeCam);
		mQuakeCam.enable();

		auto imguiManager = avk::current_composition()->element_by_type<avk::imgui_manager>();
		if(nullptr != imguiManager) {
			imguiManager->add_callback([this, imguiManager] {
				bool isEnabled = this->is_enabled();
		        ImGui::Begin("Info & Settings");
				ImGui::SetWindowPos(ImVec2(1.0f, 1.0f), ImGuiCond_FirstUseEver);
				ImGui::Text("%.3f ms/frame", 1000.0f / ImGui::GetIO().Framerate);
				ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);

				ImGui::Separator();
				ImGui::Text("F: play automatic camera path");
				ImGui::Text("R record new camera path");
				if (mCameraPathRecorder.has_value()) {
					ImGui::Text("State: %s", mCameraPathRecorder->is_recording() ? "recording" : "not recording");
				}
				ImGui::Separator();
				bool quakeCamEnabled = mQuakeCam.is_enabled();
				if (ImGui::Checkbox("Enable Quake Camera", &quakeCamEnabled)) {
					if (quakeCamEnabled) { // => should be enabled
						mQuakeCam.set_matrix(mOrbitCam.matrix());
						mQuakeCam.enable();
						mOrbitCam.disable();
					}
				}
				if (quakeCamEnabled) {
				    ImGui::TextColored(ImVec4(0.f, .6f, .8f, 1.f), "[Esc] to exit Quake Camera navigation");
					if (avk::input().key_pressed(avk::key_code::escape)) {
						mOrbitCam.set_matrix(mQuakeCam.matrix());
						mOrbitCam.enable();
						mQuakeCam.disable();
					}
				}
				else {
					ImGui::TextColored(ImVec4(.8f, .4f, .4f, 1.f), "[Esc] to exit application");
				}
				if (imguiManager->begin_wanting_to_occupy_mouse() && mOrbitCam.is_enabled()) {
					mOrbitCam.disable();
				}
				if (imguiManager->end_wanting_to_occupy_mouse() && !mQuakeCam.is_enabled()) {
					mOrbitCam.enable();
				}
				ImGui::Separator();
				ImGui::Text("Depth of Field (F1)");
				mDoFEnabledCheckbox->invokeImGui();
				mDoFModeCombo->invokeImGui();
				mDoFSliderFocus->invokeImGui();
				mDoFSliderFocusRange->invokeImGui();
				mDoFSliderDistanceOutOfFocus->invokeImGui();
				ImGui::Separator();
				ImGui::Text("Screen-Space Ambient Occlusion (SSAO) (F2)");
				mSSAOEnabledCheckbox->invokeImGui();
				mSSAOBlurCheckbox->invokeImGui();
				mIlluminationCheckbox->invokeImGui();
				ImGui::Separator();
				ImGui::Text("Lighting");
				mDayCheckbox->invokeImGui();
				ImGui::Separator();
				
				ImGui::DragFloat3("Scale", glm::value_ptr(mScale), 0.005f, 0.01f, 10.0f);
				ImGui::Checkbox("Enable/Disable invokee", &isEnabled);
				if (isEnabled != this->is_enabled())
				{
					if (!isEnabled) this->disable();
					else this->enable();
				}

				mSrgbFrameBufferCheckbox->invokeImGui();
				mResizableWindowCheckbox->invokeImGui();
				mNumConcurrentFramesSlider->invokeImGui();
				mNumPresentableImagesSlider->invokeImGui();
				mPresentationModeCombo->invokeImGui();

				

				ImGui::End();
			});
		}
	}

	void update_uniform_buffers(avk::window::frame_id_t ifi)
	{
		auto viewProjMat = mQuakeCam.is_enabled()
			                   ? mQuakeCam.projection_and_view_matrix()
			                   : mOrbitCam.projection_and_view_matrix();

		auto viewMatrix = mQuakeCam.is_enabled()
			                  ? mQuakeCam.view_matrix()
			                  : mOrbitCam.view_matrix();

		auto projectionMatrix = mQuakeCam.is_enabled()
			                        ? mQuakeCam.projection_matrix()
			                        : mOrbitCam.projection_matrix();

		// mirror x axis to transform cubemap coordinates to righ-handed coordinates
		auto mirroredViewMatrix = avk::mirror_matrix(viewMatrix, avk::principal_axis::x);

		view_projection_matrices viewProjMat2{
			projectionMatrix,
			viewMatrix,
			glm::inverse(mirroredViewMatrix),
			0.0f
		};

		auto emptyCmd = mViewProjBuffers[ifi]->fill(glm::value_ptr(viewProjMat), 0);
		
		// scale skybox, mirror x axis, cancel out translation
		viewProjMat2.mModelViewMatrix = avk::cancel_translation_from_matrix(mirroredViewMatrix * mModelMatrixSkybox);

		auto emptyToo = mViewProjBufferSkybox->fill(&viewProjMat2, 0);

		// For Raster step
		vp_matrices viewProjMat3{
			viewMatrix,
			projectionMatrix
		};
		auto emptyThree = mViewProjBuffer->fill(&viewProjMat3, 0);

		//DoF
		DoFData dofData;
		dofData.mEnabled = mDoFEnabled;
		dofData.mMode = (mDoFMode == "blur") ? 0 : (mDoFMode == "near") ? 1 : (mDoFMode == "center") ? 2 : 3;
		dofData.mFocus = mDoFFocus;
		dofData.mFocusRange = mDoFFocusRange;
		dofData.mDistOutOfFocus = mDoFDistanceOutOfFocus;
		dofData.mNearPlane = mQuakeCam.near_plane_distance();// we assume both camera have the same near and far plane
		dofData.mFarPlane = mQuakeCam.far_plane_distance();
		auto dofCmd = mDoFBuffer->fill(&dofData, 0);
		
		SSAOData ssaoData;
		ssaoData.mEnabled = mSSAOEnabled;
		ssaoData.mBlur = mSSAOBlur;
		ssaoData.mIllumination = mIllumination;
		auto fence = avk::context().record_and_submit_with_fence({
			mSSAOBuffer->fill(&ssaoData, 0)
		}, *mQueue);
		// Wait on the host until the device is done:
		fence->wait_until_signalled();

		glm::vec3 camTranslation = mQuakeCam.is_enabled() ? mQuakeCam.translation() : mOrbitCam.translation();
		glm::vec4 camPosition = glm::vec4(camTranslation, 1.0);
		CameraData camData;
		camData.position = camPosition;
		auto fence2 = avk::context().record_and_submit_with_fence({
			mCameraData->fill(&camData, 0)
		}, *mQueue);
		// Wait on the host until the device is done:
		fence2->wait_until_signalled();

		LightingData lightData;
		lightData.sunColor = mDay == 1 ? glm::vec3(1.0) : glm::vec3(0.1);
		mLightingData->fill(&lightData, 0);
	}

	void render() override
	{
		auto mainWnd = avk::context().main_window();
		auto ifi = mainWnd->current_in_flight_index();

		mQuakeCam.set_move_speed(1.1f);
		mQuakeCam.set_slow_multiplier(0.5f);
		mQuakeCam.set_fast_multiplier(3.0f);
		mQuakeCam.set_rotation_speed(0.0015f);
		update_uniform_buffers(ifi);
		
		// Get a command pool to allocate command buffers from:
		auto& commandPool = avk::context().get_command_pool_for_single_use_command_buffers(*mQueue);
		
		// Create three command buffer:
		auto cmdBfrs = commandPool->alloc_command_buffers(9u, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

		//Create semaphores 
		auto rasterizerComplete = avk::context().create_semaphore();
		auto ssaoComplete = avk::context().create_semaphore();
		auto ssaoBComplete = avk::context().create_semaphore();

		auto illumComplete = avk::context().create_semaphore();
		auto illumComplete2 = avk::context().create_semaphore();
		auto illumComplete3 = avk::context().create_semaphore();

		auto dofNearComplete = avk::context().create_semaphore();
		auto dofNearBleedComplete = avk::context().create_semaphore();
		auto dofCenterComplete = avk::context().create_semaphore();
		auto dofFarComplete = avk::context().create_semaphore();

		// The swap chain provides us with an "image available semaphore" for the current frame.
		// Only after the swapchain image has become available, we may start rendering into it.
		auto imageAvailable = mainWnd->consume_current_image_available_semaphore();

		
		//First renderpass is the main scene into the rasterizerFramebuffer and creation of the gbuffer
		avk::context().record({
		avk::command::render_pass(mPipelineSkybox->renderpass_reference(), mRasterizerFramebuffer.as_reference(), avk::command::gather(
				avk::command::bind_pipeline(mPipelineSkybox.as_reference()),
				avk::command::bind_descriptors(mPipelineSkybox->layout(), mDescriptorCache->get_or_create_descriptor_sets({
					avk::descriptor_binding(0, 0, mViewProjBufferSkybox),
					avk::descriptor_binding(0, 1, mImageSamplerCubemap->as_combined_image_sampler(avk::layout::general))
				})),
				avk::command::one_for_each(mDrawCallsSkybox, [](const data_for_draw_call& drawCall) {
					return avk::command::draw_indexed(drawCall.mIndexBuffer.as_reference(), drawCall.mPositionsBuffer.as_reference());
				})
			)),
			avk::command::render_pass(mRasterizePipeline->renderpass_reference(), mRasterizerFramebuffer.as_reference(), avk::command::gather(
				avk::command::bind_pipeline(mRasterizePipeline.as_reference()),
				avk::command::bind_descriptors(mRasterizePipeline->layout(), mDescriptorCache->get_or_create_descriptor_sets({
					avk::descriptor_binding(0, 0, avk::as_combined_image_samplers(mImageSamplers, avk::layout::shader_read_only_optimal)),
					avk::descriptor_binding(0, 1, mViewProjBuffers[ifi]),
					avk::descriptor_binding(0, 2, mViewProjBuffer),
					avk::descriptor_binding(1, 0, mMaterialBuffer),
				})),

				// Draw all the draw calls:
				avk::command::custom_commands([&,this](avk::command_buffer_t& cb) { // If there is no avk::command::... struct for a particular command, we can always use avk::command::custom_commands
					for (auto& drawCall : mDrawCalls) {
						cb.record({
							// Set the push constants per draw call:
							avk::command::push_constants(
								mRasterizePipeline->layout(),
								transformation_matrices{
									// Set model matrix for this mesh:
									glm::scale(glm::vec3(0.01f) * mScale),
									// Set material index for this mesh:
									drawCall.mMaterialIndex
								}
							),

							// Make the draw call:
							avk::command::draw_indexed(
								// Bind and use the index buffer:
								drawCall.mIndexBuffer.as_reference(),
								// Bind the vertex input buffers in the right order (corresponding to the layout specifiers in the vertex shader)
								drawCall.mPositionsBuffer.as_reference(), drawCall.mTexCoordsBuffer.as_reference(), drawCall.mNormalsBuffer.as_reference()
							)
						});
					}
				})
			))
		})
		.into_command_buffer(cmdBfrs[0])
		.then_submit_to(*mQueue)
		// Do not start to render before the image has become available:
		.waiting_for(imageAvailable >> avk::stage::color_attachment_output)
		.signaling_upon_completion(avk::stage::color_attachment_output >> rasterizerComplete)
		.submit();


		//2. Render SSAO
		avk::context().record({
			avk::command::render_pass(mPipelineSSAO->renderpass_reference(), mSSAOFramebuffer.as_reference(), avk::command::gather(
				avk::command::bind_pipeline(mPipelineSSAO.as_reference()),
				avk::command::bind_descriptors(mPipelineSSAO->layout(), mDescriptorCache->get_or_create_descriptor_sets({
					avk::descriptor_binding(0, 0, mImageSamplerRasterFBColor->as_combined_image_sampler(avk::layout::attachment_optimal)),
					avk::descriptor_binding(0, 1, mImageSamplerRasterFBDepth->as_combined_image_sampler(avk::layout::attachment_optimal)),
					avk::descriptor_binding(0, 2, mImageSamplerRasterFBPosition->as_combined_image_sampler(avk::layout::attachment_optimal)),
					avk::descriptor_binding(0, 3, mImageSamplerRasterFBNormals->as_combined_image_sampler(avk::layout::attachment_optimal)),
					avk::descriptor_binding(0, 4, mSSAONoiseTexture->as_combined_image_sampler(avk::layout::shader_read_only_optimal)),
					avk::descriptor_binding(0, 5, mSSAOBuffer),
					avk::descriptor_binding(0, 6, mSSAOKernel),
					avk::descriptor_binding(0, 7, mViewProjBuffer)
				})),
				avk::command::draw_indexed(mIndexBufferScreenspace.as_reference(), mVertexBufferScreenspace.as_reference())
			))
		})
		.into_command_buffer(cmdBfrs[1])
		.then_submit_to(*mQueue)
		.waiting_for(rasterizerComplete >> avk::stage::color_attachment_output)
		.signaling_upon_completion(avk::stage::color_attachment_output >> ssaoComplete)
		.submit();
		// Let the command buffer handle the semaphore lifetimes:
		cmdBfrs[1]->handle_lifetime_of(std::move(rasterizerComplete));

		//2.5 Blur SSAO Result
		avk::context().record({
			avk::command::render_pass(mPipelineSSAOBlur->renderpass_reference(), mSSAOBlurFramebuffer.as_reference(), avk::command::gather(
				avk::command::bind_pipeline(mPipelineSSAOBlur.as_reference()),
				avk::command::bind_descriptors(mPipelineSSAOBlur->layout(), mDescriptorCache->get_or_create_descriptor_sets({
					avk::descriptor_binding(0, 0, mImageSamplerSSAOFBColor->as_combined_image_sampler(avk::layout::attachment_optimal)),
					avk::descriptor_binding(0, 1, mSSAOBuffer)
				})),
				avk::command::draw_indexed(mIndexBufferScreenspace.as_reference(), mVertexBufferScreenspace.as_reference())
			))
		})
		.into_command_buffer(cmdBfrs[2])
		.then_submit_to(*mQueue)
		.waiting_for(ssaoComplete >> avk::stage::color_attachment_output)
		.signaling_upon_completion(avk::stage::color_attachment_output >> ssaoBComplete)
		.submit();
		cmdBfrs[2]->handle_lifetime_of(std::move(ssaoComplete));

		//Illuminate the scene
		avk::context().record({
			avk::command::render_pass(mPipelineIllumination->renderpass_reference(), mIlluminationFramebuffer.as_reference(), avk::command::gather(
				avk::command::bind_pipeline(mPipelineIllumination.as_reference()),
				avk::command::bind_descriptors(mPipelineIllumination->layout(), mDescriptorCache->get_or_create_descriptor_sets({
					avk::descriptor_binding(0, 0, mImageSamplerSSAOBlurFBColor->as_combined_image_sampler(avk::layout::attachment_optimal)),
					avk::descriptor_binding(0, 1, mImageSamplerRasterFBPositionWS->as_combined_image_sampler(avk::layout::attachment_optimal)),
					avk::descriptor_binding(0, 2, mImageSamplerRasterFBNormalsWS->as_combined_image_sampler(avk::layout::attachment_optimal)),
					avk::descriptor_binding(0, 3, mImageSamplerRasterFBColor->as_combined_image_sampler(avk::layout::attachment_optimal)),
					avk::descriptor_binding(0, 4, mImageSamplerRasterFBDepth->as_combined_image_sampler(avk::layout::attachment_optimal)),
					avk::descriptor_binding(0, 5, mSSAOBuffer),
					avk::descriptor_binding(0, 6, mCameraData),
					avk::descriptor_binding(0, 7, mLightingData),
					avk::descriptor_binding(0, 8, mLightPositions)
				})),
				avk::command::draw_indexed(mIndexBufferScreenspace.as_reference(), mVertexBufferScreenspace.as_reference())
			))
			})
			.into_command_buffer(cmdBfrs[3])
			.then_submit_to(*mQueue)
			.waiting_for(ssaoBComplete >> avk::stage::color_attachment_output)
			.signaling_upon_completion(avk::stage::color_attachment_output >> illumComplete)
			.signaling_upon_completion(avk::stage::color_attachment_output >> illumComplete2)
			.signaling_upon_completion(avk::stage::color_attachment_output >> illumComplete3)
			.submit();
		cmdBfrs[3]->handle_lifetime_of(std::move(ssaoBComplete));


		// Render Near Field for DoF
		avk::context().record({
			avk::command::render_pass(mPipelineDofNear->renderpass_reference(), mDofNearFieldFB.as_reference(), avk::command::gather(
				avk::command::bind_pipeline(mPipelineDofNear.as_reference()),
				avk::command::bind_descriptors(mPipelineDofNear->layout(), mDescriptorCache->get_or_create_descriptor_sets({
					avk::descriptor_binding(0, 0, mImageSamplerIlluminationFBColor->as_combined_image_sampler(avk::layout::attachment_optimal)),
					avk::descriptor_binding(0, 1, mImageSamplerRasterFBDepth->as_combined_image_sampler(avk::layout::attachment_optimal)),
					avk::descriptor_binding(0, 2, mDoFBuffer)
				})),
				avk::command::draw_indexed(mIndexBufferScreenspace.as_reference(), mVertexBufferScreenspace.as_reference())
			)),
		})
		.into_command_buffer(cmdBfrs[4])
		.then_submit_to(*mQueue)
		.waiting_for(illumComplete >> avk::stage::color_attachment_output)
		.signaling_upon_completion(avk::stage::color_attachment_output >> dofNearComplete)
		.submit();
		// Let the command buffer handle the semaphore lifetimes:
		cmdBfrs[4]->handle_lifetime_of(std::move(illumComplete));

		//Bleed Near Field for DoF
		avk::context().record({
			avk::command::render_pass(mPipelineDofNearBleed->renderpass_reference(), mDofNearFieldBleedFB.as_reference(), avk::command::gather(
				avk::command::bind_pipeline(mPipelineDofNearBleed.as_reference()),
				avk::command::bind_descriptors(mPipelineDofNearBleed->layout(), mDescriptorCache->get_or_create_descriptor_sets({
					avk::descriptor_binding(0, 0, mImageSamplerDofNearColor->as_combined_image_sampler(avk::layout::attachment_optimal)),
				})),
				avk::command::draw_indexed(mIndexBufferScreenspace.as_reference(), mVertexBufferScreenspace.as_reference())
			)),
		})
		.into_command_buffer(cmdBfrs[5])
		.then_submit_to(*mQueue)
		.waiting_for(dofNearComplete >> avk::stage::color_attachment_output)
		.signaling_upon_completion(avk::stage::color_attachment_output >> dofNearBleedComplete)
		.submit();
		// Let the command buffer handle the semaphore lifetimes:
		cmdBfrs[5]->handle_lifetime_of(std::move(dofNearComplete));

		//3. Render Center Field for DoF
		avk::context().record({
			avk::command::render_pass(mPipelineDofCenter->renderpass_reference(), mDofCenterFieldFB.as_reference(), avk::command::gather(
				avk::command::bind_pipeline(mPipelineDofCenter.as_reference()),
				avk::command::bind_descriptors(mPipelineDofCenter->layout(), mDescriptorCache->get_or_create_descriptor_sets({
					avk::descriptor_binding(0, 0, mImageSamplerIlluminationFBColor->as_combined_image_sampler(avk::layout::attachment_optimal)),
					avk::descriptor_binding(0, 1, mImageSamplerRasterFBDepth->as_combined_image_sampler(avk::layout::attachment_optimal)),
					avk::descriptor_binding(0, 2, mDoFBuffer)
				})),
				avk::command::draw_indexed(mIndexBufferScreenspace.as_reference(), mVertexBufferScreenspace.as_reference())
			)),
		})
		.into_command_buffer(cmdBfrs[6])
		.then_submit_to(*mQueue)
		.waiting_for(illumComplete3 >> avk::stage::color_attachment_output)
		.signaling_upon_completion(avk::stage::color_attachment_output >> dofCenterComplete)
		.submit();
		// Let the command buffer handle the semaphore lifetimes:
		cmdBfrs[6]->handle_lifetime_of(std::move(illumComplete3));


		//4. Render Far Field for DoF
		avk::context().record({
			avk::command::render_pass(mPipelineDofFar->renderpass_reference(), mDofFarFieldFB.as_reference(), avk::command::gather(
				avk::command::bind_pipeline(mPipelineDofFar.as_reference()),
				avk::command::bind_descriptors(mPipelineDofFar->layout(), mDescriptorCache->get_or_create_descriptor_sets({
					avk::descriptor_binding(0, 0, mImageSamplerIlluminationFBColor->as_combined_image_sampler(avk::layout::attachment_optimal)),
					avk::descriptor_binding(0, 1, mImageSamplerRasterFBDepth->as_combined_image_sampler(avk::layout::attachment_optimal)),
					avk::descriptor_binding(0, 2, mDoFBuffer)
				})),
				avk::command::draw_indexed(mIndexBufferScreenspace.as_reference(), mVertexBufferScreenspace.as_reference())
			)),
		})
		.into_command_buffer(cmdBfrs[7])
		.then_submit_to(*mQueue)
		.waiting_for(illumComplete2 >> avk::stage::color_attachment_output)
		.signaling_upon_completion(avk::stage::color_attachment_output >> dofFarComplete)
		.submit();
		// Let the command buffer handle the semaphore lifetimes:
		cmdBfrs[7]->handle_lifetime_of(std::move(illumComplete2));
		
		//5. Render Final DoF
		avk::context().record({
			avk::command::render_pass(mPipelineDofFinal->renderpass_reference(), avk::context().main_window()->current_backbuffer_reference(), avk::command::gather(
				avk::command::bind_pipeline(mPipelineDofFinal.as_reference()),
				avk::command::bind_descriptors(mPipelineDofFinal->layout(), mDescriptorCache->get_or_create_descriptor_sets({
					avk::descriptor_binding(0, 0, mImageSamplerIlluminationFBColor->as_combined_image_sampler(avk::layout::attachment_optimal)),
					avk::descriptor_binding(0, 1, mImageSamplerDofNearBleedColor->as_combined_image_sampler(avk::layout::attachment_optimal)),
					avk::descriptor_binding(0, 2, mImageSamplerDofCenterColor->as_combined_image_sampler(avk::layout::attachment_optimal)),
					avk::descriptor_binding(0, 3, mImageSamplerDofFarColor->as_combined_image_sampler(avk::layout::attachment_optimal)),
					avk::descriptor_binding(0, 4, mImageSamplerRasterFBDepth->as_combined_image_sampler(avk::layout::attachment_optimal)),
					avk::descriptor_binding(0, 5, mDoFBuffer),
					avk::descriptor_binding(1, 0, mDoFKernelBufferGaussian),
					avk::descriptor_binding(1, 1, mDoFKernelBufferBokeh)
				})),
				avk::command::draw_indexed(mIndexBufferScreenspace.as_reference(), mVertexBufferScreenspace.as_reference())
			)),
		})
		.into_command_buffer(cmdBfrs[8])
		.then_submit_to(*mQueue)
		.waiting_for(dofFarComplete >> avk::stage::color_attachment_output)
		.waiting_for(dofNearBleedComplete >> avk::stage::color_attachment_output)
		.waiting_for(dofCenterComplete >> avk::stage::color_attachment_output)
		.submit();
		// Let the command buffer handle the semaphore lifetimes:
		cmdBfrs[8]->handle_lifetime_of(std::move(dofFarComplete));
		cmdBfrs[8]->handle_lifetime_of(std::move(dofNearBleedComplete));
		cmdBfrs[8]->handle_lifetime_of(std::move(dofCenterComplete));

		
		// Use a convenience function of avk::window to take care of the command buffers lifetimes:
		// They will get deleted in the future after #concurrent-frames have passed by.
		avk::context().main_window()->handle_lifetime(std::move(cmdBfrs[0]));
		avk::context().main_window()->handle_lifetime(std::move(cmdBfrs[1]));
		avk::context().main_window()->handle_lifetime(std::move(cmdBfrs[2]));
		avk::context().main_window()->handle_lifetime(std::move(cmdBfrs[3]));
		avk::context().main_window()->handle_lifetime(std::move(cmdBfrs[4]));
		avk::context().main_window()->handle_lifetime(std::move(cmdBfrs[5]));
		avk::context().main_window()->handle_lifetime(std::move(cmdBfrs[6]));
		avk::context().main_window()->handle_lifetime(std::move(cmdBfrs[7]));
		avk::context().main_window()->handle_lifetime(std::move(cmdBfrs[8]));

	}

	void toggle_auto_camera_path()
	{
		mQuakeCam.enable();
		if (mCameraPath.has_value()) {
			mCameraPath.reset();
		}
		else {
			mCameraPath.emplace(mQuakeCam, "assets/camera_path.txt");
		}
	}

	void update() override
	{
		static int counter = 0;
		if (++counter == 4) {
			auto current = std::chrono::high_resolution_clock::now();
			auto time_span = current - mInitTime;
			auto int_min = std::chrono::duration_cast<std::chrono::minutes>(time_span).count();
			auto int_sec = std::chrono::duration_cast<std::chrono::seconds>(time_span).count();
			auto fp_ms = std::chrono::duration<double, std::milli>(time_span).count();
			printf("Time from init to fourth frame: %d min, %lld sec %lf ms\n", int_min, int_sec - static_cast<decltype(int_sec)>(int_min) * 60, fp_ms - 1000.0 * int_sec);
			toggle_auto_camera_path();
		}

		if (avk::input().key_pressed(avk::key_code::c)) {
			// Center the cursor:
			auto resolution = avk::context().main_window()->resolution();
			avk::context().main_window()->set_cursor_pos({ resolution[0] / 2.0, resolution[1] / 2.0 });
		}
		if (!mQuakeCam.is_enabled() && avk::input().key_pressed(avk::key_code::escape) || avk::context().main_window()->should_be_closed()) {
			// Stop the current composition:
			avk::current_composition()->stop();
		}
		if (avk::input().key_pressed(avk::key_code::left)) {
			mQuakeCam.look_along(avk::left());
		}
		if (avk::input().key_pressed(avk::key_code::right)) {
			mQuakeCam.look_along(avk::right());
		}
		if (avk::input().key_pressed(avk::key_code::up)) {
			mQuakeCam.look_along(avk::front());
		}
		if (avk::input().key_pressed(avk::key_code::down)) {
			mQuakeCam.look_along(avk::back());
		}
		if (avk::input().key_pressed(avk::key_code::page_up)) {
			mQuakeCam.look_along(avk::up());
		}
		if (avk::input().key_pressed(avk::key_code::page_down)) {
			mQuakeCam.look_along(avk::down());
		}
		if (avk::input().key_pressed(avk::key_code::home)) {
			mQuakeCam.look_at(glm::vec3{0.0f, 0.0f, 0.0f});
		}

		// Automatic camera path recording:
		if (avk::input().key_pressed(avk::key_code::r)) {
			mQuakeCam.enable();
			if (mCameraPathRecorder.has_value()) {
				mCameraPathRecorder->stop_recording();
				mCameraPathRecorder.reset();
			}
			else {
				mCameraPathRecorder.emplace(mQuakeCam);
				mCameraPathRecorder->start_recording();
			}
		}
		if(mCameraPathRecorder.has_value()) {
			mCameraPathRecorder->update();
		}

		// Start following the camera path:
		if(avk::input().key_pressed(avk::key_code::f)) {
			toggle_auto_camera_path();
		}
		if (mCameraPath.has_value()) {
			mCameraPath->update();
		}

		if (avk::input().key_pressed(avk::key_code::f1)) {
			mDoFEnabled = !mDoFEnabled;
		}
		if (avk::input().key_pressed(avk::key_code::f2)) {
			mSSAOEnabled = !mSSAOEnabled;
		}
		

		// Automatic camera path:
		// if (avk::input().key_pressed(avk::key_code::c)) {
		// 	if (avk::input().key_down(avk::key_code::left_shift)) { // => disable
		// 		if (mCameraPath.has_value()) {
		// 			avk::current_composition()->remove_element_immediately(mCameraPath.value());
		// 			mCameraPath.reset();
		// 		}
		// 	}
		// 	else { // => enable
		// 		if (mCameraPath.has_value()) {
		// 			avk::current_composition()->remove_element_immediately(mCameraPath.value());
		// 		}
		// 		mCameraPath.emplace(mQuakeCam);
		// 		avk::current_composition()->add_element(mCameraPath.value());
		// 	}
		// }
	}

private: // v== Member variables ==v

	std::chrono::high_resolution_clock::time_point mInitTime;

	//skybox
	avk::image_sampler mImageSamplerCubemap;
	
	std::vector<data_for_draw_call> mDrawCallsSkybox;
	avk::graphics_pipeline mPipelineSkybox;

	avk::buffer mViewProjBufferSkybox;

	//scene:

	avk::queue* mQueue;
	avk::descriptor_cache mDescriptorCache;

	std::array<avk::buffer, 10> mViewProjBuffers;
	avk::buffer mMaterialBuffer;
	std::vector<avk::image_sampler> mImageSamplers;

	std::vector<data_for_draw_call> mDrawCalls;
	avk::graphics_pipeline mRasterizePipeline;
    glm::vec3 mScale;

	avk::orbit_camera mOrbitCam;
	avk::quake_camera mQuakeCam;
	bool stoppedCamera = false;
	std::optional<camera_path> mCameraPath;
	std::optional<camera_path_recorder> mCameraPathRecorder;

	//1. rasterizer
	avk::framebuffer mRasterizerFramebuffer;//Rasterizer and skybox render into this
	avk::buffer mViewProjBuffer;
	avk::image_sampler mImageSamplerRasterFBColor;
	avk::image_sampler mImageSamplerRasterFBDepth;
	avk::image_sampler mImageSamplerRasterFBPosition;
	avk::image_sampler mImageSamplerRasterFBNormals;
	avk::image_sampler mImageSamplerRasterFBPositionWS;
	avk::image_sampler mImageSamplerRasterFBNormalsWS;

	//2. SSAO 1. pass (create ssao effect)
	avk::graphics_pipeline mPipelineSSAO;//renders into ssaoFramebuffer
	avk::framebuffer mSSAOFramebuffer;//SSAO renders into this
	avk::buffer mSSAOBuffer;
	avk::image_sampler mImageSamplerSSAOFBColor;

	//2.5 SSAO 2. pass (blur result)
	avk::graphics_pipeline mPipelineSSAOBlur;
	avk::framebuffer mSSAOBlurFramebuffer;
	avk::image_sampler mImageSamplerSSAOBlurFBColor;

	//Illumination pass (use ssao output)
	avk::graphics_pipeline mPipelineIllumination;
	avk::framebuffer mIlluminationFramebuffer;
	avk::image_sampler mImageSamplerIlluminationFBColor;
	avk::buffer mCameraData;
	avk::buffer mLightingData;

	//3. DoF 1. pass (renders near field into mDofNearFieldFB)
	avk::graphics_pipeline mPipelineDofNear;//renders into mDofNearFieldFB
	avk::framebuffer mDofNearFieldFB;//Dof1 renders into this
	avk::image_sampler mImageSamplerDofNearColor;

	//3.25 DoF 1.25 pass (bleeds nearField with a max filter)
	avk::graphics_pipeline mPipelineDofNearBleed;//renders into mDofNearFieldFB
	avk::framebuffer mDofNearFieldBleedFB;//Dof1 renders into this
	avk::image_sampler mImageSamplerDofNearBleedColor;

	//3.5 DoF 1.5 pass (renders center field into  mDofCenterFieldFB)
	avk::graphics_pipeline mPipelineDofCenter;//renders into mDofCenterFieldFB
	avk::framebuffer mDofCenterFieldFB;//Dof1 renders into this
	avk::image_sampler mImageSamplerDofCenterColor;

	//4. DoF 2. pass (renders far field into mDofFarFieldFB)
	avk::graphics_pipeline mPipelineDofFar;//renders into mDofFarFieldFB
	avk::framebuffer mDofFarFieldFB;//Dof2 renders into this
	avk::image_sampler mImageSamplerDofFarColor;
	
	//5. DoF 3. pass (renders blurred image into main window) - uses near field, far field, depth buffer
	avk::graphics_pipeline mPipelineDofFinal;//renders directly to the screen
	
	avk::buffer mDoFBuffer;
	avk::buffer mDoFKernelBufferGaussian;//gaussian
	avk::buffer mDoFKernelBufferBokeh;


	//general screenspace quad
	avk::buffer mVertexBufferScreenspace;
	avk::buffer mIndexBufferScreenspace;
	
	// imgui elements
	std::optional<combo_box_container> mPresentationModeCombo;
	std::optional<check_box_container> mSrgbFrameBufferCheckbox;
	std::optional<slider_container<int>> mNumConcurrentFramesSlider;
	std::optional<slider_container<int>> mNumPresentableImagesSlider;
	std::optional<check_box_container> mResizableWindowCheckbox;

	//slider for depth of field (circle of confusion), near and far plane
	std::optional<slider_container<float>> mDoFSliderFocus;
	std::optional<slider_container<float>> mDoFSliderFocusRange;
	std::optional<slider_container<float>> mDoFSliderDistanceOutOfFocus;
	std::optional<check_box_container> mDoFEnabledCheckbox;
	std::optional<combo_box_container> mDoFModeCombo;

	std::optional<check_box_container> mSSAOEnabledCheckbox;
	std::optional<check_box_container> mSSAOBlurCheckbox;
	std::optional<check_box_container> mIlluminationCheckbox;

	std::optional<check_box_container> mDayCheckbox;

	//depth of field data
	float mDoFFocus = 0.8f;
	float mDoFFocusRange = 0.1f;
	float mDoFDistanceOutOfFocus = 0.1f;
	int mDoFEnabled = 1;
	std::string mDoFMode = "blur";

	// SSAO data
	int mSSAOEnabled = 1;
	int mSSAOBlur = 1;
	int mIllumination = 1;

	// Lighting data
	int mDay = 0;


	const float mScaleSkybox = 100.f;
	const glm::mat4 mModelMatrixSkybox = glm::scale(glm::vec3(mScaleSkybox));

}; // model_loader_app



//load settings from settings.ini
void load_start_options()
// 	[window]
// width=1920
// height=1080
//
// [scene]
// model=fullScene.fbx
{
	LPCSTR ini = "./settings.ini";
	startOptions options;
	options.width = GetPrivateProfileIntA("window", "width", 1920, ini);
	options.height = GetPrivateProfileIntA("window", "height", 1080, ini);
	// Buffer for the scene file
	char sceneFileBuffer[256];
	GetPrivateProfileStringA(
		"scene",          // Section name
		"model",          // Key name
		"fullScene.fbx", // Default value
		sceneFileBuffer,  // Buffer to store the retrieved string
		sizeof(sceneFileBuffer), // Buffer size
		ini               // Path to the ini file
	);
	options.sceneFile = sceneFileBuffer; // Assign retrieved string to the structure

	// Debug output to verify the loaded values
	std::cout << "Width: " << options.width << "\n";
	std::cout << "Height: " << options.height << "\n";
	std::cout << "Scene File: " << options.sceneFile << "\n";
	mStartOptions = options;
}

int main() // <== Starting point ==
{
	load_start_options();
	int result = EXIT_FAILURE;
	try {
		// Create a window and open it
		auto mainWnd = avk::context().create_window("4 Seasons");
		mainWnd->set_resolution({ mStartOptions.width, mStartOptions.height });
		mainWnd->enable_resizing(false);
		mainWnd->set_additional_back_buffer_attachments({
			avk::attachment::declare(vk::Format::eD32Sfloat, avk::on_load::clear.from_previous_layout(avk::layout::undefined), avk::usage::depth_stencil, avk::on_store::dont_care)
		});
		mainWnd->set_presentaton_mode(avk::presentation_mode::mailbox);
		mainWnd->set_number_of_concurrent_frames(3u);
		mainWnd->open();

		auto& singleQueue = avk::context().create_queue({}, avk::queue_selection_preference::versatile_queue, mainWnd);
		mainWnd->set_queue_family_ownership(singleQueue.family_index());
		mainWnd->set_present_queue(singleQueue);

		// Create an instance of our main avk::element which contains all the functionality:
		auto app = model_loader_app(singleQueue);
		// Create another element for drawing the UI with ImGui
		auto ui = avk::imgui_manager(singleQueue, "imgui_manager", {}, [](float uiScale) {
			auto& style = ImGui::GetStyle();
			style = ImGuiStyle(); // reset to default style (for non-color settings, like rounded corners)
			ImGui::StyleColorsClassic(); // change color theme
			style.ScaleAllSizes(uiScale); // and scale
		});

		// Compile all the configuration parameters and the invokees into a "composition":
		auto composition = configure_and_compose(
			avk::application_name("4-Seasons Demo"),
			[](avk::validation_layers& config) {
				config.enable_feature(vk::ValidationFeatureEnableEXT::eSynchronizationValidation);
			},
			// Vulkan Device Features 1.2
			[](vk::PhysicalDeviceVulkan12Features& features) {
				features.setSeparateDepthStencilLayouts(VK_TRUE);
			},
			// Pass windows:
			mainWnd,
			// Pass invokees:
			app, ui
		);

		// Create an invoker object, which defines the way how invokees/elements are invoked
		// (In this case, just sequentially in their execution order):
		avk::sequential_invoker invoker;

		// With everything configured, let us start our render loop:
		composition.start_render_loop(
			// Callback in the case of update:
			[&invoker](const std::vector<avk::invokee*>& aToBeInvoked) {
				// Call all the update() callbacks:
				invoker.invoke_updates(aToBeInvoked);
			},
			// Callback in the case of render:
			[&invoker](const std::vector<avk::invokee*>& aToBeInvoked) {
				// Sync (wait for fences and so) per window BEFORE executing render callbacks
				avk::context().execute_for_each_window([](avk::window* wnd) {
					wnd->sync_before_render();
				});

				// Call all the render() callbacks:
				invoker.invoke_renders(aToBeInvoked);

				// Render per window:
				avk::context().execute_for_each_window([](avk::window* wnd) {
					wnd->render_frame();
				});
			}
		); // This is a blocking call, which loops until avk::current_composition()->stop(); has been called (see update())
	
		result = EXIT_SUCCESS;
	}
	catch (avk::logic_error&) {}
	catch (avk::runtime_error&) {}
	return result;
}
