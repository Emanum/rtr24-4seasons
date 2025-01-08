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

	//for skybox
	struct view_projection_matrices {
		glm::mat4 mProjectionMatrix;
		glm::mat4 mModelViewMatrix;
		glm::mat4 mInverseModelViewMatrix;
		float mLodBias = 0.0f;
	};

	//for DoF
	struct DoFData {
		int mEnabled = 0;
		int mMode; //0 = depth, 1 = gaussian, 2 = bokeh
		float mFocus = 3.0f; //at what distance the focus is
		float mFocusRange = 1.5f; //how far the focus reaches (in both directions), i.e. the range of sharpness
		float mDistOutOfFocus = 3.0f; //how much from the start of the out of focus area until the image is completely out of focus
	};

	//for SSAO
	struct SSAOData {
		int mEnabled = 0;
	};

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
		mDoFSliderFocus = slider_container<float>{ "Focus", 0.3f, 0.0f, 1, [this](float val) { this->mDoFFocus = val; } };
		mDoFSliderFocusRange = slider_container<float>{ "Range", 0.01f, 0.0f, 0.1, [this](float val) { this->mDoFFocusRange = val; } };
		mDoFSliderDistanceOutOfFocus = slider_container<float>{ "Dist", 0.05f, 0.0f, 0.2, [this](float val) { this->mDoFDistanceOutOfFocus = val; } };
		mDoFEnabledCheckbox = check_box_container{ "Enabled", true, [this](bool val) { this->mDoFEnabled = val; } };
		mDoFModeCombo = combo_box_container{ "Mode", { "depth", "gaussian", "bokeh" }, 1, [this](std::string val) { this->mDoFMode = val; } };
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
		auto sponza = avk::model_t::load_from_file("assets/Environment.fbx", aiProcess_Triangulate | aiProcess_PreTransformVertices);
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

		// Create a buffer for the transformation matrices in a host coherent memory region (one for each frame in flight):
		for (int i = 0; i < 10; ++i) { // Up to 10 concurrent frames can be configured through the UI.
			mViewProjBuffers[i] = avk::context().create_buffer(
				avk::memory_usage::host_coherent, {},
				avk::uniform_buffer_meta::create_from_data(glm::mat4())
			);
		}
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
		auto colorAttachment = avk::context().create_image_view(avk::context().create_image(r.x, r.y, vk::Format::eR8G8B8A8Unorm, 1, avk::memory_usage::device, avk::image_usage::general_color_attachment));
		auto depthAttachment = avk::context().create_depth_image_view(avk::context().create_depth_image(r.x, r.y, vk::Format::eD32Sfloat, 1, avk::memory_usage::device, avk::image_usage::depth_stencil_attachment));
		auto colorAttachmentDescription = avk::attachment::declare_for(colorAttachment.as_reference(), avk::on_load::load.from_previous_layout(avk::layout::color_attachment_optimal), avk::usage::color(0)     , avk::on_store::store);
		auto depthAttachmentDescription = avk::attachment::declare_for(depthAttachment.as_reference(), avk::on_load::clear.from_previous_layout(avk::layout::depth_stencil_attachment_optimal), avk::usage::depth_stencil, avk::on_store::store);

		mOneFramebuffer = avk::context().create_framebuffer(
			{ colorAttachmentDescription, depthAttachmentDescription }, // Attachment declarations can just be copied => use initializer_list.
			avk::make_vector( colorAttachment, depthAttachment )
		);
		auto sampler = avk::context().create_sampler(avk::filter_mode::trilinear, avk::border_handling_mode::clamp_to_edge, 0);
		mImageSamplerScreenspaceColor = avk::context().create_image_sampler(mOneFramebuffer->image_view_at(0), sampler);
		mImageSamplerScreenspaceDepth = avk::context().create_image_sampler(mOneFramebuffer->image_view_at(1), sampler);
		
		//Create Buffer for DoF effect
		mDoFBuffer = avk::context().create_buffer(
			avk::memory_usage::host_coherent, {},
			avk::uniform_buffer_meta::create_from_data(DoFData())
		);

		mSSAOBuffer = avk::context().create_buffer(
			avk::memory_usage::host_coherent, {},
			avk::uniform_buffer_meta::create_from_data(SSAOData())
		);
		
		//Create Vertex Buffer for Screenspace Quad
		{
			mVertexBufferScreenspace = avk::context().create_buffer(
				avk::memory_usage::device, {},
				// Create the buffer on the device, i.e. in GPU memory, (no additional usage flags).
				// because the screenspace quad is static, we can use device memory
				avk::vertex_buffer_meta::create_from_data(mScreenspaceQuadVertexData)
			);
			// Submit the Vertex Buffer fill command to the device:
			auto fence = avk::context().record_and_submit_with_fence({
				mVertexBufferScreenspace->fill(mScreenspaceQuadVertexData.data(), 0)
			}, *mQueue);
			// Wait on the host until the device is done:
			fence->wait_until_signalled();

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
			avk::cfg::viewport_depth_scissors_config::from_framebuffer(mOneFramebuffer.as_reference()),
			// We'll render to the framebuffer, only color no depth needed cause skybox is always at infinity
			// mColorAttachmentDescription,
			avk::context().create_renderpass(
			{
				colorAttachmentDescription,
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
			avk::vertex_shader("shaders/transform_and_pass_pos_nrm_uv.vert"),
			avk::fragment_shader("shaders/diffuse_shading_fixed_lightsource.frag"),
			// The next 3 lines define the format and location of the vertex shader inputs:
			// (The dummy values (like glm::vec3) tell the pipeline the format of the respective input)
			avk::from_buffer_binding(0) -> stream_per_vertex<glm::vec3>() -> to_location(0), // <-- corresponds to vertex shader's inPosition
			avk::from_buffer_binding(1) -> stream_per_vertex<glm::vec2>() -> to_location(1), // <-- corresponds to vertex shader's inTexCoord
			avk::from_buffer_binding(2) -> stream_per_vertex<glm::vec3>() -> to_location(2), // <-- corresponds to vertex shader's inNormal
			// Some further settings:
			avk::cfg::front_face::define_front_faces_to_be_counter_clockwise(),
			avk::cfg::viewport_depth_scissors_config::from_framebuffer(mOneFramebuffer.as_reference()), // Align viewport with framebuffer resolution
			
			// We'll render to the framebuffer
			avk::context().create_renderpass(
			{
				colorAttachmentDescription,
				depthAttachmentDescription
			}),
				
			
			// The following define additional data which we'll pass to the pipeline:
			//   We'll pass two matrices to our vertex shader via push constants:
			avk::push_constant_binding_data { avk::shader_type::vertex, 0, sizeof(transformation_matrices) },
			avk::descriptor_binding(0, 0, avk::as_combined_image_samplers(mImageSamplers, avk::layout::shader_read_only_optimal)),
			avk::descriptor_binding(0, 1, mViewProjBuffers[0]),
			avk::descriptor_binding(1, 0, mMaterialBuffer)
		);

		//Pipeline for Screenspace Effects (DoF)
		//Basically we just need to render a quad with the texture of the result of the previous pipeline and apply the DoF effect
		//In addition we also need to pass the depth buffer to the pipeline from the previous pipeline
		
		
		mPipelineScreenspace = avk::context().create_graphics_pipeline_for(
			// Specify which shaders the pipeline consists of:
			avk::vertex_shader("shaders/screenspace.vert"),
			avk::fragment_shader("shaders/screenspace.frag"),
			
			avk::from_buffer_binding(0) -> stream_per_vertex<glm::vec2>() -> to_location(0), // <-- corresponds to vertex shader's inPosition

			// Some further settings:
			avk::cfg::front_face::define_front_faces_to_be_clockwise(),
			avk::cfg::viewport_depth_scissors_config::from_framebuffer(avk::context().main_window()->backbuffer_reference_at_index(0)),	// Align viewport with main window's resolution

			// We'll render to the back buffer, which has a color attachment always, and in our case additionally a depth
			// attachment, which has been configured when creating the window. See main() function!
			avk::context().create_renderpass({
				avk::attachment::declare(avk::format_from_window_color_buffer(avk::context().main_window()), avk::on_load::clear.from_previous_layout(avk::layout::undefined), avk::usage::color(0), avk::on_store::store),	 
				// avk::attachment::declare(avk::format_from_window_depth_buffer(avk::context().main_window()), avk::on_load::clear.from_previous_layout(avk::layout::undefined), avk::usage::depth_stencil, avk::on_store::dont_care)
			}, avk::context().main_window()->renderpass_reference().subpass_dependencies()),
			
			// we bind the image (in which we copy the result of the previous pipeline) to the fragment shader
			avk::descriptor_binding(0, 0, mImageSamplerScreenspaceColor->as_combined_image_sampler(avk::layout::color_attachment_optimal)),
			avk::descriptor_binding(0, 1, mImageSamplerScreenspaceDepth->as_combined_image_sampler(avk::layout::depth_stencil_attachment_optimal)),
			avk::descriptor_binding(0, 2, mDoFBuffer),
			avk::descriptor_binding(0, 3, mSSAOBuffer)
		);
			

		// set up updater
		// we want to use an updater, so create one:
		mUpdater.emplace();
		mPipelineScreenspace.enable_shared_ownership(); // Make it usable with the updater
		mPipelineSkybox.enable_shared_ownership(); // Make it usable with the updater
		mRasterizePipeline.enable_shared_ownership(); // Make it usable with the updater

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
			avk::shader_files_changed_event(mPipelineScreenspace.as_reference())
		).update(mRasterizePipeline, mPipelineSkybox, mPipelineScreenspace);
		

		
		// Add the cameras to the composition (and let them handle updates)
		mOrbitCam.set_translation({ 0.0f, 0.0f, 0.0f });
		mQuakeCam.set_translation({ 0.0f, 0.0f, 0.0f });
		mOrbitCam.set_perspective_projection(glm::radians(30.0f), avk::context().main_window()->aspect_ratio(), 0.3f, 1000.0f);
		mQuakeCam.set_perspective_projection(glm::radians(30.0f), avk::context().main_window()->aspect_ratio(), 0.3f, 1000.0f);
		avk::current_composition()->add_element(mOrbitCam);
		avk::current_composition()->add_element(mQuakeCam);
		mQuakeCam.disable();

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
				ImGui::Text("Depth of Field");
				mDoFEnabledCheckbox->invokeImGui();
				mDoFModeCombo->invokeImGui();
				mDoFSliderFocus->invokeImGui();
				mDoFSliderFocusRange->invokeImGui();
				mDoFSliderDistanceOutOfFocus->invokeImGui();
				ImGui::Separator();
				ImGui::Text("Screen-Space Ambient Occlusion (SSAO)");
				ImGui::Checkbox("Enabled", &mSSAOEnabled);
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

		//DoF
		DoFData dofData;
		dofData.mEnabled = mDoFEnabled;
		dofData.mMode = (mDoFMode == "depth") ? 0 : (mDoFMode == "gaussian") ? 1 : 2;
		dofData.mFocus = mDoFFocus;
		dofData.mFocusRange = mDoFFocusRange;
		dofData.mDistOutOfFocus = mDoFDistanceOutOfFocus;
		auto dofCmd = mDoFBuffer->fill(&dofData, 0);

		SSAOData ssaoData;
		ssaoData.mEnabled = static_cast<int>(mSSAOEnabled);
		auto ssaoCmd = mSSAOBuffer->fill(&ssaoData, 0);
	}

	void render() override
	{
		auto mainWnd = avk::context().main_window();
		auto ifi = mainWnd->current_in_flight_index();

		update_uniform_buffers(ifi);
		
		// Get a command pool to allocate command buffers from:
		auto& commandPool = avk::context().get_command_pool_for_single_use_command_buffers(*mQueue);
		
		// Create three command buffer:
		auto cmdBfrs = commandPool->alloc_command_buffers(3u, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

		//Create two semaphores (one between each pipeline)
		auto copyCompleteSemaphore = avk::context().create_semaphore();
		auto rasterizerCompleteSemaphore = avk::context().create_semaphore();
		// The swap chain provides us with an "image available semaphore" for the current frame.
		// Only after the swapchain image has become available, we may start rendering into it.
		auto imageAvailableSemaphore = mainWnd->consume_current_image_available_semaphore();

		//Note there is not really an order in the commands. The order is created by our wait_for and signaling_upon_completion conditions
		//It's more declarative than imperative programming paradigm
		//First command is the skybox -> into Framebuffer
		// avk::context().record({
		// 		avk::command::render_pass(mPipelineSkybox->renderpass_reference(), mOneFramebuffer.as_reference(), avk::command::gather(
		// 			avk::command::bind_pipeline(mPipelineSkybox.as_reference()),
		// 							avk::command::bind_descriptors(mPipelineSkybox->layout(), mDescriptorCache->get_or_create_descriptor_sets({
		// 								avk::descriptor_binding(0, 0, mViewProjBufferSkybox),
		// 								avk::descriptor_binding(0, 1, mImageSamplerCubemap->as_combined_image_sampler(avk::layout::general))
		// 							})),
		// 							avk::command::one_for_each(mDrawCallsSkybox, [](const data_for_draw_call& drawCall) {
		// 								return avk::command::draw_indexed(drawCall.mIndexBuffer.as_reference(), drawCall.mPositionsBuffer.as_reference());
		// 							})
		// 		))})
		// 	.into_command_buffer(cmdBfrs[0])
		// 	.then_submit_to(*mQueue)
		// 	// Do not start to render before the image has become available:
		// 	.waiting_for(imageAvailableSemaphore >> avk::stage::color_attachment_output)
		// 	.signaling_upon_completion(avk::stage::color_attachment_output >> skyboxCompleteSemaphore)
		// 	.submit();

		//Second command is the rasterizer -> into Framebuffer
		avk::context().record({
		avk::command::render_pass(mPipelineSkybox->renderpass_reference(), mOneFramebuffer.as_reference(), avk::command::gather(
				avk::command::bind_pipeline(mPipelineSkybox.as_reference()),
				avk::command::bind_descriptors(mPipelineSkybox->layout(), mDescriptorCache->get_or_create_descriptor_sets({
					avk::descriptor_binding(0, 0, mViewProjBufferSkybox),
					avk::descriptor_binding(0, 1, mImageSamplerCubemap->as_combined_image_sampler(avk::layout::general))
				})),
				avk::command::one_for_each(mDrawCallsSkybox, [](const data_for_draw_call& drawCall) {
					return avk::command::draw_indexed(drawCall.mIndexBuffer.as_reference(), drawCall.mPositionsBuffer.as_reference());
				})
			)),
			avk::command::render_pass(mRasterizePipeline->renderpass_reference(), mOneFramebuffer.as_reference(), avk::command::gather(
				avk::command::bind_pipeline(mRasterizePipeline.as_reference()),
					avk::command::bind_descriptors(mRasterizePipeline->layout(), mDescriptorCache->get_or_create_descriptor_sets({
						avk::descriptor_binding(0, 0, avk::as_combined_image_samplers(mImageSamplers, avk::layout::shader_read_only_optimal)),
						avk::descriptor_binding(0, 1, mViewProjBuffers[ifi]),
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
		// Do not start to render before the skybox has been rendered:
		// .waiting_for(skyboxCompleteSemaphore >> avk::stage::color_attachment_output)
		// Do not start to render before the image has become available: (not sure if we need this because we already waited for the skybox, which waited for the image)
		.waiting_for(imageAvailableSemaphore >> avk::stage::color_attachment_output)
		.signaling_upon_completion(avk::stage::color_attachment_output >> rasterizerCompleteSemaphore)
		.submit();
		
					
		avk::context().record({
				avk::command::render_pass(mPipelineScreenspace->renderpass_reference(), avk::context().main_window()->current_backbuffer_reference(), avk::command::gather(
					avk::command::bind_pipeline(mPipelineScreenspace.as_reference()),
						avk::command::bind_descriptors(mPipelineScreenspace->layout(), mDescriptorCache->get_or_create_descriptor_sets({
							avk::descriptor_binding(0, 0, mImageSamplerScreenspaceColor->as_combined_image_sampler(avk::layout::attachment_optimal)),
							avk::descriptor_binding(0, 1, mImageSamplerScreenspaceDepth->as_combined_image_sampler(avk::layout::attachment_optimal)),
							avk::descriptor_binding(0, 2, mDoFBuffer),
							avk::descriptor_binding(0, 3, mSSAOBuffer)
						})),
						avk::command::draw_indexed(mIndexBufferScreenspace.as_reference(), mVertexBufferScreenspace.as_reference())
				)),
		})
			.into_command_buffer(cmdBfrs[1])
			.then_submit_to(*mQueue)
			// Do not start to render before the copy has been done:
			.waiting_for(rasterizerCompleteSemaphore >> avk::stage::color_attachment_output)
			.submit();
		// Let the command buffer handle the semaphore lifetimes:
		cmdBfrs[1]->handle_lifetime_of(std::move(rasterizerCompleteSemaphore));
		
		// Use a convenience function of avk::window to take care of the command buffers lifetimes:
		// They will get deleted in the future after #concurrent-frames have passed by.
		avk::context().main_window()->handle_lifetime(std::move(cmdBfrs[0]));
		avk::context().main_window()->handle_lifetime(std::move(cmdBfrs[1]));
		// avk::context().main_window()->handle_lifetime(std::move(cmdBfrs[2]));

		// avk::context().main_window()->handle_lifetime(std::move(cmdBfrs[2]));
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
			mQuakeCam.enable();
			if (mCameraPath.has_value()) {
				mCameraPath.reset();
			}
			else {
				mCameraPath.emplace(mQuakeCam, "assets/camera_path.txt");
			}
		}
		if (mCameraPath.has_value()) {
			mCameraPath->update();
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
	std::optional<camera_path> mCameraPath;
	std::optional<camera_path_recorder> mCameraPathRecorder;

	avk::framebuffer mOneFramebuffer;
	avk::graphics_pipeline mPipelineScreenspace;
	avk::buffer mDoFBuffer;
	avk::buffer mSSAOBuffer;
	avk::buffer mVertexBufferScreenspace;
	avk::buffer mIndexBufferScreenspace;
	avk::image_sampler mImageSamplerScreenspaceColor;
	avk::image_sampler mImageSamplerScreenspaceDepth;


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

	//depth of field data
	float mDoFFocus = 0.3f;
	float mDoFFocusRange = 0.1f;
	float mDoFDistanceOutOfFocus = 0.1f;
	int mDoFEnabled = 1;
	std::string mDoFMode = "gaussian";

	// SSAO data
	bool mSSAOEnabled = true;

	const float mScaleSkybox = 100.f;
	const glm::mat4 mModelMatrixSkybox = glm::scale(glm::vec3(mScaleSkybox));

}; // model_loader_app

int main() // <== Starting point ==
{
	int result = EXIT_FAILURE;
	try {
		// Create a window and open it
		auto mainWnd = avk::context().create_window("4 Seasons");

		mainWnd->set_resolution({ 1920, 1080 });
		mainWnd->enable_resizing(false);
		// mainWnd->set_additional_back_buffer_attachments({
		// 	avk::attachment::declare(vk::Format::eD32Sfloat, avk::on_load::clear.from_previous_layout(avk::layout::undefined), avk::usage::depth_stencil, avk::on_store::dont_care)
		// });
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
			avk::application_name("Auto-Vk-Toolkit Example: Model Loader"),
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
