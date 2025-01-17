#include "globals.h"

/* Entry point */
#ifndef entry_point_testing

// STDLib
#include <iostream>

// OPENGL related
#include <glad\glad.h>
#include <GLFW\glfw3.h>
#include <glm\glm.hpp>
#include "GLFWUtil.hpp"

// Engine header files
#include "Shader.hpp"
#include "Texture.hpp"
#include "FrameBuffer.hpp"

// Valve header files
#include "vmf.hpp"

// Util
#include "cxxopts.hpp"
#include "interpolation.h"
#include "vfilesys.hpp"

// Image stuff
#define STBI_MSC_SECURE_CRT
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "dds.hpp"
#include "GradientMap.hpp"

// Experimental
//#define TAR_EXPERIMENTAL

/* Grabs the currently bound framebuffer and saves it to a .png */
void render_to_png(int x, int y, const char* filepath){
	void* data = malloc(4 * x * y);

	glReadPixels(0, 0, x, y, GL_RGBA, GL_UNSIGNED_BYTE, data);

	stbi_flip_vertically_on_write(true);
	stbi_write_png(filepath, x, y, 4, data, x * 4);

	free(data);
}

/* Grabs the currently bound framebuffer and saves it to a .dds */
void save_to_dds(int x, int y, const char* filepath, IMG imgmode = IMG::MODE_DXT1) {
	void* data = malloc(4 * x * y);

	glReadPixels(0, 0, x, y, GL_RGB, GL_UNSIGNED_BYTE, data);

	dds_write((uint8_t*)data, filepath, x, y, imgmode);

	free(data);
}

/* Renders opengl in opaque mode (normal) */
void opengl_render_opaque() {
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
}

/* Renders opengl in addative mode */
void opengl_render_additive() {
	glDepthMask(GL_TRUE);
	glEnable(GL_BLEND);

	// I still do not fully understand OPENGL blend modes. However these equations looks nice for the grid floor.
	glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
	glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
}

/* Command line variables */
#ifndef _DEBUG
std::string m_mapfile_path;
std::string m_game_path;
#endif
#ifdef _DEBUG
std::string m_mapfile_path = "sample_stuff/de_tavr_test";
std::string m_game_path = "D:/SteamLibrary/steamapps/common/Counter-Strike Source/cstrike";
#endif

//derived strings
std::string m_mapfile_name;
std::string m_overviews_folder;
std::string m_resources_folder;

vfilesys* filesys = NULL;

#ifdef _DEBUG
bool m_outputMasks = true;
bool m_onlyOutputMasks;

bool m_comp_shadows_enable = true;
bool m_comp_ao_enable = true;
#endif

#ifndef _DEBUG
bool m_outputMasks;
bool m_onlyOutputMasks;

bool m_comp_shadows_enable;
bool m_comp_ao_enable;
#endif

//tar_config overrides
uint32_t m_renderWidth = 1024;
uint32_t m_renderHeight = 1024;
bool m_enable_maskgen_supersample = true;

bool tar_cfg_enableAO = true;
int tar_cfg_aoSzie = 16;

bool tar_cfg_enableShadows = false;

bool tar_cfg_enableOutline = false;
int tar_cfg_outlineSize = 2;

Texture* tar_cfg_gradientMap;

/* Main program */
int app(int argc, const char** argv) {
#ifndef _DEBUG
	cxxopts::Options options("AutoRadar", "Auto radar");
	options.add_options()
		("v,version", "Shows the software version")
		("g,game", "(REQUIRED) Specify game path", cxxopts::value<std::string>()->default_value(""))
		("m,mapfile", "(REQUIRED) Specify the map file (vmf)", cxxopts::value<std::string>()->default_value(""))

		("d,dumpMasks", "Toggles whether auto radar should output mask images (resources/map_file.resources/)")
		("o,onlyMasks", "Specift whether auto radar should only output mask images and do nothing else (resources/map_file.resources)")

		("ao", "[OBSOLETE] Turn on AO in the compisotor")
		("shadows", "[OBSOLETE] Turn on Shadows in the compositor")

		("w,width", "[OBSOLETE] Render width in pixels (experimental)", cxxopts::value<uint32_t>()->default_value("1024"))
		("h,height", "[OBSOLETE] Render height in pixels (experimental)", cxxopts::value<uint32_t>()->default_value("1024"))

		// Experimental
		("autoModulate", "Enables automatic height modulation between two levels")
		("minHeightDiff", "Minumum height difference(units) to modulate between two levels", cxxopts::value<int>()->default_value("128"))
		
		// Future
		("useVBSP", "Use VBSP.exe to pre-process brush unions automatically")
		("useLightmaps", "Use lightmaps generated by vvis in the VBSP. (If this flag is set, Auto Radar must be ran after vvis.exe)")

		("positional", "Positional parameters", cxxopts::value<std::vector<std::string>>());

	options.parse_positional("positional");
	auto result = options.parse(argc, argv);

	/* Check required parameters */
	if (result.count("game")) m_game_path = sutil::ReplaceAll(result["game"].as<std::string>(), "\n", "");
	else throw cxxopts::option_required_exception("game");
	
	if(result.count("mapfile")) m_mapfile_path = result["mapfile"].as<std::string>();
	else if (result.count("positional")) {
		auto& positional = result["positional"].as<std::vector<std::string>>();
		
		m_mapfile_path = sutil::ReplaceAll(positional[0], "\n", "");
	}
	else throw cxxopts::option_required_exception("mapfile"); // We need a map file

	//Clean paths to what we can deal with
	m_mapfile_path = sutil::ReplaceAll(m_mapfile_path, "\\", "/");
	m_game_path = sutil::ReplaceAll(m_game_path, "\\", "/");

	/* Check the rest of the flags */
	m_onlyOutputMasks = result["onlyMasks"].as<bool>();
	m_outputMasks = result["dumpMasks"].as<bool>() || m_onlyOutputMasks;

	/* Render options */
	m_renderWidth = result["width"].as<uint32_t>();
	m_renderHeight = result["height"].as<uint32_t>();

	m_comp_ao_enable = result["ao"].as<bool>();
	m_comp_shadows_enable = result["shadows"].as<bool>();

#endif

	//Derive the ones
	m_mapfile_name = split(m_mapfile_path, '/').back();
	m_overviews_folder = m_game_path + "/resource/overviews/";
	m_resources_folder = m_overviews_folder + m_mapfile_name + ".resources/";

	/*
	std::cout << "Launching with options:\n";
	std::cout << "  Render width:    " << m_renderWidth << "\n";
	std::cout << "  Render height:   " << m_renderHeight << "\n";
	std::cout << "  Save masks?      " << (m_outputMasks ? "YES" : "NO") << "\n";
	std::cout << "  Output to game?  " << (!m_onlyOutputMasks ? "YES" : "NO") << "\n\n";
	std::cout << "  Game path:       " << m_game_path << "\n";
	std::cout << "  Map path:        " << m_mapfile_path << "\n";
	std::cout << "\n  -------- RENDER SETTINGS -------\n";
	std::cout << "    AO:              " << (m_comp_ao_enable ? "YES" : "NO") << "\n";
	std::cout << "    Shadows:         " << (m_comp_shadows_enable ? "YES" : "NO") << "\n";
	*/

	try {
		filesys = new vfilesys(m_game_path + "/gameinfo.txt");
	}
	catch (std::exception e) {
		std::cout << "Error creating vfilesys:\n";
		std::cout << e.what() << "\n";
		system("PAUSE");
		return 0;
	}

	filesys->debug_info();

	std::cout << "Initializing OpenGL\n";

#pragma region init_opengl

	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); //We are using version 3.3 of openGL
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
	glfwWindowHint(GLFW_VISIBLE, GL_FALSE); // Window le nope
	
	//Create window
	GLFWwindow* window = glfwCreateWindow(1, 1, "If you are seeing this window, something is broken", NULL, NULL);

	//Check if window open
	if (window == NULL){
		std::cout << "Failed to create GLFW window" << std::endl;
		glfwTerminate(); return -1;
	}
	glfwMakeContextCurrent(window);

	//Settingn up glad
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)){
		std::cout << "Failed to initialize GLAD" << std::endl; return -1;
	}

	const unsigned char* glver = glGetString(GL_VERSION);
	std::cout << "(required: min core 3.3.0) opengl version: " << glver << "\n";

	glEnable(GL_DEPTH_TEST);

	glViewport(0, 0, m_renderWidth, m_renderHeight);

	glClearColor(0.00f, 0.00f, 0.00f, 0.00f);

	std::cout << "Creating render buffers\n";

	FrameBuffer fb_tex_playspace = FrameBuffer(m_renderWidth, m_renderHeight);
	FrameBuffer fb_tex_objectives = FrameBuffer(m_renderWidth, m_renderHeight);
	FrameBuffer fb_comp = FrameBuffer(m_renderWidth * 2, m_renderHeight * 2);
	FrameBuffer fb_comp_1 = FrameBuffer(m_renderWidth * 2, m_renderHeight * 2); //Reverse ordered frame buffer
	FrameBuffer fb_final = FrameBuffer(m_renderWidth, m_renderHeight);

	// Screenspace quad
	std::cout << "Creating screenspace mesh\n";

	std::vector<float> __meshData = {
		-1, -1,
		-1, 1,
		1, -1,
		-1, 1,
		1, 1,
		1, -1
	};

	Mesh* mesh_screen_quad = new Mesh(__meshData, MeshMode::SCREEN_SPACE_UV);

#pragma endregion

#pragma region shader_compilation

	std::cout << "Compiling Shaders\n";
	std::cout << "______________________________________________________________\n\n";

	// Internal engine shaders
	Shader shader_depth("shaders/depth.vs", "shaders/depth.fs");
	Shader shader_unlit("shaders/unlit.vs", "shaders/unlit.fs");

	// Compositing shaders
	Shader shader_comp_main("shaders/fullscreenbase.vs", "shaders/ss_comp_main.fs"); // le big one
	Shader shader_precomp_playspace("shaders/fullscreenbase.vs", "shaders/ss_precomp_playspace.fs"); // computes distance map
	Shader shader_precomp_objectives("shaders/fullscreenbase.vs", "shaders/ss_precomp_objectives.fs"); // computes distance map

	if (shader_depth.compileUnsuccessful || 
		shader_unlit.compileUnsuccessful || 
		shader_comp_main.compileUnsuccessful || 
		shader_precomp_playspace.compileUnsuccessful || 
		shader_precomp_objectives.compileUnsuccessful) {

		std::cout << "______________________________________________________________\n";
		std::cout << "Shader compilation step failed.\n";
		glfwTerminate();
#ifdef _DEBUG
		system("PAUSE");
#endif
		return 1;
	}

	std::cout << "______________________________________________________________\n";
	std::cout << "Shader compilation successful\n\n";

	std::cout << "Loading textures... ";

	Texture tex_background = Texture("textures/grid.png");
	//Texture tex_gradient = Texture("textures/gradients/gradientmap_6.png", true);
	Texture tex_height_modulate = Texture("textures/modulate.png");
	
	//GradientTexture gtex_gradient = GradientTexture(std::string("32 68 136 255"), std::string("149 0 0 255"), std::string("178 113 65"));

	std::cout << "done!\n\n";

#pragma endregion

#pragma region map_load

	std::cout << "Loading map file...\n";

	vmf::vmf vmf_main(m_mapfile_path + ".vmf");
	//vmf_main.setup_main();
	//vmf_main.genVMFReferences(); // Load all our func_instances

	//std::cout << "Generating Meshes...\n";

	vmf_main.ComputeGLMeshes();
	vmf_main.ComputeDisplacements();

	// TAR entities
	std::vector<vmf::Entity*> tavr_ent_tar_config = vmf_main.findEntitiesByClassName("tar_config");

	if (tavr_ent_tar_config.size() > 1) {
		std::cout << "More than 1 tar config found! Currently unsupported... Using last.\n";
	}

	vmf::Entity* tar_config = NULL;
	if (tavr_ent_tar_config.size() > 0) {
		tar_config = tavr_ent_tar_config.back();

		// Color scheme
		std::string schemeNum = kv::tryGetStringValue(tar_config->keyValues, "colorScheme", "0");
		if (schemeNum == "-1") { // Custom color scheme
			tar_cfg_gradientMap = new GradientTexture(
				kv::tryGetStringValue(tar_config->keyValues, "customCol0", "0   0   0   255"),
				kv::tryGetStringValue(tar_config->keyValues, "customCol1", "128 128 128 255"),
				kv::tryGetStringValue(tar_config->keyValues, "customCol2", "255 255 255 255"));
		} else {
			tar_cfg_gradientMap = new Texture("textures/gradients/gradientmap_" + schemeNum + ".png", true);
		}

		// Ambient occlusion
		tar_cfg_enableAO = (kv::tryGetStringValue(tar_config->keyValues, "enableAO", "1") == "1");
		tar_cfg_aoSzie = kv::tryGetValue(tar_config->keyValues, "aoSize", 16);

		// Outline
		tar_cfg_enableOutline = (kv::tryGetStringValue(tar_config->keyValues, "enableOutline", "0") == "1");
		tar_cfg_outlineSize = kv::tryGetValue(tar_config->keyValues, "outlineWidth", 2);

		// Shadows
		tar_cfg_enableShadows = (kv::tryGetStringValue(tar_config->keyValues, "enableShadows", "0") == "1");
	}
	else {
		tar_cfg_gradientMap = new Texture("textures/gradients/gradientmap_6.png", true);
	}

	std::cout << "Collecting Objects... \n";
	std::vector<vmf::Solid*> tavr_solids = vmf_main.getAllBrushesInVisGroup(tar_config == NULL? "tar_layout" : kv::tryGetStringValue(tar_config->keyValues, "vgroup_layout", "tar_layout"));
	std::vector<vmf::Solid*> tavr_solids_negative = vmf_main.getAllBrushesInVisGroup(tar_config == NULL? "tar_mask" : kv::tryGetStringValue(tar_config->keyValues, "vgroup_negative", "tar_mask"));
	std::vector<vmf::Solid*> tavr_entire_brushlist = vmf_main.getAllRenderBrushes();
	std::vector<vmf::Solid*> tavr_cover = vmf_main.getAllBrushesInVisGroup(tar_config == NULL ? "tar_cover" : kv::tryGetStringValue(tar_config->keyValues, "vgroup_cover", "tar_cover"));
	for (auto && v : tavr_cover) { v->temp_mark = true; tavr_solids.push_back(v); }

	//std::vector<vmf::Solid*> tavr_solids_funcbrush = vmf_main.getAllBrushesByClassName("func_brush");
	std::vector<vmf::Solid*> tavr_buyzones = vmf_main.getAllBrushesByClassName("func_buyzone");
	std::vector<vmf::Solid*> tavr_bombtargets = vmf_main.getAllBrushesByClassName("func_bomb_target");

	std::vector<vmf::Entity*> tavr_ent_tavr_height_min = vmf_main.findEntitiesByClassName("tar_min");
	std::vector<vmf::Entity*> tavr_ent_tavr_height_max = vmf_main.findEntitiesByClassName("tar_max");

	//Collect models
	std::cout << "Collecting models... \n";
	vmf_main.populateModelDict(filesys);
	vmf_main.populatePropList(tar_config == NULL ? "tar_cover" : kv::tryGetStringValue(tar_config->keyValues, "vgroup_cover", "tar_cover"));

	std::cout << "done!\n";

#pragma region bounds
	std::cout << "Calculating bounds... ";

	vmf::BoundingBox limits = vmf::getSolidListBounds(tavr_solids);
	float z_render_min = limits.SEL.y;
	float z_render_max = limits.NWU.y;

	// Overide entity heights
	if (tavr_ent_tavr_height_min.size()) z_render_min = tavr_ent_tavr_height_min[0]->origin.z;
	if (tavr_ent_tavr_height_max.size()) z_render_max = tavr_ent_tavr_height_max[0]->origin.z;

	float padding = 128.0f;

	float x_bounds_min = -limits.NWU.x - padding; //inflate distances slightly
	float x_bounds_max = -limits.SEL.x + padding;

	float y_bounds_min = limits.SEL.z - padding;
	float y_bounds_max = limits.NWU.z + padding;

	float dist_x = x_bounds_max - x_bounds_min;
	float dist_y = y_bounds_max - y_bounds_min;

	float mx_dist = glm::max(dist_x, dist_y);

	float justify_x = (mx_dist - dist_x) * 0.5f;
	float justify_y = (mx_dist - dist_y) * 0.5f;

	float render_ortho_scale = glm::round((mx_dist / 1024.0f) / 0.01f) * 0.01f * 1024.0f; // Take largest, scale up a tiny bit. Clamp to 1024 min. Do some rounding.
	glm::vec2 view_origin = glm::vec2(x_bounds_min - justify_x, y_bounds_max + justify_y);

	std::cout << "done\n\n";
#pragma endregion 

#pragma endregion

#pragma region OpenGLRender

	std::cout << "Starting OpenGL Render\n";

#pragma region render_playable_space
	std::cout << "Rendering playable space... ";

	// ======================================================== REGULAR ORDER ========================================================

	glViewport(0, 0, m_renderWidth * 2, m_renderHeight * 2);

	fb_comp.Bind(); //Bind framebuffer

	glClearColor(0.00f, 0.00f, 0.00f, 1.00f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glPolygonMode(GL_FRONT, GL_FILL);
	
	shader_depth.use();
	shader_depth.setMatrix("projection", glm::ortho(view_origin.x, view_origin.x + render_ortho_scale , view_origin.y - render_ortho_scale, view_origin.y, -1024.0f, 1024.0f));
	shader_depth.setMatrix("view", glm::lookAt(glm::vec3(0, 0, 0), glm::vec3(0, -1.0f, 0), glm::vec3(0, 0, 1)));
		
	glm::mat4 model = glm::mat4();
	shader_depth.setMatrix("model", model);

	shader_depth.setFloat("HEIGHT_MIN", z_render_min);
	shader_depth.setFloat("HEIGHT_MAX", z_render_max);
	shader_depth.setFloat("write_playable", 0.0f);

	// Render entire map first
	for (auto && brush : tavr_entire_brushlist) {
		shader_depth.setFloat("write_cover", brush->temp_mark ? 1.0f : 0.0f);
		brush->mesh->Draw();
	}
	glClear(GL_DEPTH_BUFFER_BIT);

	// Render playable area over it
	shader_depth.setFloat("write_playable", 1.0f);
	for (auto && s_solid : tavr_solids) {
		shader_depth.setFloat("write_cover", s_solid->temp_mark ? 1.0f : 0.0f);
		if (!s_solid->containsDisplacements)
			s_solid->mesh->Draw();
		else {
			for (auto && f : s_solid->faces) {
				if (f.displacement != NULL) {
					f.displacement->glMesh->Draw();
				}
			}
		}
	}

#ifdef TAR_EXPERIMENTAL
	// Render instances (experimental)
	for (auto && sub_vmf : vmf_main.findEntitiesByClassName("func_instance")) {
		std::string mapname = kv::tryGetStringValue(sub_vmf->keyValues, "file", "");

		if (mapname == "") continue; //Something went wrong...

		model = glm::mat4();

		// do transforms
		model = glm::translate(model, glm::vec3(-sub_vmf->origin.x, sub_vmf->origin.z, sub_vmf->origin.y));

		// upload
		shader_depth.setMatrix("model", model);

		for (auto && solid : vmf_main.subvmf_references[mapname]->getAllBrushesInVisGroup("tar_cover")) {
			shader_depth.setFloat("write_cover", solid->temp_mark ? 1.0f : 1.0f);
			if (!solid->containsDisplacements)
				solid->mesh->Draw();
			else {
				for (auto && f : solid->faces) {
					if (f.displacement != NULL) {
						f.displacement->glMesh->Draw();
					}
				}
			}
		}
	}
#endif // TAR_EXPERIMENTAL

	// Render props
	std::cout << "Rendering props\n";
	shader_depth.setFloat("write_cover", 1.0f);
	for (auto && s_prop : vmf_main.props) {
		if (vmf_main.modelCache[s_prop.modelID] == NULL) continue; // Skip uncanched / errored models. This shouldn't happen if the vmf references everything normally and all files exist.

		model = glm::mat4();
		model = glm::translate(model, s_prop.origin); // Position
		model = glm::rotate(model, glm::radians(s_prop.rotation.y), glm::vec3(0, 1, 0)); // Yaw 
		model = glm::rotate(model, glm::radians(s_prop.rotation.x), glm::vec3(0, 0, 1)); // ROOOOOLLLLL
		model = glm::rotate(model, -glm::radians(s_prop.rotation.z), glm::vec3(1, 0, 0)); // Pitch 
		model = glm::scale(model, glm::vec3(s_prop.unifromScale)); // Scale

		shader_depth.setMatrix("model", model);
		vmf_main.modelCache[s_prop.modelID]->Draw();
	}

	model = glm::mat4();
	shader_depth.setMatrix("model", model);

	// Re render subtractive brushes
	shader_depth.setFloat("write_playable", 0.0f);
	for (auto && s_solid : tavr_solids_negative) {
		shader_depth.setFloat("write_cover", s_solid->temp_mark ? 1.0f : 0.0f);
		if (!s_solid->containsDisplacements)
			s_solid->mesh->Draw();
		else {
			for (auto && f : s_solid->faces) {
				if (f.displacement != NULL) {
					f.displacement->glMesh->Draw();
				}
			}
		}
	}

	// ======================================================== REVERSE ORDER ========================================================

	fb_comp_1.Bind();

	// Reverse rendering
	glClearDepth(0);
	glEnable(GL_CULL_FACE);
	glDepthFunc(GL_GREATER);

	glClearColor(0.00f, 0.00f, 0.00f, 1.00f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glPolygonMode(GL_FRONT, GL_FILL);

	shader_depth.setFloat("HEIGHT_MIN", z_render_min);
	shader_depth.setFloat("HEIGHT_MAX", z_render_max);
	shader_depth.setFloat("write_playable", 0.0f);

	for (auto && s_solid : tavr_solids) {
		if (!s_solid->containsDisplacements)
			s_solid->mesh->Draw();
		else {
			for (auto && f : s_solid->faces) {
				if (f.displacement != NULL) {
					f.displacement->glMesh->Draw();
				}
			}
		}
	}

	// regular depth testing
	glClearDepth(1);
	glDepthFunc(GL_LESS);
	glDisable(GL_CULL_FACE);

	// ========================================================== PRE-COMP ===========================================================

	glViewport(0, 0, m_renderWidth, m_renderHeight);

	// Apply diffusion
	fb_tex_playspace.Bind();

	glClearColor(0.00f, 0.00f, 0.00f, 0.00f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);
	glPolygonMode(GL_FRONT, GL_FILL);

	shader_precomp_playspace.use();

	//shader_precomp_playspace.setFloat("HEIGHT_MIN", z_render_min);
	//shader_precomp_playspace.setFloat("HEIGHT_MAX", z_render_max);

	fb_comp.BindRTtoTexSlot(0);
	shader_precomp_playspace.setInt("tex_in", 0);

	fb_comp_1.BindRTtoTexSlot(1);
	shader_precomp_playspace.setInt("tex_in_1", 1);

	//tex_height_modulate.bindOnSlot(2);
	//shader_precomp_playspace.setInt("tex_modulate", 2);

	mesh_screen_quad->Draw();

	glEnable(GL_DEPTH_TEST);

	if(m_outputMasks) render_to_png(m_renderWidth, m_renderHeight, filesys->create_output_filepath("resource/overviews/" + m_mapfile_name + ".resources/playspace.png", true).c_str());

	std::cout << "done!\n";
#pragma endregion 

#pragma region render_objectives
	std::cout << "Rendering bombsites & buyzones space... ";

	glViewport(0, 0, m_renderWidth * 2, m_renderHeight * 2);

	fb_comp.Bind();

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glPolygonMode(GL_FRONT, GL_FILL);
	//glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	shader_unlit.use();
	shader_unlit.setMatrix("projection", glm::ortho(view_origin.x, view_origin.x + render_ortho_scale, view_origin.y - render_ortho_scale, view_origin.y, -1024.0f, 1024.0f));
	shader_unlit.setMatrix("view", glm::lookAt(glm::vec3(0, 0, 0), glm::vec3(0, -1.0f, 0), glm::vec3(0, 0, 1)));
	shader_unlit.setMatrix("model", model);

	shader_unlit.setVec3("color", 0.0f, 1.0f, 0.0f);

	for (auto && s_solid : tavr_buyzones) {
		s_solid->mesh->Draw();
	}

	fb_comp_1.Bind();
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	shader_unlit.setVec3("color", 1.0f, 0.0f, 0.0f);

	for (auto && s_solid : tavr_bombtargets) {
		s_solid->mesh->Draw();
	}

	// Apply diffusion
	glViewport(0, 0, m_renderWidth, m_renderHeight);

	fb_tex_objectives.Bind();

	glClearColor(0.00f, 0.00f, 0.00f, 0.00f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);
	glPolygonMode(GL_FRONT, GL_FILL);

	shader_precomp_objectives.use();

	fb_comp.BindRTtoTexSlot(0);
	shader_precomp_objectives.setInt("tex_in", 0);

	fb_comp_1.BindRTtoTexSlot(1);
	shader_precomp_objectives.setInt("tex_in_1", 1);

	fb_tex_playspace.BindRTtoTexSlot(2);
	shader_precomp_objectives.setInt("tex_in_2", 2);

	mesh_screen_quad->Draw();

	if (m_outputMasks) render_to_png(m_renderWidth, m_renderHeight, filesys->create_output_filepath("resource/overviews/" + m_mapfile_name + ".resources/buyzones_bombtargets.png", true).c_str());

	glEnable(GL_DEPTH_TEST);
	std::cout << "done!\n";
#pragma endregion 

#pragma region compositing
	std::cout << "Compositing... \n";

	fb_final.Bind();

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glPolygonMode(GL_FRONT, GL_FILL);

	shader_comp_main.use();

	/* Fill out shader uniforms */
	/*
		vec3 bounds_NWU     	North-West-Upper coordinate of the playspace (worldspace)
		vec3 bounds_SEL 		South-East-Lower coordinate of the playspace (worldspace)
		**vec2 bounds_NWU_SS 	North-West coordinate of the playspace (screen space)
		**vec2 bounds_SEL_SS 	South-East coordinate of the playspace (screen space)

		**vec2 pos_spawn_ct 	Location of the CT Spawn	(0-1)
		**vec2 pos_spawn_t 		Location of the T Spawn	(0-1)
		**vec2 bombsite_a 		Location of bomsite A	(0-1)
		**vec2 bombsite_b  		Location of bombsite B	(0-1)
	*/
	shader_comp_main.setVec3("bounds_NWU", glm::vec3(x_bounds_min, y_bounds_max, z_render_max));
	shader_comp_main.setVec3("bounds_SEL", glm::vec3(x_bounds_max, y_bounds_min, z_render_min));

	/* Render flags */
	shader_comp_main.setInt("cmdl_shadows_enable", tar_cfg_enableShadows ? 1 : 0);
	shader_comp_main.setInt("cmdl_ao_enable", tar_cfg_enableAO ? 1 : 0);
	shader_comp_main.setInt("cmdl_ao_size", tar_cfg_aoSzie);
	shader_comp_main.setInt("cmdl_outline_enable", tar_cfg_enableOutline);
	shader_comp_main.setInt("cmdl_outline_size", tar_cfg_outlineSize);

	shader_comp_main.setVec4("outline_color", parseVec4(kv::tryGetStringValue(tar_config->keyValues, "zColOutline", "255 255 255 255")));
	shader_comp_main.setVec4("ao_color", parseVec4(kv::tryGetStringValue(tar_config->keyValues, "zColAO", "255 255 255 255")));

	shader_comp_main.setVec4("buyzone_color", parseVec4(kv::tryGetStringValue(tar_config->keyValues, "zColBuyzone", "255 255 255 255")));
	shader_comp_main.setVec4("objective_color", parseVec4(kv::tryGetStringValue(tar_config->keyValues, "zColObjective", "255 255 255 255")));
	shader_comp_main.setVec4("cover_color", parseVec4(kv::tryGetStringValue(tar_config->keyValues, "zColCover", "255 255 255 255")));

	/* Bind texture samplers */
	tex_background.bindOnSlot(0);
	shader_comp_main.setInt("tex_background", 0);

	fb_tex_playspace.BindRTtoTexSlot(1);
	shader_comp_main.setInt("tex_playspace", 1);

	fb_tex_objectives.BindRTtoTexSlot(2);
	shader_comp_main.setInt("tex_objectives", 2);

	tar_cfg_gradientMap->bindOnSlot(4);
	shader_comp_main.setInt("tex_gradient", 4);

	mesh_screen_quad->Draw();

	std::cout << "done!\n";

#pragma endregion 

#pragma endregion

#pragma region auto_export_game

	if (!m_onlyOutputMasks) save_to_dds(m_renderWidth, m_renderHeight, filesys->create_output_filepath("resource/overviews/" + m_mapfile_name + "_radar.dds", true).c_str(), IMG::MODE_DXT1);
	if (m_outputMasks) render_to_png(m_renderWidth, m_renderHeight, filesys->create_output_filepath("resource/overviews/" + m_mapfile_name + ".resources/raw.png", true).c_str());

#pragma region generate_radar_txt

	std::cout << "Generating radar .TXT... ";

	kv::DataBlock node_radar = kv::DataBlock();
	node_radar.name = m_mapfile_name;
	node_radar.Values.insert({ "material", "overviews/" + m_mapfile_name });

	node_radar.Values.insert({ "pos_x", std::to_string(view_origin.x) });
	node_radar.Values.insert({ "pos_y", std::to_string(view_origin.y) });
	node_radar.Values.insert({ "scale", std::to_string(render_ortho_scale / 1024.0f) });

	// Try resolve spawn positions
	//glm::vec3* loc_spawnCT = vmf_main.calculateSpawnLocation(vmf::team::counter_terrorist);
	//glm::vec3* loc_spawnT = vmf_main.calculateSpawnLocation(vmf::team::terrorist);

	//if (loc_spawnCT != NULL) {
	//	node_radar.Values.insert({ "CTSpawn_x", std::to_string(util::roundf(remap(loc_spawnCT->x, view_origin.x, view_origin.x + render_ortho_scale, 0.0f, 1.0f), 0.01f)) });
	//	node_radar.Values.insert({ "CTSpawn_y", std::to_string(util::roundf(remap(loc_spawnCT->y, view_origin.y, view_origin.y - render_ortho_scale, 0.0f, 1.0f), 0.01f)) });
	//}
	//if (loc_spawnT != NULL) {
	//	node_radar.Values.insert({ "TSpawn_x", std::to_string(util::roundf(remap(loc_spawnT->x, view_origin.x, view_origin.x + render_ortho_scale, 0.0f, 1.0f), 0.01f)) });
	//	node_radar.Values.insert({ "TSpawn_y", std::to_string(util::roundf(remap(loc_spawnT->y, view_origin.y, view_origin.y - render_ortho_scale, 0.0f, 1.0f), 0.01f)) });
	//}

	std::ofstream out(filesys->create_output_filepath("resource/overviews/" + m_mapfile_name + ".txt", true).c_str());
	out << "// TAVR - AUTO RADAR. v 2.0.0\n";
	node_radar.Serialize(out);
	out.close();

	std::cout << "done!";

#pragma endregion 
#pragma endregion

	std::cout << "\n- Radar generation successful... cleaning up. -\n";

	//Exit safely
	glfwTerminate();
#ifdef _DEBUG
	system("PAUSE");
#endif

	return 0;
}


int main(int argc, const char** argv) {
	try {
		return app(argc, argv);
	}
	catch (cxxopts::OptionException& e) {
		std::cerr << "Parse error: " << e.what() << "\n";
	}

	return 1;
}


/* 

NVIDIA optimus systems will default to intel integrated graphics chips.
These chips cause fatal memory issues when using opengl3.3 (even though they claim to generate correct contexts)

This export gets picked up by NVIDIA drivers (v 302+).
It will force usage of the dedicated video device in the machine, which likely has full coverage of 3.3

*/
extern "C" {
	_declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
}

#endif