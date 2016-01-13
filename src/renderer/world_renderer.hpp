#pragma once
#include <mutex>

#include "../hex.hpp"
#include "../fwd.hpp"
#include "../ticks_counter.hpp"
#include "i_world_renderer.hpp"

#include "texture.hpp"
#include "framebuffer.hpp"
#include "polygon_buffer.hpp"
#include "glsl_program.hpp"
#include "text.hpp"
#include "texture_manager.hpp"
#include "weather_effects_particle_manager.hpp"

#include "matrix.hpp"

class r_WorldRenderer final : public r_IWorldRenderer
{
public:
	r_WorldRenderer( const h_SettingsPtr& settings, const h_WorldConstPtr& world, const h_PlayerConstPtr& player );
	virtual ~r_WorldRenderer() override;

public: // r_IWorldRenderer
	virtual void Update() override;
	virtual void UpdateChunk( unsigned short X, unsigned short Y, bool immediately ) override;
	virtual void UpdateChunkWater( unsigned short X, unsigned short Y, bool immediately ) override;
	virtual void UpdateWorldPosition( int longitude, int latitude ) override;

public:
	void Draw();

	void Init();
	void InitGL();

	void SetViewportSize( unsigned int viewport_width, unsigned int viewport_height );

private:
	r_WorldRenderer& operator=( const r_WorldRenderer& other ) = delete;

	void LoadShaders();
	void LoadTextures();
	void InitFrameBuffers();
	void InitVertexBuffers();

	// Recalc pointers to parent h_Chunk and his neighbors.
	void UpdateChunkMatrixPointers();
	void MoveChunkMatrix( int longitude, int latitude );
	// Coordinates - in chunks matrix.
	bool NeedRebuildChunkInThisTick( unsigned int x, unsigned int y );

	void UpdateGPUData();

	void CalculateMatrices();
	void CalculateLight();

	void DrawBuildPrism();
	void DrawTestMob();
	void DrawCrosshair();

	// helper. returns vertex count
	void CalculateChunksVisibility();
	unsigned int DrawClusterMatrix( r_WVB* wvb, unsigned int triangles_per_primitive, unsigned int vertices_per_primitive );
	void DrawWorld();
	void DrawWater();

	void DrawSky();
	void DrawStars();
	void DrawSun();
	void DrawConsole();

private:
	const h_SettingsPtr settings_;
	const h_WorldConstPtr world_;
	const h_PlayerConstPtr player_;

	// Counters
	h_TicksCounter frames_counter_;
	h_TicksCounter chunks_updates_counter_;
	h_TicksCounter chunks_water_updates_counter_;
	h_TicksCounter updates_counter_;
	unsigned int world_quads_in_frame_;
	unsigned int water_hexagons_in_frame_;
	unsigned int chunks_visible_;

	// Shaders
	r_GLSLProgram world_shader_;
	r_GLSLProgram build_prism_shader_;
	r_GLSLProgram water_shader_;
	r_GLSLProgram skybox_shader_;
	r_GLSLProgram stars_shader_;
	r_GLSLProgram sun_shader_;
	r_GLSLProgram console_bg_shader_;
	r_GLSLProgram crosshair_shader_;
	r_GLSLProgram supersampling_final_shader_;

	//VBO
	r_PolygonBuffer build_prism_vbo_;
	r_PolygonBuffer skybox_vbo_;
	r_PolygonBuffer stars_vbo_;

	struct
	{
		m_Vec3 current_sun_light;
		m_Vec3 current_fire_light;
		m_Vec3 sun_direction;
		m_Vec3 sky_color;
		float stars_brightness;
	} lighting_data_;

	//framebuffers
	unsigned viewport_width_, viewport_height_;
	r_Framebuffer supersampling_buffer_;
	bool use_supersampling_;
	unsigned int pixel_size_;

	//textures
	r_TextureManager texture_manager_;
	r_Texture sun_texture_;
	r_Texture water_texture_;
	r_Texture console_bg_texture_;
	r_Texture crosshair_texture_;

	//matrices and vectors
	float fov_x_, fov_y_;
	m_Mat4 rotation_matrix_; // camera rotation * basis change * perspective
	m_Mat4 view_matrix_; // translate * rotation_matrix_
	m_Mat4 block_scale_matrix_;
	m_Mat4 block_final_matrix_;
	m_Mat4 water_final_matrix_;
	m_Vec3 cam_ang_, cam_pos_;

	struct
	{
		std::vector< r_ChunkInfoPtr > chunk_matrix;
		// same as world size
		unsigned int matrix_size[2];
		// Longitude + latitude
		int matrix_position[2];
	} chunks_info_;

	// Read in GPU thread
	struct
	{
		std::vector<bool> chunks_visibility_matrix;
		// Longitude + latitude
		int matrix_position[2];
	} chunks_info_for_drawing_;

	std::unique_ptr<r_WVB> world_vertex_buffer_;
	std::unique_ptr<r_WVB> world_water_vertex_buffer_;
	std::mutex world_vertex_buffer_mutex_;

	//text out
	std::unique_ptr<r_Text> text_manager_;

	r_WeatherEffectsParticleManager weather_effects_particle_manager_;

	uint64_t startup_time_;
};

inline void r_WorldRenderer::SetViewportSize( unsigned int viewport_width, unsigned int viewport_height )
{
	viewport_width_= viewport_width;
	viewport_height_= viewport_height;
}
